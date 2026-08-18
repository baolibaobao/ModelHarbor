#include "support/deterministic_random.h"
#include "support/fake_upstream.h"
#include "support/test_data_dir.h"
#include "support/virtual_clock.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

boost::beast::http::response<boost::beast::http::string_body> get(std::uint16_t port,
                                                                  const char* target) {
    namespace asio = boost::asio;
    namespace http = boost::beast::http;
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    socket.connect({asio::ip::make_address("127.0.0.1"), port});
    http::request<http::empty_body> request{http::verb::get, target, 11};
    request.set(http::field::host, "127.0.0.1");
    http::write(socket, request);
    boost::beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return response;
}

} // namespace

int main() {
    using namespace modelharbor::test_support;
    VirtualClock clock;
    const auto before = clock.now();
    clock.advance(std::chrono::milliseconds(250));
    if (clock.now() - before != std::chrono::milliseconds(250))
        throw std::runtime_error("virtual clock did not advance deterministically");

    DeterministicRandom first(42), second(42);
    for (int i = 0; i < 32; ++i) {
        if (first.next() != second.next())
            throw std::runtime_error("random source is not deterministic");
    }

    TestDataDir data;
    const auto fixture = data.path() / "fixture.json";
    std::ofstream(fixture) << R"({"fixture":true})";
    if (!std::filesystem::exists(fixture))
        throw std::runtime_error("test data directory failed");

    FakeUpstream upstream;
    FakeUpstream::Plan plan;
    plan.mode = FakeUpstream::Mode::SlowSse;
    plan.delayMs = 1;
    upstream.setPlan("/slow", plan);
    FakeUpstream::Plan rateLimited;
    rateLimited.status = 429;
    upstream.setPlan("/429", rateLimited);
    FakeUpstream::Plan unauthorized;
    unauthorized.status = 401;
    upstream.setPlan("/401", unauthorized);
    FakeUpstream::Plan serverError;
    serverError.status = 503;
    upstream.setPlan("/503", serverError);
    FakeUpstream::Plan malformed;
    malformed.mode = FakeUpstream::Mode::Malformed;
    upstream.setPlan("/malformed", malformed);
    FakeUpstream::Plan disconnected;
    disconnected.mode = FakeUpstream::Mode::Disconnect;
    upstream.setPlan("/disconnect", disconnected);
    upstream.start();

    namespace http = boost::beast::http;
    if (get(upstream.port(), "/429").result() != http::status::too_many_requests)
        throw std::runtime_error("fake 429 plan failed");
    if (get(upstream.port(), "/401").result() != http::status::unauthorized)
        throw std::runtime_error("fake 401 plan failed");
    if (get(upstream.port(), "/503").result() != http::status::service_unavailable)
        throw std::runtime_error("fake 5xx plan failed");
    if (get(upstream.port(), "/malformed").body() != "not-json")
        throw std::runtime_error("fake malformed body plan failed");
    if (upstream.planFor("/slow").mode != FakeUpstream::Mode::SlowSse ||
        upstream.planFor("/disconnect").mode != FakeUpstream::Mode::Disconnect)
        throw std::runtime_error("fake streaming plans failed");
    if (upstream.requestCount() != 4)
        throw std::runtime_error("fake request count failed");

    upstream.stop();
    std::cout << "ModelHarbor test support OK\n";
    return 0;
}
