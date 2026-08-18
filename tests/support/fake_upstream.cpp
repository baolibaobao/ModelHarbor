#include "fake_upstream.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <memory>
#include <sstream>

namespace modelharbor::test_support {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

FakeUpstream::FakeUpstream()
    : acceptor_(io_, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0)) {}

FakeUpstream::~FakeUpstream() { stop(); }

void FakeUpstream::start() {
    if (running_.exchange(true))
        return;
    acceptNext();
    acceptThread_ = std::thread([this] { acceptLoop(); });
}

void FakeUpstream::stop() {
    if (!running_.exchange(false))
        return;
    boost::system::error_code error;
    acceptor_.cancel(error);
    io_.stop();
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    acceptor_.close(error);
    std::scoped_lock lock(workersMutex_);
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }
    workers_.clear();
}

std::uint16_t FakeUpstream::port() const { return acceptor_.local_endpoint().port(); }

void FakeUpstream::setPlan(std::string path, Plan plan) {
    std::scoped_lock lock(plansMutex_);
    plans_[std::move(path)] = std::move(plan);
}

FakeUpstream::Plan FakeUpstream::planFor(const std::string& path) const {
    std::scoped_lock lock(plansMutex_);
    if (const auto it = plans_.find(path); it != plans_.end())
        return it->second;
    return {};
}

void FakeUpstream::acceptLoop() { io_.run(); }

void FakeUpstream::acceptNext() {
    auto socket = std::make_shared<tcp::socket>(io_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& error) {
        if (error || !running_)
            return;
        std::scoped_lock lock(workersMutex_);
        workers_.emplace_back([this, socket]() mutable { handle(std::move(*socket)); });
        if (running_)
            acceptNext();
    });
}

void FakeUpstream::handle(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::empty_body> request;
    boost::system::error_code error;
    http::read(socket, buffer, request, error);
    if (error)
        return;
    ++requestCount_;
    const Plan plan = planFor(std::string(request.target()));

    if (plan.mode == Mode::Sse || plan.mode == Mode::SlowSse || plan.mode == Mode::Disconnect) {
        std::ostringstream header;
        header << "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
               << "Cache-Control: no-cache\r\nTransfer-Encoding: chunked\r\n"
               << "Connection: close\r\n\r\n";
        asio::write(socket, asio::buffer(header.str()), error);
        if (error)
            return;
        for (const auto& chunk : plan.chunks) {
            std::ostringstream frame;
            frame << std::hex << chunk.size() << "\r\n" << chunk << "\r\n";
            asio::write(socket, asio::buffer(frame.str()), error);
            if (error)
                return;
            if (plan.mode == Mode::SlowSse && plan.delayMs != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(plan.delayMs));
            }
        }
        if (plan.mode != Mode::Disconnect)
            asio::write(socket, asio::buffer("0\r\n\r\n"), error);
        socket.shutdown(tcp::socket::shutdown_both, error);
        return;
    }

    http::status status = static_cast<http::status>(plan.status);
    std::string body = plan.mode == Mode::Malformed ? "not-json" : plan.body;
    http::response<http::string_body> response{status, request.version()};
    response.set(http::field::content_type, "application/json");
    response.body() = body;
    response.prepare_payload();
    response.keep_alive(false);
    http::write(socket, response, error);
    socket.shutdown(tcp::socket::shutdown_both, error);
}

} // namespace modelharbor::test_support
