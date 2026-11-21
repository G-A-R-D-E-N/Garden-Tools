#include "McpCatalog.hpp"
#include "McpPathPolicy.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

std::optional<PathArgument> pathArgument(const std::string& name, std::string_view type) {
    if (type == "rpath") return PathArgument{name, PathUse::Read, PathForm::Single};
    if (type == "wpath") return PathArgument{name, PathUse::Write, PathForm::Single};
    if (type == "rlist") return PathArgument{name, PathUse::Read, PathForm::SemicolonList};
    if (type == "rlines") return PathArgument{name, PathUse::Read, PathForm::LineList};
    if (type == "rpaths") return PathArgument{name, PathUse::Read, PathForm::JsonList};
    if (type == "dpath") return PathArgument{name, PathUse::DataRelative, PathForm::Single};
    if (type == "dpaths" || type == "dlist") return PathArgument{name, PathUse::DataRelative, PathForm::JsonList};
    if (type == "pname") return PathArgument{name, PathUse::PluginName, PathForm::Single};
    return std::nullopt;
}

bool addCatalogTool(fo4ide::mcp::Registry& registry,
                    const fo4ide::rpc::Dispatcher& rpc,
                    const std::shared_ptr<spdlog::logger>& log,
                    const fo4ide::settings::SharedSettings& settings,
                    const AutoRootResolver& autoRoots,
                    const CatalogTool& spec) {
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    std::vector<std::string> order;
    std::vector<PathArgument> paths;
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
        if (typeText == "json") schema = nlohmann::json{{"type", nlohmann::json::array({"object", "array", "string", "number", "integer", "boolean"})}};
        if (typeText == "array") schema = nlohmann::json{{"type", "array"}, {"items", {{"type", "string"}}}};
        if (auto path = pathArgument(name, typeText)) {
            schema = typeText == "dlist" ? nlohmann::json{{"type", "array"}, {"items", {{"type", "string"}}}}
                                         : nlohmann::json{{"type", "string"}};
            paths.push_back(std::move(*path));
        }
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
        return fo4ide::mcp::rpcToolResult(rpc.invoke({target, method, std::move(rpcArgs)}));
    };
    auto tool = std::make_shared<fo4ide::mcp::FunctionTool>(std::move(definition), std::move(handler));
    return registerTool(registry, withPathPolicy(std::move(tool), std::move(paths), settings, autoRoots), log);
}

}

bool registerCatalogTools(fo4ide::mcp::Registry& registry,
                          const fo4ide::rpc::Dispatcher& rpc,
                          const std::shared_ptr<spdlog::logger>& log,
                          const fo4ide::settings::SharedSettings& settings,
                          const AutoRootResolver& autoRoots) {
    for (const auto tools : {readCatalogTools(), editCatalogTools(), serviceCatalogTools(), assetCatalogTools(), layoutCatalogTools()}) {
        for (const auto& spec : tools) {
            if (!addCatalogTool(registry, rpc, log, settings, autoRoots, spec)) return false;
        }
    }
    return true;
}

}
