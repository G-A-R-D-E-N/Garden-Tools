#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <fo4ide/events/EventBus.hpp>
#include <fo4ide/http/HttpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

using namespace std::chrono_literals;

namespace {

class EchoTarget final : public fo4ide::rpc::Target {
public:
    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                           const nlohmann::json& args) override {
        if (method == "Echo") return args.at(0);
        return std::unexpected(fo4ide::Error{"UnknownMethod", "unknown"});
    }
};

std::filesystem::path tempWebRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() / ("fo4ide_http_test_" + std::to_string(id));
    std::filesystem::create_directories(root);
    std::ofstream(root / "index.html") << "<html><body>native ui</body></html>";
    std::ofstream(root / "app.js") << "console.log('native');";
    return root;
}

struct Fixture {
    fo4ide::events::EventBus events;
    fo4ide::rpc::Dispatcher rpc;
    fo4ide::http::HttpServer server{rpc, events};
    std::filesystem::path webRoot{tempWebRoot()};
    int port{0};

    Fixture() {
        auto registered = rpc.registerTarget("test", std::make_shared<EchoTarget>());
        if (!registered) throw std::runtime_error(registered.error().message);
        auto started = server.start({.host = "127.0.0.1", .port = 0, .webRoot = webRoot});
        if (!started) throw std::runtime_error(started.error().message);
        port = *started;
    }

    ~Fixture() {
        server.stop();
        events.shutdown();
        std::error_code ec;
        std::filesystem::remove_all(webRoot, ec);
    }
};

}

TEST_CASE("HTTP health reports native RPC targets") {
    Fixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    auto response = client.Get("/api/health");

    REQUIRE(response);
    CHECK(response->status == 200);
    const auto json = nlohmann::json::parse(response->body);
    CHECK(json.at("ok") == true);
    CHECK(json.at("native") == true);
    CHECK(json.at("targets") == nlohmann::json::array({"test"}));
}

TEST_CASE("HTTP RPC preserves the existing JSON envelope") {
    Fixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);
    const nlohmann::json request{
        {"target", "test"},
        {"method", "Echo"},
        {"args", nlohmann::json::array({"hello"})}
    };

    auto response = client.Post("/rpc", request.dump(), "application/json");
    REQUIRE(response);
    CHECK(response->status == 200);
    CHECK(nlohmann::json::parse(response->body) == nlohmann::json{{"ok", true}, {"value", "hello"}});
}

TEST_CASE("HTTP server serves the existing web bundle shape") {
    Fixture fixture;
    httplib::Client client("127.0.0.1", fixture.port);

    auto index = client.Get("/");
    REQUIRE(index);
    CHECK(index->status == 200);
    CHECK(index->body.find("native ui") != std::string::npos);

    auto asset = client.Get("/app.js");
    REQUIRE(asset);
    CHECK(asset->status == 200);
    CHECK(asset->body.find("console.log") != std::string::npos);
}

TEST_CASE("SSE emits existing Type plus payload fields") {
    Fixture fixture;
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
    auto result = server.start({.host = "0.0.0.0", .port = 0});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "UnsafeHttpBind");
}
