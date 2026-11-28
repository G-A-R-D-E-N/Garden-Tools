#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <fo4ide/rpc/RemoteTarget.hpp>

namespace {

std::string testToken() {
    std::string token;
    for (int i = 0; i < 32; ++i) token.push_back("0123456789abcdef"[(i * 7) % 16]);
    return token;
}

struct FakeGui {
    FakeGui() {
        server.Get("/api/health", [](const httplib::Request&, httplib::Response& response) {
            response.set_content(R"({"ok":true,"native":true,"targets":["cell","app"]})", "application/json");
        });
        server.Post("/rpc", [this](const httplib::Request& request, httplib::Response& response) {
            if (request.get_header_value("X-FO4IDE-Token") != token) {
                response.status = 401;
                response.set_content(
                    R"({"ok":false,"error":"Missing or invalid X-FO4IDE-Token","type":"Unauthorized"})",
                    "application/json");
                return;
            }
            const auto json = nlohmann::json::parse(request.body);
            if (json.at("method") == "Fail") {
                response.set_content(R"({"ok":false,"error":"no plugin","type":"PluginNotFound"})",
                                     "application/json");
                return;
            }
            const nlohmann::json value{
                {"client", request.get_header_value("X-FO4IDE-Client")},
                {"request", json}
            };
            response.set_content(nlohmann::json{{"ok", true}, {"value", value}}.dump(), "application/json");
        });
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }

    ~FakeGui() {
        server.stop();
        if (thread.joinable()) thread.join();
    }

    FakeGui(const FakeGui&) = delete;
    FakeGui& operator=(const FakeGui&) = delete;

    std::string token = testToken();
    httplib::Server server;
    std::thread thread;
    int port{0};
};

}

TEST_CASE("remote target forwards the call with the access token and MCP client header") {
    FakeGui gui;
    fo4ide::rpc::RemoteTarget target({.host = "127.0.0.1", .port = gui.port, .token = gui.token}, "cell");
    const auto result = target.invoke("Echo", nlohmann::json::array({"a", 2}));
    REQUIRE(result.has_value());
    CHECK(result->at("client") == "mcp");
    CHECK(result->at("request").at("target") == "cell");
    CHECK(result->at("request").at("method") == "Echo");
    CHECK(result->at("request").at("args")[1] == 2);
}

TEST_CASE("remote target keeps the GUI error type and message") {
    FakeGui gui;
    fo4ide::rpc::RemoteTarget target({.host = "127.0.0.1", .port = gui.port, .token = gui.token}, "cell");
    const auto result = target.invoke("Fail", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "PluginNotFound");
    CHECK(result.error().message == "no plugin");
}

TEST_CASE("remote target reports a rejected token as Unauthorized") {
    FakeGui gui;
    fo4ide::rpc::RemoteTarget target({.host = "127.0.0.1", .port = gui.port, .token = "wrong"}, "app");
    const auto result = target.invoke("GetHealth", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "Unauthorized");
}

TEST_CASE("remote target reports an unreachable GUI as RemoteUnavailable") {
    int closedPort = 0;
    {
        FakeGui gui;
        closedPort = gui.port;
    }
    fo4ide::rpc::RemoteTarget target({.host = "127.0.0.1", .port = closedPort, .token = testToken()}, "app");
    const auto result = target.invoke("GetHealth", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "RemoteUnavailable");
}

TEST_CASE("GUI health lists the native RPC target names") {
    FakeGui gui;
    const auto names = fo4ide::rpc::fetchRemoteTargetNames({.host = "127.0.0.1", .port = gui.port, .token = ""});
    REQUIRE(names.has_value());
    CHECK(*names == std::vector<std::string>{"cell", "app"});
}
