#include "McpCatalog.hpp"

#include <string>
#include <utility>
#include <vector>

namespace fo4ide::app {
namespace {

bool registerTool(fo4ide::mcp::Registry& registry,
                  std::shared_ptr<fo4ide::mcp::Tool> tool,
                  const std::shared_ptr<spdlog::logger>& log) {
    auto registered = registry.registerTool(std::move(tool));
    if (registered) return true;
    log->error("MCP setup failed: {}", registered.error().message);
    return false;
}

std::vector<std::string_view> splitFields(std::string_view fields) {
    std::vector<std::string_view> out;
    if (fields.empty()) return out;
    std::size_t start = 0;
    while (start <= fields.size()) {
        const auto end = fields.find(',', start);
        out.push_back(fields.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

bool addCatalogTool(fo4ide::mcp::Registry& registry,
                    const fo4ide::rpc::Dispatcher& rpc,
                    const std::shared_ptr<spdlog::logger>& log,
                    const CatalogTool& spec) {
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    std::vector<std::string> order;
    for (const auto field : splitFields(spec.fields)) {
        const auto colon = field.find(':');
        if (colon == std::string_view::npos) continue;
        std::string name(field.substr(0, colon));
        std::string_view type = field.substr(colon + 1);
        bool optional = false;
        if (!type.empty() && type.back() == '?') {
            optional = true;
            type.remove_suffix(1);
        }
        const std::string typeText(type);
        nlohmann::json schema{{"type", typeText}};
        if (typeText == "array") schema = nlohmann::json{{"type", "array"}, {"items", {{"type", "string"}}}};
        properties[name] = std::move(schema);
        if (!optional) required.push_back(name);
        order.push_back(std::move(name));
    }
    nlohmann::json inputSchema{{"type", "object"}, {"properties", std::move(properties)},
                               {"additionalProperties", false}};
    if (!required.empty()) inputSchema["required"] = std::move(required);
    fo4ide::mcp::ToolDefinition definition{std::string(spec.name), std::string(spec.description),
                                           inputSchema};
    auto handler = [&rpc, target = std::string(spec.target), method = std::string(spec.method),
                    schema = std::move(inputSchema), order = std::move(order)](const nlohmann::json& arguments) {
        if (const auto error = fo4ide::mcp::argumentError(schema, arguments)) {
            return fo4ide::mcp::ToolResult{*error, true};
        }
        nlohmann::json rpcArgs = nlohmann::json::array();
        for (const auto& name : order) {
            const auto it = arguments.find(name);
            rpcArgs.push_back(it == arguments.end() ? nlohmann::json(nullptr) : *it);
        }
        const auto response = rpc.invoke({target, method, std::move(rpcArgs)});
        if (!response.ok) {
            const auto prefix = response.type.empty() ? std::string{} : response.type + ": ";
            return fo4ide::mcp::ToolResult{prefix + response.error, true};
        }
        if (response.value.is_string()) {
            const auto text = response.value.get<std::string>();
            const bool failure = text.rfind("Error:", 0) == 0 || text.rfind("ERR:", 0) == 0;
            return fo4ide::mcp::ToolResult{text, failure};
        }
        if (response.value.is_null()) return fo4ide::mcp::ToolResult{"", false};
        return fo4ide::mcp::ToolResult{
            response.value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false};
    };
    auto tool = std::make_shared<fo4ide::mcp::FunctionTool>(std::move(definition), std::move(handler));
    return registerTool(registry, std::move(tool), log);
}

}

bool registerCatalogTools(fo4ide::mcp::Registry& registry,
                          const fo4ide::rpc::Dispatcher& rpc,
                          const std::shared_ptr<spdlog::logger>& log) {
    for (const auto tools : {readCatalogTools(), editCatalogTools(), serviceCatalogTools()}) {
        for (const auto& spec : tools) {
            if (!addCatalogTool(registry, rpc, log, spec)) return false;
        }
    }
    return true;
}

}
