#include <fo4ide/mcp/McpServer.hpp>

#include <algorithm>
#include <exception>
#include <mutex>
#include <utility>

namespace fo4ide::mcp {

nlohmann::json ToolDefinition::toJson() const {
    return {
        {"name", name},
        {"description", description},
        {"inputSchema", inputSchema}
    };
}

std::optional<std::string> argumentError(const nlohmann::json& inputSchema,
                                         const nlohmann::json& arguments) {
    if (!arguments.is_object()) return "Arguments must be an object";
    const auto properties = inputSchema.value("properties", nlohmann::json::object());
    for (const auto& [name, value] : arguments.items()) {
        static_cast<void>(value);
        if (!properties.contains(name)) return "Unknown argument: " + name;
    }
    for (const auto& field : inputSchema.value("required", nlohmann::json::array())) {
        const auto name = field.get<std::string>();
        if (!arguments.contains(name)) return "Missing argument: " + name;
    }
    for (const auto& [name, schema] : properties.items()) {
        const auto value = arguments.find(name);
        if (value == arguments.end()) continue;
        const auto type = schema.value("type", std::string{});
        const bool valid = (type == "string" && value->is_string()) ||
                           (type == "object" && value->is_object()) ||
                           (type == "array" && value->is_array()) ||
                           (type == "integer" && value->is_number_integer()) ||
                           (type == "number" && value->is_number()) ||
                           (type == "boolean" && value->is_boolean());
        if (!valid) return name + " must be " + type;
    }
    return std::nullopt;
}

FunctionTool::FunctionTool(ToolDefinition definition, Handler handler)
    : definition_(std::move(definition)), handler_(std::move(handler)) {}

const ToolDefinition& FunctionTool::definition() const noexcept {
    return definition_;
}

ToolResult FunctionTool::call(const nlohmann::json& arguments) {
    if (!handler_) return {"Tool handler is not configured", true};
    try {
        return handler_(arguments);
    } catch (const std::exception& ex) {
        return {ex.what(), true};
    } catch (...) {
        return {"Unhandled non-standard exception", true};
    }
}

RpcTool::RpcTool(ToolDefinition definition,
                 const rpc::Dispatcher& dispatcher,
                 std::string target,
                 std::string method,
                 std::vector<std::string> argumentOrder)
    : definition_(std::move(definition)),
      dispatcher_(dispatcher),
      target_(std::move(target)),
      method_(std::move(method)),
      argumentOrder_(std::move(argumentOrder)) {}

const ToolDefinition& RpcTool::definition() const noexcept {
    return definition_;
}

ToolResult RpcTool::call(const nlohmann::json& arguments) {
    nlohmann::json rpcArgs = nlohmann::json::array();
    if (arguments.is_array()) {
        rpcArgs = arguments;
    } else if (arguments.is_object()) {
        for (const auto& name : argumentOrder_) {
            const auto it = arguments.find(name);
            rpcArgs.push_back(it == arguments.end() ? nlohmann::json(nullptr) : *it);
        }
    } else if (!arguments.is_null()) {
        return {"MCP tool arguments must be an object", true};
    }

    const auto response = dispatcher_.invoke({target_, method_, std::move(rpcArgs)});
    if (!response.ok) {
        const auto prefix = response.type.empty() ? std::string{} : response.type + ": ";
        return {prefix + response.error, true};
    }

    if (response.value.is_string()) {
        auto text = response.value.get<std::string>();
        const bool failure = text.rfind("Error:", 0) == 0 || text.rfind("ERR:", 0) == 0;
        return {std::move(text), failure};
    }
    if (response.value.is_null()) return {"", false};
    return {response.value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false};
}

Result<void> Registry::registerTool(std::shared_ptr<Tool> tool) {
    if (!tool) return std::unexpected(Error{"InvalidTool", "MCP tool is null"});
    const auto name = tool->definition().name;
    if (name.empty()) return std::unexpected(Error{"InvalidTool", "MCP tool name is empty"});

    std::unique_lock lock(mutex_);
    if (tools_.contains(name)) {
        return std::unexpected(Error{"DuplicateTool", "MCP tool is already registered: " + name});
    }
    tools_.emplace(name, std::move(tool));
    return {};
}

nlohmann::json Registry::definitions() const {
    std::vector<nlohmann::json> rows;
    {
        std::shared_lock lock(mutex_);
        rows.reserve(tools_.size());
        for (const auto& [name, tool] : tools_) {
            static_cast<void>(name);
            rows.push_back(tool->definition().toJson());
        }
    }
    std::ranges::sort(rows, [](const nlohmann::json& left, const nlohmann::json& right) {
        return left.at("name").get_ref<const std::string&>() < right.at("name").get_ref<const std::string&>();
    });
    return rows;
}

ToolResult Registry::call(std::string_view name, const nlohmann::json& arguments) const {
    std::shared_ptr<Tool> tool;
    {
        std::shared_lock lock(mutex_);
        const auto it = tools_.find(std::string(name));
        if (it == tools_.end()) return {"Unknown MCP tool: " + std::string(name), true};
        tool = it->second;
    }
    {
        std::lock_guard lock(hookMutex_);
        if (beforeFirstCall_) {
            if (auto error = beforeFirstCall_()) return {std::move(*error), true};
            beforeFirstCall_ = nullptr;
        }
    }
    return tool->call(arguments);
}

void Registry::setBeforeFirstCall(std::function<std::optional<std::string>()> hook) {
    std::lock_guard lock(hookMutex_);
    beforeFirstCall_ = std::move(hook);
}

StdioServer::StdioServer(Registry& registry) : registry_(registry) {}

std::optional<std::string> StdioServer::handleLine(std::string_view line) const {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(line);
    } catch (...) {
        return std::nullopt;
    }

    if (!root.is_object() || !root.contains("id")) return std::nullopt;
    const auto id = root.at("id");
    try {
        const auto method = root.value("method", std::string{});

        if (method == "initialize") {
            return result(id, {
                {"protocolVersion", std::string(ProtocolVersion)},
                {"capabilities", {{"tools", nlohmann::json::object()}}},
                {"serverInfo", {{"name", "fo4editor"}, {"version", "1.0.0"}}}
            });
        }

        if (method == "tools/list") {
            return result(id, {{"tools", registry_.definitions()}});
        }

        if (method == "tools/call") {
            const auto paramsIt = root.find("params");
            if (paramsIt == root.end() || !paramsIt->is_object()) {
                return error(id, -32602, "Invalid params");
            }
            const auto name = paramsIt->value("name", std::string{});
            if (name.empty()) return error(id, -32602, "Tool name is required");
            const auto argsIt = paramsIt->find("arguments");
            const auto arguments = argsIt == paramsIt->end() ? nlohmann::json::object() : *argsIt;
            const auto toolResult = registry_.call(name, arguments);
            return result(id, {
                {"content", nlohmann::json::array({{{"type", "text"}, {"text", toolResult.text}}})},
                {"isError", toolResult.isError}
            });
        }

        if (method == "ping") return result(id, nlohmann::json::object());
        return error(id, -32601, "Method not found: " + method);
    } catch (const std::exception& ex) {
        return error(id, -32603, ex.what());
    }
}

void StdioServer::run(std::istream& input, std::ostream& output) const {
    std::string line;
    while (std::getline(input, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        const auto response = handleLine(line);
        if (!response) continue;
        output << *response << '\n';
        output.flush();
    }
}

std::string StdioServer::result(const nlohmann::json& id, const nlohmann::json& value) const {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", value}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string StdioServer::error(const nlohmann::json& id, int code, std::string message) const {
    return nlohmann::json{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", std::move(message)}}}
    }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}
