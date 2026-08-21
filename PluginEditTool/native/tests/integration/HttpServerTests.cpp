#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <httplib.h>

#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/http/HttpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

using namespace std::chrono_literals;

namespace {

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
               ("fo4ide_http_" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()))),
          server(rpc, events) {
        std::filesystem::create_directories(root / "assets");
        std::ofstream(root / "index.html") << "<html>native</html>";
        std::ofstream(root / "assets" / "app.js") << "console.log('native');";
        REQUIRE(rpc.registerTarget("echo", std::make_shared<EchoTarget>()).has_value());
        auto started = server.start({.host = "127.0.0.1", .port = 0, .webRoot = root});
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
    auto response = client.Post("/rpc", request.dump(), "application/json");
    REQUIRE(response);
    REQUIRE(response->status == 200);
    const auto json = nlohmann::json::parse(response->body);
    CHECK(json.at("ok") == true);
    CHECK(json.at("value").at("method") == "Run");
    CHECK(json.at("value").at("args")[0] == "one");
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

    static_cast<void>(client.Get("/events", [&](const char* data, std::size_t length) {
        received.append(data, length);
        return received.find("data: ") == std::string::npos;
    }));

    CHECK(received.find("\"Type\":\"SetStatus\"") != std::string::npos);
    CHECK(received.find("\"Text\":\"Loading\"") != std::string::npos);
}

TEST_CASE("HTTP refuses non-loopback binds") {
    fo4ide::events::EventBus events;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::http::HttpServer server(rpc, events);
    auto result = server.start({.host = "0.0.0.0", .port = 0, .webRoot = {}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "UnsafeHttpBind");
}
