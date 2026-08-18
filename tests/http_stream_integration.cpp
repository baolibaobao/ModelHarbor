#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using asio::ip::tcp;

void writeChunk(tcp::socket& socket, const std::string& data) {
    const std::string size = [&data]() {
        char buffer[32]{};
        sprintf_s(buffer, "%zx", data.size());
        return std::string(buffer);
    }();
    const std::string prefix = size + "\r\n";
    const std::string suffix = "\r\n";
    asio::write(socket, asio::buffer(prefix));
    asio::write(socket, asio::buffer(data));
    asio::write(socket, asio::buffer(suffix));
}

void writeChunkTerminator(tcp::socket& socket) {
    asio::write(socket, asio::buffer(std::string("0\r\n\r\n")));
}

class FakeUpstream final {
  public:
    FakeUpstream() : acceptor_(io_, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)) {}

    ~FakeUpstream() { stop(); }

    void start() {
        running_ = true;
        acceptNext();
        thread_ = std::thread([this]() { io_.run(); });
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        boost::system::error_code error;
        acceptor_.cancel(error);
        io_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
        acceptor_.close(error);
        {
            std::scoped_lock socketLock(socketsMutex_);
            for (const auto& weakSocket : sockets_) {
                if (const auto socket = weakSocket.lock()) {
                    socket->close(error);
                }
            }
        }
        std::scoped_lock lock(workersMutex_);
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

  private:
    void acceptNext() {
        auto socket = std::make_shared<tcp::socket>(io_);
        acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& error) {
            if (error || !running_) {
                return;
            }
            {
                std::scoped_lock socketLock(socketsMutex_);
                sockets_.push_back(socket);
            }
            {
                std::scoped_lock lock(workersMutex_);
                workers_.emplace_back([this, socket]() { handle(*socket); });
            }
            if (running_) {
                acceptNext();
            }
        });
    }

    void handle(tcp::socket& socket) {
        beast::flat_buffer buffer;
        http::request<http::empty_body> request;
        boost::system::error_code error;
        http::read(socket, buffer, request, error);
        if (error) {
            return;
        }

        const bool shortStream = request.target() == "/short";
        http::response<http::empty_body> response{http::status::ok, request.version()};
        response.set(http::field::content_type, "text/event-stream");
        response.set(http::field::cache_control, "no-cache");
        response.chunked(true);
        response.keep_alive(false);
        http::serializer<false, http::empty_body> serializer{response};
        serializer.split(true);
        http::write_header(socket, serializer, error);
        if (error) {
            return;
        }

        if (shortStream) {
            asio::write(socket, asio::buffer(std::string("100\r\ndata: partial")), error);
            socket.shutdown(tcp::socket::shutdown_both, error);
            return;
        }

        constexpr std::size_t totalBytes = 1024 * 1024;
        constexpr std::size_t chunkBytes = 16 * 1024;
        std::string chunk(chunkBytes, 'x');
        std::size_t sent = 0;
        while (sent < totalBytes) {
            const std::string event = "data: " + chunk + "\n\n";
            writeChunk(socket, event);
            sent += chunk.size();
        }
        writeChunkTerminator(socket);
        socket.shutdown(tcp::socket::shutdown_both, error);
    }

    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::atomic_bool running_{false};
    std::thread thread_;
    std::mutex workersMutex_;
    std::vector<std::thread> workers_;
    std::mutex socketsMutex_;
    std::vector<std::weak_ptr<tcp::socket>> sockets_;
};

struct RelayContext {
    tcp::socket* downstream = nullptr;
    std::atomic_bool failed{false};
};

size_t relayWrite(char* data, size_t size, size_t count, void* userData) {
    auto* context = static_cast<RelayContext*>(userData);
    const std::size_t length = size * count;
    try {
        writeChunk(*context->downstream, std::string(data, length));
        return length;
    } catch (const std::exception&) {
        context->failed = true;
        return 0;
    }
}

class RelayServer final {
  public:
    explicit RelayServer(std::uint16_t upstreamPort)
        : upstreamPort_(upstreamPort),
          acceptor_(io_, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)) {}

    ~RelayServer() { stop(); }

    void start() {
        running_ = true;
        acceptNext();
        thread_ = std::thread([this]() { io_.run(); });
    }

    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        boost::system::error_code error;
        acceptor_.cancel(error);
        io_.stop();
        {
            std::scoped_lock lock(socketsMutex_);
            for (const auto& weakSocket : sockets_) {
                if (const auto socket = weakSocket.lock()) {
                    socket->close(error);
                }
            }
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        acceptor_.close(error);
    }

    std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    int activeConnections() const { return activeConnections_.load(); }

  private:
    void acceptNext() {
        auto socket = std::make_shared<tcp::socket>(io_);
        acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& error) {
            if (error || !running_) {
                return;
            }
            {
                std::scoped_lock lock(socketsMutex_);
                sockets_.push_back(socket);
            }
            ++activeConnections_;
            handle(*socket);
            --activeConnections_;
            if (running_) {
                acceptNext();
            }
        });
    }

    void handle(tcp::socket& socket) {
        beast::flat_buffer buffer;
        http::request<http::empty_body> request;
        boost::system::error_code error;
        http::read(socket, buffer, request, error);
        if (error) {
            return;
        }

        http::response<http::empty_body> response{http::status::ok, request.version()};
        response.set(http::field::content_type, "text/event-stream");
        response.set(http::field::cache_control, "no-cache");
        response.chunked(true);
        response.keep_alive(false);
        http::serializer<false, http::empty_body> serializer{response};
        serializer.split(true);
        http::write_header(socket, serializer, error);
        if (error) {
            return;
        }

        CURLM* multi = curl_multi_init();
        CURL* easy = curl_easy_init();
        if (multi == nullptr || easy == nullptr) {
            if (easy != nullptr) {
                curl_easy_cleanup(easy);
            }
            if (multi != nullptr) {
                curl_multi_cleanup(multi);
            }
            return;
        }

        const bool shortStream = request.target() == "/short";
        const std::string url = "http://127.0.0.1:" + std::to_string(upstreamPort_) +
                                (shortStream ? "/short" : "/stream");
        RelayContext context{&socket};
        curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, relayWrite);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &context);
        curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 2000L);
        curl_multi_add_handle(multi, easy);

        int running = 0;
        curl_multi_perform(multi, &running);
        while (running > 0 && !context.failed) {
            curl_multi_poll(multi, nullptr, 0, 50, nullptr);
            curl_multi_perform(multi, &running);
        }

        CURLcode result = CURLE_OK;
        int messages = 0;
        while (CURLMsg* message = curl_multi_info_read(multi, &messages)) {
            if (message->msg == CURLMSG_DONE) {
                result = message->data.result;
            }
        }
        curl_multi_remove_handle(multi, easy);
        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);

        if (!context.failed && result == CURLE_OK) {
            writeChunkTerminator(socket);
        }
        socket.shutdown(tcp::socket::shutdown_both, error);
    }

    const std::uint16_t upstreamPort_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::atomic_bool running_{false};
    std::atomic_int activeConnections_{0};
    std::thread thread_;
    std::mutex socketsMutex_;
    std::vector<std::weak_ptr<tcp::socket>> sockets_;
};

class ChunkReader final {
  public:
    explicit ChunkReader(tcp::socket& socket) : socket_(socket) {
        boost::system::error_code error;
        socket_.non_blocking(true, error);
    }

    bool readHeader() { return readUntil("\r\n\r\n"); }

    enum class Result { Chunk, End, Closed, Error };

    Result readChunk(std::size_t* bytes) {
        if (!readUntil("\r\n")) {
            return Result::Closed;
        }
        const std::size_t delimiter = buffer_.find("\r\n");
        const std::string lengthText = buffer_.substr(0, delimiter);
        buffer_.erase(0, delimiter + 2);
        std::size_t length = 0;
        try {
            length = std::stoull(lengthText, nullptr, 16);
        } catch (const std::exception&) {
            return Result::Error;
        }
        if (length == 0) {
            return Result::End;
        }
        if (!readBytes(length + 2)) {
            return Result::Closed;
        }
        *bytes += length;
        buffer_.erase(0, length + 2);
        return Result::Chunk;
    }

  private:
    bool readUntil(const std::string& delimiter) {
        while (buffer_.find(delimiter) == std::string::npos) {
            if (!readMore()) {
                return false;
            }
        }
        if (delimiter == "\r\n\r\n") {
            buffer_.erase(0, buffer_.find(delimiter) + delimiter.size());
        }
        return true;
    }

    bool readBytes(std::size_t count) {
        while (buffer_.size() < count) {
            if (!readMore()) {
                return false;
            }
        }
        return true;
    }

    bool readMore() {
        char data[64 * 1024];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            boost::system::error_code error;
            const std::size_t size = socket_.read_some(asio::buffer(data), error);
            if (!error) {
                buffer_.append(data, size);
                return size != 0;
            }
            if (error == asio::error::eof || error == asio::error::connection_reset) {
                return false;
            }
            if (error != asio::error::would_block && error != asio::error::try_again) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    tcp::socket& socket_;
    std::string buffer_;
};

bool connectClient(tcp::socket& socket, std::uint16_t port, const char* path) {
    socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
    const std::string request =
        "GET " + std::string(path) + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    asio::write(socket, asio::buffer(request));
    return true;
}

} // namespace

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    FakeUpstream upstream;
    upstream.start();
    RelayServer relay(upstream.port());
    relay.start();

    try {
        asio::io_context io;
        tcp::socket client(io);
        connectClient(client, relay.port(), "/relay");
        ChunkReader reader(client);
        if (!reader.readHeader()) {
            throw std::runtime_error("full stream header failed");
        }
        std::size_t bytes = 0;
        const auto firstStart = std::chrono::steady_clock::now();
        if (reader.readChunk(&bytes) != ChunkReader::Result::Chunk ||
            std::chrono::steady_clock::now() - firstStart > std::chrono::seconds(2)) {
            throw std::runtime_error("first SSE chunk was not incremental");
        }
        ChunkReader::Result result = ChunkReader::Result::Chunk;
        while (result == ChunkReader::Result::Chunk) {
            result = reader.readChunk(&bytes);
        }
        if (result != ChunkReader::Result::End || bytes < 1024 * 1024) {
            throw std::runtime_error("full SSE stream was truncated");
        }

        tcp::socket cancelled(io);
        connectClient(cancelled, relay.port(), "/relay");
        ChunkReader cancelledReader(cancelled);
        if (!cancelledReader.readHeader() ||
            cancelledReader.readChunk(&bytes) != ChunkReader::Result::Chunk) {
            throw std::runtime_error("cancel setup failed");
        }
        cancelled.close();
        for (int i = 0; i < 40 && relay.activeConnections() != 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (relay.activeConnections() != 0) {
            throw std::runtime_error("client cancellation did not release relay");
        }

        tcp::socket interrupted(io);
        connectClient(interrupted, relay.port(), "/short");
        ChunkReader interruptedReader(interrupted);
        if (!interruptedReader.readHeader()) {
            throw std::runtime_error("short stream header failed");
        }
        std::size_t shortBytes = 0;
        result = ChunkReader::Result::Chunk;
        while (result == ChunkReader::Result::Chunk) {
            result = interruptedReader.readChunk(&shortBytes);
        }
        if (result != ChunkReader::Result::Closed) {
            throw std::runtime_error("upstream disconnect was not observable");
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        relay.stop();
        upstream.stop();
        curl_global_cleanup();
        return 1;
    }

    relay.stop();
    upstream.stop();
    curl_global_cleanup();
    std::cout << "ModelHarbor HTTP SSE integration OK\n";
    return 0;
}
