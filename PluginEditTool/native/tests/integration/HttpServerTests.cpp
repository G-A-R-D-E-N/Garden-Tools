#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <filesystem>
#include <fstream>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <httplib.h>

#include <fo4ide/core/Base64.hpp>
#include <fo4ide/core/PayloadLimits.hpp>
#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/http/HttpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

using namespace std::chrono_literals;

namespace {

constexpr const char* kToken = "0123456789abcdef0123456789abcdef";

class EchoTarget final : public fo4ide::rpc::Target {
public:
    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                          const nlohmann::json& args) override {
        return nlohmann::json{{"method", method}, {"args", args}};
    }
};

struct ServerFixture {
    std::filesystem::path root;
    fo4ide::events::EventBus events;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::http::HttpServer server;
    int port{0};

    ServerFixture()
        : root(std::filesystem::temp_directory_path() /
               ("fo4ide_http_" + fo4ide_test::uniqueTempName())),
          server(rpc, events) {
        std::filesystem::create_directories(root / "assets");
        std::ofstream(root / "index.html") << "<html>native</html>";
        std::ofstream(root / "assets" / "app.js") << "console.log('native');";
        REQUIRE(rpc.registerTarget("echo", std::make_shared<EchoTarget>()).has_value());
        auto started = server.start({.host = "127.0.0.1", .port = 0, .webRoot = root, .token = kToken});
        REQUIRE(started.has_value());
        port = *started;
    }

    ~ServerFixture() {
        server.stop();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

}

TEST_CASE("HTTP health reports native RPC targets") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    auto response = client.Get("/api/health");
    REQUIRE(response);
    REQUIRE(response->status == 200);
    const auto json = nlohmann::json::parse(response->body);
    CHECK(json.at("ok") == true);
    CHECK(json.at("targets")[0] == "echo");
}

TEST_CASE("HTTP RPC preserves the existing JSON envelope") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const nlohmann::json request{
        {"target", "echo"},
        {"method", "Run"},
        {"args", nlohmann::json::array({"one", 2})}
    };
    auto response = client.Post("/rpc", httplib::Headers{{"X-FO4IDE-Token", kToken}},
                                request.dump(), "application/json");
    REQUIRE(response);
    REQUIRE(response->status == 200);
    const auto json = nlohmann::json::parse(response->body);
    CHECK(json.at("ok") == true);
    CHECK(json.at("value").at("method") == "Run");
    CHECK(json.at("value").at("args")[0] == "one");
}

TEST_CASE("HTTP RPC refuses a missing or wrong access token") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);

    auto missing = client.Post("/rpc", std::string{}, "application/json");
    REQUIRE(missing);
    CHECK(missing->status == 401);

    auto wrong = client.Post("/rpc", httplib::Headers{{"X-FO4IDE-Token", "wrong"}}, std::string{},
                             "application/json");
    REQUIRE(wrong);
    CHECK(wrong->status == 401);
}

TEST_CASE("HTTP RPC requires an application/json content type") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const std::string body = R"({"target":"echo","method":"Run","args":[]})";

    auto plain = client.Post("/rpc", httplib::Headers{{"X-FO4IDE-Token", kToken}}, std::string{}, "text/plain");
    REQUIRE(plain);
    CHECK(plain->status == 415);

    auto charset = client.Post("/rpc", httplib::Headers{{"X-FO4IDE-Token", kToken}}, body,
                               "Application/JSON; charset=utf-8");
    REQUIRE(charset);
    CHECK(charset->status == 200);
}

TEST_CASE("HTTP refuses Host headers that are not this loopback server") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const std::string evil = "evil.example:" + std::to_string(fixture.port);

    auto rpc = client.Post("/rpc", httplib::Headers{{"Host", evil}, {"X-FO4IDE-Token", kToken}},
                           std::string{}, "application/json");
    REQUIRE(rpc);
    CHECK(rpc->status == 403);

    auto page = client.Get("/", httplib::Headers{{"Host", evil}});
    REQUIRE(page);
    CHECK(page->status == 403);

    auto local = client.Get("/api/health",
                            httplib::Headers{{"Host", "localhost:" + std::to_string(fixture.port)}});
    REQUIRE(local);
    CHECK(local->status == 200);
}

TEST_CASE("HTTP RPC accepts a body carrying the largest dropped file", "[large]") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const std::vector<std::byte> file(fo4ide::core::kMaxDroppedFileBytes);
    const std::string body = R"({"target":"echo","method":"Run","args":[")" +
                             fo4ide::core::encodeBase64(file) + R"("]})";

    const auto response = client.Post("/rpc", httplib::Headers{{"X-FO4IDE-Token", kToken}},
                                      body, "application/json");
    REQUIRE(response);
    CHECK(response->status == 200);
}

TEST_CASE("HTTP RPC refuses request bodies over the body cap") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const std::string body = R"({"target":"echo","method":"Run","args":[")" +
                             std::string(fo4ide::core::kMaxRequestBytes, 'a') + R"("]})";

    const auto response = client.Post("/rpc", httplib::Headers{{"X-FO4IDE-Token", kToken}},
                                      body, "application/json");
    const bool accepted = response && response->status == 200;
    CHECK_FALSE(accepted);
}

TEST_CASE("HTTP refuses an unauthenticated oversized RPC body before reading it") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const std::string body(fo4ide::core::kMaxRequestBytes + 1, 'a');

    const auto response = client.Post("/rpc", body, "application/json");
    const bool sizeRejected = response && response->status == 413;
    CHECK_FALSE(sizeRejected);
    const bool accepted = response && response->status == 200;
    CHECK_FALSE(accepted);
}

TEST_CASE("HTTP accepts the bracketed IPv6 loopback Host only when bound to ::1") {
    fo4ide::events::EventBus events;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::http::HttpServer server(rpc, events);
    auto started = server.start({.host = "::1", .port = 0, .webRoot = {}, .token = kToken});
    REQUIRE(started.has_value());

    httplib::Client client("::1", *started);
    auto local = client.Get("/api/health",
                            httplib::Headers{{"Host", "[::1]:" + std::to_string(*started)}});
    REQUIRE(local);
    CHECK(local->status == 200);

    auto foreign = client.Get("/api/health",
                              httplib::Headers{{"Host", "evil.example:" + std::to_string(*started)}});
    REQUIRE(foreign);
    CHECK(foreign->status == 403);
}

TEST_CASE("HTTP events require the access token in a header") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    client.set_read_timeout(2s);

    std::string body;
    static_cast<void>(client.Get("/events", [&](const char* data, std::size_t length) {
        body.append(data, length);
        return false;
    }));
    CHECK(body.find("Unauthorized") != std::string::npos);

    body.clear();
    static_cast<void>(client.Get("/events?t=" + std::string(kToken), [&](const char* data, std::size_t length) {
        body.append(data, length);
        return false;
    }));
    CHECK(body.find("Unauthorized") != std::string::npos);
}

TEST_CASE("HTTP HEAD on events requires the access token") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);

    auto head = client.Head("/events");
    REQUIRE(head);
    CHECK(head->status == 401);
}

TEST_CASE("SSE emits existing Type plus payload fields") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    client.set_read_timeout(2s);

    std::string received;
    std::jthread publisher([&fixture] {
        for (int i = 0; i < 100 && fixture.events.subscriberCount() == 0; ++i) {
            std::this_thread::sleep_for(5ms);
        }
        fixture.events.publish({"SetStatus", {{"Text", "Loading"}}});
    });

    static_cast<void>(client.Get("/events", httplib::Headers{{"X-FO4IDE-Token", kToken}},
                                 [&](const char* data, std::size_t length) {
        received.append(data, length);
        return received.find("data: ") == std::string::npos;
    }));

    CHECK(received.find("\"Type\":\"SetStatus\"") != std::string::npos);
    CHECK(received.find("\"Text\":\"Loading\"") != std::string::npos);
}

TEST_CASE("HTTP server serves the existing web bundle shape") {
    ServerFixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);

    auto index = client.Get("/");
    REQUIRE(index);
    CHECK(index->status == 200);
    CHECK(index->body == "<html>native</html>");

    auto asset = client.Get("/assets/app.js");
    REQUIRE(asset);
    CHECK(asset->status == 200);
    CHECK(asset->body == "console.log('native');");
}

TEST_CASE("HTTP refuses non-loopback binds") {
    fo4ide::events::EventBus events;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::http::HttpServer server(rpc, events);
    auto result = server.start({.host = "0.0.0.0", .port = 0, .webRoot = {}, .token = {}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "UnsafeHttpBind");
}

TEST_CASE("HTTP server refuses to start without an access token") {
    fo4ide::events::EventBus events;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::http::HttpServer server(rpc, events);
    auto result = server.start({.host = "127.0.0.1", .port = 0, .webRoot = {}, .token = {}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "HttpTokenMissing");
}

TEST_CASE("HTTP port argument accepts only whole port numbers") {
    CHECK(fo4ide::http::parsePort("8080") == 8080);
    CHECK(fo4ide::http::parsePort("0") == 0);
    CHECK(fo4ide::http::parsePort("65535") == 65535);
    CHECK_FALSE(fo4ide::http::parsePort("65536").has_value());
    CHECK_FALSE(fo4ide::http::parsePort("-1").has_value());
    CHECK_FALSE(fo4ide::http::parsePort("abc").has_value());
    CHECK_FALSE(fo4ide::http::parsePort("80x").has_value());
    CHECK_FALSE(fo4ide::http::parsePort("").has_value());
}
