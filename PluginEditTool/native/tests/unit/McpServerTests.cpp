#include <algorithm>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fo4ide/mcp/McpServer.hpp>
#include <fo4ide/rpc/RpcDispatcher.hpp>

namespace {

class EchoTarget final : public fo4ide::rpc::Target {
public:
    fo4ide::Result<nlohmann::json> invoke(std::string_view method,
                                          const nlohmann::json& args) override {
        if (method != "Echo") {
            return std::unexpected(fo4ide::Error{"UnknownMethod", "unknown"});
        }
        return args;
    }
};

class ThrowingTool final : public fo4ide::mcp::Tool {
public:
    const fo4ide::mcp::ToolDefinition& definition() const noexcept override { return definition_; }
    fo4ide::mcp::ToolResult call(const nlohmann::json&) override {
        throw std::runtime_error("boom");
    }

private:
    fo4ide::mcp::ToolDefinition definition_{.name = "throws", .description = "throws"};
};

fo4ide::mcp::ToolDefinition echoDefinition() {
    return {
        .name = "echo",
        .description = "Echo one value",
        .inputSchema = {
            {"type", "object"},
            {"properties", {{"value", {{"type", "string"}}}}},
            {"required", nlohmann::json::array({"value"})}
        }
    };
}

}

TEST_CASE("MCP initialize matches the existing server contract") {
    fo4ide::mcp::Registry registry;
    fo4ide::mcp::StdioServer server(registry);

    const auto response = server.handleLine(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    REQUIRE(response.has_value());
    const auto json = nlohmann::json::parse(*response);
    CHECK(json.at("jsonrpc") == "2.0");
    CHECK(json.at("id") == 1);
    CHECK(json.at("result").at("protocolVersion") == "2024-11-05");
    CHECK(json.at("result").at("serverInfo").at("name") == "fo4editor");
    CHECK(json.at("result").at("serverInfo").at("version") == "1.0.0");
    CHECK(json.at("result").at("capabilities").contains("tools"));
}

TEST_CASE("MCP notifications and malformed JSON produce no response") {
    fo4ide::mcp::Registry registry;
    fo4ide::mcp::StdioServer server(registry);

    CHECK_FALSE(server.handleLine("not-json").has_value());
    CHECK_FALSE(server.handleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").has_value());
}

TEST_CASE("MCP tool definitions are sorted and callable") {
    fo4ide::mcp::Registry registry;
    auto zeta = std::make_shared<fo4ide::mcp::FunctionTool>(
        fo4ide::mcp::ToolDefinition{.name = "zeta", .description = "z"},
        [](const nlohmann::json&) { return fo4ide::mcp::ToolResult{"z", false}; });
    auto alpha = std::make_shared<fo4ide::mcp::FunctionTool>(
        fo4ide::mcp::ToolDefinition{.name = "alpha", .description = "a"},
        [](const nlohmann::json& args) {
            return fo4ide::mcp::ToolResult{args.value("text", std::string{}), false};
        });
    REQUIRE(registry.registerTool(zeta).has_value());
    REQUIRE(registry.registerTool(alpha).has_value());

    fo4ide::mcp::StdioServer server(registry);
    const auto listed = server.handleLine(R"({"jsonrpc":"2.0","id":"list","method":"tools/list"})");
    REQUIRE(listed.has_value());
    const auto listJson = nlohmann::json::parse(*listed);
    const auto& tools = listJson.at("result").at("tools");
    REQUIRE(tools.size() == 2);
    CHECK(tools.at(0).at("name") == "alpha");
    CHECK(tools.at(1).at("name") == "zeta");

    const auto called = server.handleLine(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"alpha","arguments":{"text":"hello"}}})");
    REQUIRE(called.has_value());
    const auto callJson = nlohmann::json::parse(*called);
    CHECK_FALSE(callJson.at("result").at("isError").get<bool>());
    CHECK(callJson.at("result").at("content").at(0).at("text") == "hello");
}

TEST_CASE("MCP unknown method and unknown tool report structured errors") {
    fo4ide::mcp::Registry registry;
    fo4ide::mcp::StdioServer server(registry);

    const auto method = server.handleLine(R"({"jsonrpc":"2.0","id":1,"method":"nope"})");
    REQUIRE(method.has_value());
    const auto methodJson = nlohmann::json::parse(*method);
    CHECK(methodJson.at("error").at("code") == -32601);

    const auto tool = server.handleLine(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"missing","arguments":{}}})");
    REQUIRE(tool.has_value());
    const auto toolJson = nlohmann::json::parse(*tool);
    CHECK(toolJson.at("result").at("isError").get<bool>());
    CHECK(toolJson.at("result").at("content").at(0).at("text").get<std::string>().find("Unknown MCP tool") != std::string::npos);
}

TEST_CASE("RPC-backed MCP tools map named arguments to positional RPC arguments") {
    fo4ide::rpc::Dispatcher dispatcher;
    REQUIRE(dispatcher.registerTarget("test", std::make_shared<EchoTarget>()).has_value());

    fo4ide::mcp::RpcTool tool(echoDefinition(), dispatcher, "test", "Echo", {"value"});
    const auto result = tool.call({{"value", "hello"}});
    CHECK_FALSE(result.isError);
    CHECK(result.text == R"(["hello"])");
}

TEST_CASE("MCP stdio loop is newline-delimited") {
    fo4ide::mcp::Registry registry;
    fo4ide::mcp::StdioServer server(registry);
    std::istringstream input(
        "\n"
        R"({"jsonrpc":"2.0","id":1,"method":"ping"})" "\n"
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n");
    std::ostringstream output;

    server.run(input, output);
    const auto text = output.str();
    REQUIRE_FALSE(text.empty());
    CHECK(text.back() == '\n');
    CHECK(std::count(text.begin(), text.end(), '\n') == 1);
}

TEST_CASE("MCP invalid UTF-8 tool output is replaced and the stdio loop keeps answering") {
    fo4ide::mcp::Registry registry;
    REQUIRE(registry.registerTool(std::make_shared<fo4ide::mcp::FunctionTool>(
        fo4ide::mcp::ToolDefinition{.name = "bad", .description = "bad"},
        [](const nlohmann::json&) { return fo4ide::mcp::ToolResult{std::string("ok\xff\xfe"), false}; })).has_value());
    fo4ide::mcp::StdioServer server(registry);
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"bad","arguments":{}}})" "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"ping"})" "\n");
    std::ostringstream output;

    server.run(input, output);
    std::istringstream lines(output.str());
    std::string first;
    std::string second;
    REQUIRE(std::getline(lines, first));
    REQUIRE(std::getline(lines, second));

    const auto callJson = nlohmann::json::parse(first);
    const auto text = callJson.at("result").at("content").at(0).at("text").get<std::string>();
    CHECK(text.starts_with("ok"));
    CHECK(text.find("\xEF\xBF\xBD") != std::string::npos);
    CHECK(nlohmann::json::parse(second).at("id") == 2);
}

TEST_CASE("MCP tool exception becomes a JSON-RPC internal error and the stdio loop keeps answering") {
    fo4ide::mcp::Registry registry;
    REQUIRE(registry.registerTool(std::make_shared<ThrowingTool>()).has_value());
    fo4ide::mcp::StdioServer server(registry);
    std::istringstream input(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"throws","arguments":{}}})" "\n"
        R"({"jsonrpc":"2.0","id":4,"method":"ping"})" "\n");
    std::ostringstream output;

    server.run(input, output);
    std::istringstream lines(output.str());
    std::string first;
    std::string second;
    REQUIRE(std::getline(lines, first));
    REQUIRE(std::getline(lines, second));

    const auto failedJson = nlohmann::json::parse(first);
    CHECK(failedJson.at("id") == 3);
    CHECK(failedJson.at("error").at("code") == -32603);
    CHECK(failedJson.at("error").at("message") == "boom");
    CHECK(nlohmann::json::parse(second).at("id") == 4);
}

TEST_CASE("MCP before-first-call hook runs only for known tool calls and retries until it succeeds") {
    fo4ide::mcp::Registry registry;
    int hookRuns = 0;
    int handlerRuns = 0;
    bool failing = true;
    registry.setBeforeFirstCall([&]() -> std::optional<std::string> {
        ++hookRuns;
        if (failing) return std::string("load failed");
        return std::nullopt;
    });
    REQUIRE(registry.registerTool(std::make_shared<fo4ide::mcp::FunctionTool>(
        fo4ide::mcp::ToolDefinition{.name = "probe", .description = "probe"},
        [&](const nlohmann::json&) {
            ++handlerRuns;
            return fo4ide::mcp::ToolResult{"ran", false};
        })).has_value());
    fo4ide::mcp::StdioServer server(registry);

    REQUIRE(server.handleLine(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})").has_value());
    REQUIRE(server.handleLine(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})").has_value());
    CHECK(hookRuns == 0);

    const auto unknown = server.handleLine(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"missing","arguments":{}}})");
    REQUIRE(unknown.has_value());
    CHECK(nlohmann::json::parse(*unknown).at("result").at("isError").get<bool>());
    CHECK(hookRuns == 0);

    const auto callProbe = [&server](int id) {
        const auto response = server.handleLine(
            R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
            R"(,"method":"tools/call","params":{"name":"probe","arguments":{}}})");
        REQUIRE(response.has_value());
        return nlohmann::json::parse(*response).at("result");
    };

    const auto failed = callProbe(4);
    CHECK(failed.at("isError").get<bool>());
    CHECK(failed.at("content").at(0).at("text") == "load failed");
    CHECK(hookRuns == 1);
    CHECK(handlerRuns == 0);

    failing = false;
    const auto retried = callProbe(5);
    CHECK_FALSE(retried.at("isError").get<bool>());
    CHECK(retried.at("content").at(0).at("text") == "ran");
    CHECK(hookRuns == 2);
    CHECK(handlerRuns == 1);

    const auto again = callProbe(6);
    CHECK_FALSE(again.at("isError").get<bool>());
    CHECK(hookRuns == 2);
    CHECK(handlerRuns == 2);
}
