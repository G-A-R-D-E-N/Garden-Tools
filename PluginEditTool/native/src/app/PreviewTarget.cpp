#include <fo4ide/app/PreviewTarget.hpp>

#include <cstddef>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <fo4ide/preview/PreviewRender.hpp>

namespace fo4ide::app {
namespace {

using json = nlohmann::json;

constexpr int kDefaultCellWidth = 800;
constexpr int kDefaultCellHeight = 600;
constexpr int kDefaultModelSide = 512;

Error failure(std::string type, std::string message) {
    return Error{std::move(type), std::move(message)};
}

Result<std::string> requiredString(const json& options, std::string_view key) {
    const std::string name(key);
    if (!options.is_object() || !options.contains(name) || !options.at(name).is_string() ||
        options.at(name).get<std::string>().empty()) {
        return std::unexpected(failure("InvalidArgument", name + " is required"));
    }
    return options.at(name).get<std::string>();
}

Result<json> parseReply(const json& reply, std::string_view what) {
    if (!reply.is_string()) return reply;
    const auto text = reply.get<std::string>();
    json parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected(failure("PreviewFailed", std::string(what) + " is not JSON: " + text.substr(0, 160)));
    }
    if (parsed.is_object() && parsed.contains("error") && parsed.at("error").is_string()) {
        return std::unexpected(failure("PreviewFailed", parsed.at("error").get<std::string>()));
    }
    return parsed;
}

Result<preview::CellData> loadCell(rpc::Target& cells, const std::string& cellId) {
    auto reply = cells.invoke("GetPlacedReferences", json::array({cellId}));
    if (!reply) return std::unexpected(reply.error());
    auto parsed = parseReply(*reply, "GetPlacedReferences");
    if (!parsed) return std::unexpected(parsed.error());
    auto cell = preview::parseCellReferences(*parsed);
    if (!cell) return std::unexpected(failure("PreviewFailed", "cell " + cellId + ": " + cell.error().message));
    return cell;
}

Result<preview::GeometryMap> loadGeometry(rpc::Target& cells, const std::vector<std::string>& paths) {
    if (paths.empty()) return preview::GeometryMap{};
    auto reply = cells.invoke("GetGeometryBatch", json::array({json(paths).dump()}));
    if (!reply) return std::unexpected(reply.error());
    auto parsed = parseReply(*reply, "GetGeometryBatch");
    if (!parsed) return std::unexpected(parsed.error());
    return preview::parseGeometry(*parsed);
}

std::vector<std::string> modelPaths(const preview::CellData& cell, std::size_t maxReferences) {
    std::set<std::string> unique;
    std::size_t walked = 0;
    for (const auto& ref : cell.references) {
        if (ref.isMarker) continue;
        if (walked++ >= maxReferences) break;
        if (ref.scolParts.empty()) {
            if (!ref.modelPath.empty()) unique.insert(ref.modelPath);
            continue;
        }
        for (const auto& part : ref.scolParts) {
            if (!part.modelPath.empty()) unique.insert(part.modelPath);
        }
    }
    return {unique.begin(), unique.end()};
}

Result<json> renderCellPng(rpc::Target& cells, const json& options) {
    auto cellId = requiredString(options, "cell_id");
    if (!cellId) return std::unexpected(cellId.error());
    auto render = preview::parseRenderOptions(options, kDefaultCellWidth, kDefaultCellHeight);
    if (!render) return std::unexpected(render.error());
    auto cell = loadCell(cells, *cellId);
    if (!cell) return std::unexpected(cell.error());
    auto geometry = loadGeometry(cells, modelPaths(*cell, render->budget.maxReferences));
    if (!geometry) return std::unexpected(geometry.error());
    auto image = preview::renderCell(*cell, *geometry, *render);
    if (!image) return std::unexpected(image.error());
    image->report["cellFormKey"] = cell->cellFormKey;
    auto png = preview::encodePngBase64(*image);
    if (!png) return std::unexpected(png.error());
    return json{{"image", {{"mimeType", "image/png"}, {"base64", *png}}}, {"report", image->report}};
}

Result<json> renderModelPng(rpc::Target& cells, const json& options) {
    auto modelPath = requiredString(options, "model_path");
    if (!modelPath) return std::unexpected(modelPath.error());
    json sized = options;
    if (options.is_object() && options.contains("size")) {
        sized["width"] = options.at("size");
        sized["height"] = options.at("size");
    }
    auto render = preview::parseRenderOptions(sized, kDefaultModelSide, kDefaultModelSide);
    if (!render) return std::unexpected(render.error());
    auto geometry = loadGeometry(cells, {*modelPath});
    if (!geometry) return std::unexpected(geometry.error());
    if (geometry->size() != 1) return std::unexpected(failure("PreviewFailed", "model geometry was not returned"));
    const auto& entry = geometry->begin()->second;
    if (!entry.error.empty()) return std::unexpected(failure("PreviewFailed", entry.error));
    auto image = preview::renderModel(entry, *render);
    if (!image) return std::unexpected(image.error());
    image->report["modelPath"] = *modelPath;
    auto png = preview::encodePngBase64(*image);
    if (!png) return std::unexpected(png.error());
    return json{{"image", {{"mimeType", "image/png"}, {"base64", *png}}}, {"report", image->report}};
}

Result<json> cellLayoutSummary(rpc::Target& cells, const json& options) {
    auto cellId = requiredString(options, "cell_id");
    if (!cellId) return std::unexpected(cellId.error());
    auto cell = loadCell(cells, *cellId);
    if (!cell) return std::unexpected(cell.error());
    auto geometry = loadGeometry(cells, modelPaths(*cell, std::numeric_limits<std::size_t>::max()));
    if (!geometry) return std::unexpected(geometry.error());
    return preview::layoutSummary(*cell, *geometry);
}

}  // namespace

PreviewTarget::PreviewTarget(rpc::Target& cells) : cells_(cells) {}

Result<json> PreviewTarget::invoke(std::string_view method, const json& args) {
    json options = args.is_array() && !args.empty() ? args[0] : json::object();
    if (method == "RenderCell") return renderCellPng(cells_, options);
    if (method == "RenderModel") return renderModelPng(cells_, options);
    if (method == "CellLayout") return cellLayoutSummary(cells_, options);
    return std::unexpected(failure("UnknownMethod", "Unknown preview RPC method: " + std::string(method)));
}

}  // namespace fo4ide::app
