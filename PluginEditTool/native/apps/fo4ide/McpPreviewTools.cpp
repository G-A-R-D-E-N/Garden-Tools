#include "McpTools.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4ide::app::AutoRootResolver;
using fo4ide::app::PathArgument;
using fo4ide::app::PathForm;
using fo4ide::app::PathUse;
using fo4ide::app::withPathPolicy;
using fo4ide::mcp::argumentError;
using fo4ide::mcp::FunctionTool;
using fo4ide::mcp::ToolDefinition;
using fo4ide::mcp::ToolImage;
using fo4ide::mcp::ToolResult;

constexpr int kMinPreviewSide = 64;
constexpr int kMaxPreviewSide = 2048;
constexpr int kMaxPreviewReferences = 50000;
constexpr int kMaxHighlightKeys = 64;

nlohmann::json previewSchema(nlohmann::json properties, nlohmann::json required) {
    nlohmann::json schema{{"type", "object"}, {"properties", std::move(properties)}};
    if (!required.empty()) schema["required"] = std::move(required);
    schema["additionalProperties"] = false;
    return schema;
}

nlohmann::json cameraSchema() {
    return {
        {"type", "object"},
        {"description",
         "Camera: {\"preset\": \"front\" | \"right\" | \"top\" | \"iso\"} (default iso; front looks from -Y, right from +X, top down). "
         "Custom: {\"eye\": [x, y, z], \"target\": [x, y, z], \"fov\": 10-120}. Z is up"},
        {"properties",
         {{"preset", {{"type", "string"}, {"enum", {"front", "right", "top", "iso"}}}},
          {"eye", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
          {"target", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
          {"fov", {{"type", "number"}, {"minimum", 10}, {"maximum", 120}}}}},
        {"additionalProperties", false},
    };
}

nlohmann::json layersSchema() {
    const auto flag = [](const char* description) {
        return nlohmann::json{{"type", "boolean"}, {"description", description}};
    };
    return {
        {"type", "object"},
        {"description", "Layer visibility like the cell viewer; every layer defaults to on"},
        {"properties",
         {{"room", flag("Room and portal volumes and room-marker models")},
          {"trigger", flag("Trigger and primitive volumes and trigger models")},
          {"marker", flag("Editor markers, unmodelled references and unresolved markers (wire boxes)")},
          {"effect", flag("Effect models under an effects folder")}}},
        {"additionalProperties", false},
    };
}

nlohmann::json sideSchema(const char* description) {
    return {{"type", "integer"}, {"minimum", kMinPreviewSide}, {"maximum", kMaxPreviewSide}, {"description", description}};
}

std::string errorText(const fo4ide::rpc::Response& response) {
    const auto prefix = response.type.empty() ? std::string{} : response.type + ": ";
    return prefix + response.error;
}

ToolResult textResult(const fo4ide::rpc::Response& response) {
    if (!response.ok) return {errorText(response), true};
    if (response.value.is_string()) return {response.value.get<std::string>(), false};
    return {response.value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false};
}

ToolResult imageResult(const fo4ide::rpc::Response& response) {
    if (!response.ok) return {errorText(response), true};
    const auto& value = response.value;
    if (!value.is_object() || !value.contains("report") || !value.contains("image")) {
        return {"Preview returned no image", true};
    }
    const auto& image = value.at("image");
    if (!image.is_object() || !image.contains("base64") || !image.at("base64").is_string()) {
        return {"Preview returned no image", true};
    }
    ToolImage block{"image/png", image.at("base64").get<std::string>()};
    return ToolResult{value.at("report").dump(-1, ' ', false, nlohmann::json::error_handler_t::replace), false,
                      std::move(block)};
}

bool addPreviewTool(fo4ide::mcp::Registry& registry,
                    const fo4ide::rpc::Dispatcher& rpc,
                    const std::shared_ptr<spdlog::logger>& log,
                    const fo4ide::settings::SharedSettings& settings,
                    const AutoRootResolver& autoRoots,
                    ToolDefinition definition,
                    std::string method,
                    bool withImage,
                    std::vector<PathArgument> paths = {}) {
    const auto schema = definition.inputSchema;
    auto handler = [&rpc, schema, method = std::move(method), withImage](const nlohmann::json& args) -> ToolResult {
        if (const auto error = argumentError(schema, args)) return {*error, true};
        const auto response = rpc.invoke({"preview", method, nlohmann::json::array({args})});
        return withImage ? imageResult(response) : textResult(response);
    };
    auto tool = std::make_shared<FunctionTool>(std::move(definition), std::move(handler));
    auto registered = registry.registerTool(withPathPolicy(std::move(tool), std::move(paths), settings, autoRoots));
    if (registered) return true;
    log->error("MCP setup failed: {}", registered.error().message);
    return false;
}

}  // namespace

bool registerPreviewMcpTools(fo4ide::mcp::Registry& registry,
                             const fo4ide::rpc::Dispatcher& rpc,
                             const std::shared_ptr<spdlog::logger>& log,
                             const fo4ide::settings::SharedSettings& settings,
                             const fo4ide::app::AutoRootResolver& autoRoots) {
    const auto cellIdProperty = nlohmann::json{{"type", "string"},
                                               {"description", "FormKey or editor ID of a cell in a loaded plugin"}};

    if (!addPreviewTool(registry, rpc, log, settings, autoRoots,
                        {"render_cell_preview",
                         "Render a loaded cell's placed references to a PNG (Z-up; front, right, top or iso camera, "
                         "framed on the drawn content). Returns the image plus JSON: drawn and skipped counts "
                         "(missingGeometry, overBudget), markers, bounds, camera, legend and each highlight_keys "
                         "FormKey's pixel position, volumes drawn and unresolved markers. Markers draw as wire boxes; primitive volumes draw as boxes, rings, rectangles or segments by type, and "
                         "room and trigger volumes use the viewer's colours; references past "
                         "max_references are skipped",
                         previewSchema(
                             {{"cell_id", cellIdProperty},
                              {"camera", cameraSchema()},
                              {"layers", layersSchema()},
                              {"width", sideSchema("Image width in pixels (default 800)")},
                              {"height", sideSchema("Image height in pixels (default 600)")},
                              {"highlight_keys",
                               {{"type", "array"},
                                {"items", {{"type", "string"}}},
                                {"maxItems", kMaxHighlightKeys},
                                {"description", "FormKeys to mark in a highlight colour; their pixel positions are reported"}}},
                              {"max_references",
                               {{"type", "integer"}, {"minimum", 1}, {"maximum", kMaxPreviewReferences},
                                {"description", "Most references to draw (default 4000)"}}}},
                             nlohmann::json::array({"cell_id"}))},
                        "RenderCell", true)) return false;

    if (!addPreviewTool(registry, rpc, log, settings, autoRoots,
                        {"render_model_preview",
                         "Render one NIF model's geometry to a PNG (Z-up; camera as render_cell_preview; default iso, "
                         "square size). Returns the image plus JSON: triangles, bounds, camera and legend",
                         previewSchema(
                             {{"model_path", {{"type", "string"}, {"description", "Model path relative to Data, e.g. Architecture/Foo.nif"}}},
                              {"camera", cameraSchema()},
                              {"size", sideSchema("Square image side in pixels (default 512)")}},
                             nlohmann::json::array({"model_path"}))},
                        "RenderModel", true, {{"model_path", PathUse::DataRelative, PathForm::Single}})) return false;

    if (!addPreviewTool(registry, rpc, log, settings, autoRoots,
                        {"get_cell_layout_summary",
                         "Numeric layout of a loaded cell with no image: reference counts by record type, bounds, "
                         "a 16x16 occupancy grid (counts and dominant model per cell), the largest structures by "
                         "bounding box with positions, and floor and ceiling height histograms",
                         previewSchema({{"cell_id", cellIdProperty}}, nlohmann::json::array({"cell_id"}))},
                        "CellLayout", false)) return false;

    return true;
}
