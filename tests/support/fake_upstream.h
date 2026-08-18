#pragma once

#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace modelharbor::test_support {

class FakeUpstream final {
  public:
    enum class Mode { Json, Sse, SlowSse, Disconnect, Malformed };

    struct Plan {
        int status = 200;
        Mode mode = Mode::Json;
        std::string body = R"({"ok":true})";
        std::vector<std::string> chunks{"data: hello\n\n", "data: world\n\n"};
        std::uint32_t delayMs = 0;
    };

    FakeUpstream();
    ~FakeUpstream();

    FakeUpstream(const FakeUpstream&) = delete;
    FakeUpstream& operator=(const FakeUpstream&) = delete;

    void start();
    void stop();
    std::uint16_t port() const;
    void setPlan(std::string path, Plan plan);
    Plan planFor(const std::string& path) const;
    std::size_t requestCount() const { return requestCount_.load(); }

  private:
    void acceptLoop();
    void acceptNext();
    void handle(boost::asio::ip::tcp::socket socket);

    boost::asio::io_context io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    mutable std::mutex plansMutex_;
    std::map<std::string, Plan> plans_;
    std::atomic_bool running_{false};
    std::atomic_size_t requestCount_{0};
    std::thread acceptThread_;
    std::mutex workersMutex_;
    std::vector<std::thread> workers_;
};

} // namespace modelharbor::test_support
