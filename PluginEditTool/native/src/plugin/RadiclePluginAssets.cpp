#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>
#include <fo4ide/plugin/BaseObjectTypes.hpp>
#include <fo4ide/plugin/RadiclePluginAssetCatalog.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/environment/environment.hpp>

namespace fo4ide::plugin {

namespace {

using assets::AssetCatalog;
using assets::AssetFilter;
using assets::AssetRow;
using assets::AssetSearch;
using assets::AssetSort;
using assets::TextField;
using assets::TextMatch;

constexpr std::size_t k_default_limit = 50;
constexpr std::size_t k_max_limit = 200;
constexpr std::size_t k_default_depth = 1;
constexpr std::size_t k_max_depth = 10;
constexpr std::size_t k_folder_samples = 3;
constexpr std::size_t k_max_placements = 200;
constexpr std::size_t k_max_candidates = 20;

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct PlaceAssetRequest {
    std::string patchPlugin;
    std::string cellId;
    std::string asset;
    std::optional<Vec3> position;
    std::string nearKey;
    Vec3 offset;
    Vec3 rotation;
    std::optional<double> scale;
    std::size_t count{1};
    Vec3 spacing;
    double snapGrid{0.0};
    bool persistent{false};
    bool dryRun{false};
};

Error argumentError(std::string message) {
    return Error{"AssetArgumentInvalid", std::move(message)};
}

const nlohmann::json& argumentAt(const nlohmann::json& arguments, std::size_t index) {
    static const nlohmann::json absent;
    if (!arguments.is_array() || index >= arguments.size()) return absent;
    return arguments[index];
}

Result<std::string> readText(const nlohmann::json& value, std::string_view name) {
    if (value.is_null()) return std::string{};
    if (!value.is_string()) return std::unexpected(argumentError(std::string(name) + " must be a string"));
    return value.get<std::string>();
}

Result<std::optional<bool>> readBool(const nlohmann::json& value, std::string_view name) {
    if (value.is_null()) return std::optional<bool>{};
    if (!value.is_boolean()) return std::unexpected(argumentError(std::string(name) + " must be a boolean"));
    return std::optional<bool>(value.get<bool>());
}

Result<std::optional<double>> readNumber(const nlohmann::json& value, std::string_view name) {
    if (value.is_null()) return std::optional<double>{};
    if (!value.is_number()) return std::unexpected(argumentError(std::string(name) + " must be a number"));
    return std::optional<double>(value.get<double>());
}

Result<std::optional<std::size_t>> readCount(const nlohmann::json& value, std::string_view name) {
    if (value.is_null()) return std::optional<std::size_t>{};
    const auto number = core::jsonInt(value);
    if (!number || *number < 0) {
        return std::unexpected(argumentError(std::string(name) + " must be a non-negative integer"));
    }
    return std::optional<std::size_t>(static_cast<std::size_t>(*number));
}

Result<std::optional<Vec3>> readVec(const nlohmann::json& value, std::string_view name) {
    if (value.is_null()) return std::optional<Vec3>{};
    if (!value.is_object()) {
        return std::unexpected(argumentError(std::string(name) + " must be an object with x, y and z"));
    }
    Vec3 out;
    const auto component = [&value](const char* key, double& target) {
        if (!value.contains(key) || !value.at(key).is_number()) return false;
        target = value.at(key).get<double>();
        return true;
    };
    if (!component("x", out.x) || !component("y", out.y) || !component("z", out.z)) {
        return std::unexpected(argumentError(std::string(name) + " must carry numeric x, y and z"));
    }
    return std::optional<Vec3>(out);
}

Result<std::string> normalizeFormKey(std::string_view text) {
    try {
        return fo4::FormKey::parse(std::string(text)).to_string();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
}

Result<std::string> canonicalGroup(const std::string& group) {
    if (group.empty()) return std::string{};
    const auto canonical = assets::canonicalGroupName(group);
    if (!canonical) return std::unexpected(argumentError("Unknown asset group: " + group));
    return *canonical;
}

Result<nlohmann::json> textResult(const nlohmann::json& document) {
    return nlohmann::json(document.dump());
}

nlohmann::json rowJson(const AssetRow& row) {
    const auto typeName = placeableBaseTypeName(row.signature);
    return {
        {"formKey", row.formKey},
        {"editorId", row.editorId},
        {"name", row.name},
        {"signature", row.signature},
        {"typeName", typeName ? std::string(*typeName) : row.signature},
        {"group", assets::groupOfSignature(row.signature)},
        {"modelPath", row.modelPath},
        {"plugin", row.plugin},
        {"deleted", row.deleted},
        {"hasModel", !row.modelPath.empty()},
    };
}

nlohmann::json vecJson(const Vec3& value) {
    return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
}

Result<const AssetRow*> resolveFormKey(const AssetCatalog& catalog, std::string_view text) {
    const auto normalized = normalizeFormKey(text);
    if (!normalized) return std::unexpected(normalized.error());
    for (const auto& row : catalog.rows()) {
        if (row.formKey == *normalized) return &row;
    }
    return std::unexpected(Error{"AssetNotPlaceable",
                                 *normalized + " is not a placeable base object in the winning records"});
}

Result<const AssetRow*> resolveEditorId(const AssetCatalog& catalog, std::string_view editorId) {
    const auto matches = catalog.editorIdMatches(editorId);
    if (matches.empty()) {
        return std::unexpected(Error{"AssetNotFound", "No placeable base object has editor ID " + std::string(editorId)});
    }
    std::vector<std::size_t> live;
    for (const auto index : matches) {
        if (!catalog.rows()[index].deleted) live.push_back(index);
    }
    const auto& pool = live.empty() ? matches : live;
    if (pool.size() == 1) return &catalog.rows()[pool.front()];

    nlohmann::json candidates = nlohmann::json::array();
    for (const auto index : pool) {
        if (candidates.size() == k_max_candidates) break;
        candidates.push_back(rowJson(catalog.rows()[index]));
    }
    return std::unexpected(Error{"AssetAmbiguous",
                                 "Editor ID " + std::string(editorId) + " matches " +
                                     std::to_string(pool.size()) +
                                     " base objects; pass one of these as asset (form key): " +
                                     candidates.dump()});
}

Result<const AssetRow*> resolveAsset(const AssetCatalog& catalog, std::string_view asset) {
    if (asset.empty()) {
        return std::unexpected(argumentError("asset must name a base object by form key or editor ID"));
    }
    if (asset.find(':') != std::string_view::npos) return resolveFormKey(catalog, asset);
    return resolveEditorId(catalog, asset);
}

Result<PlaceAssetRequest> parsePlaceAsset(const nlohmann::json& arguments) {
    PlaceAssetRequest request;
    const auto patch = readText(argumentAt(arguments, 0), "patch_plugin");
    if (!patch) return std::unexpected(patch.error());
    request.patchPlugin = *patch;
    const auto cell = readText(argumentAt(arguments, 1), "cell_id");
    if (!cell) return std::unexpected(cell.error());
    request.cellId = *cell;
    const auto asset = readText(argumentAt(arguments, 2), "asset");
    if (!asset) return std::unexpected(asset.error());
    request.asset = *asset;
    if (request.patchPlugin.empty() || request.cellId.empty() || request.asset.empty()) {
        return std::unexpected(argumentError("place_asset requires patch_plugin, cell_id and asset"));
    }

    const auto position = readVec(argumentAt(arguments, 3), "position");
    if (!position) return std::unexpected(position.error());
    request.position = *position;
    const auto nearKey = readText(argumentAt(arguments, 4), "near_reference_key");
    if (!nearKey) return std::unexpected(nearKey.error());
    const auto offset = readVec(argumentAt(arguments, 5), "offset");
    if (!offset) return std::unexpected(offset.error());
    const auto rotation = readVec(argumentAt(arguments, 6), "rotation");
    if (!rotation) return std::unexpected(rotation.error());
    const auto scale = readNumber(argumentAt(arguments, 7), "scale");
    if (!scale) return std::unexpected(scale.error());
    const auto count = readCount(argumentAt(arguments, 8), "count");
    if (!count) return std::unexpected(count.error());
    const auto spacing = readVec(argumentAt(arguments, 9), "spacing");
    if (!spacing) return std::unexpected(spacing.error());
    const auto snap = readNumber(argumentAt(arguments, 10), "snap_grid");
    if (!snap) return std::unexpected(snap.error());
    const auto persistent = readBool(argumentAt(arguments, 11), "persistent");
    if (!persistent) return std::unexpected(persistent.error());
    const auto dryRun = readBool(argumentAt(arguments, 12), "dry_run");
    if (!dryRun) return std::unexpected(dryRun.error());

    if (position->has_value() && !nearKey->empty()) {
        return std::unexpected(argumentError("Pass either position or near_reference_key, not both"));
    }
    if (!position->has_value() && nearKey->empty()) {
        return std::unexpected(argumentError("place_asset requires position or near_reference_key"));
    }
    if (offset->has_value() && nearKey->empty()) {
        return std::unexpected(argumentError("offset applies only with near_reference_key"));
    }
    if (!nearKey->empty()) {
        const auto normalized = normalizeFormKey(*nearKey);
        if (!normalized) return std::unexpected(normalized.error());
        request.nearKey = *normalized;
    }
    request.offset = offset->value_or(Vec3{});
    request.rotation = rotation->value_or(Vec3{});
    request.spacing = spacing->value_or(Vec3{});

    if (scale->has_value()) {
        if (!std::isfinite(**scale) || **scale <= 0.0) {
            return std::unexpected(argumentError("scale must be a positive number"));
        }
        request.scale = **scale;
    }
    if (snap->has_value()) {
        if (!std::isfinite(**snap) || **snap < 0.0) return std::unexpected(argumentError("snap_grid must not be negative"));
        request.snapGrid = **snap;
    }
    request.count = count->value_or(1);
    if (request.count == 0) return std::unexpected(argumentError("count must be at least 1"));
    if (request.count > k_max_placements) {
        return std::unexpected(Error{"AssetPlacementTooMany",
                                     "count " + std::to_string(request.count) +
                                         " exceeds 200 placements in one undo step; split the request"});
    }
    request.persistent = persistent->value_or(false);
    request.dryRun = dryRun->value_or(false);
    return request;
}

Result<nlohmann::json> browseCategories(const AssetCatalog& catalog, const nlohmann::json& arguments) {
    const auto plugin = readText(argumentAt(arguments, 0), "plugin");
    if (!plugin) return std::unexpected(plugin.error());
    AssetFilter filter;
    filter.plugin = *plugin;

    nlohmann::json groups = nlohmann::json::array();
    std::size_t total = 0;
    for (const auto& group : catalog.groupCounts(filter)) {
        groups.push_back({{"name", group.name}, {"count", group.count}});
        total += group.count;
    }
    nlohmann::json plugins = nlohmann::json::array();
    for (const auto& entry : catalog.pluginCounts(AssetFilter{})) {
        plugins.push_back({{"name", entry.name}, {"count", entry.count}});
    }
    const nlohmann::json document = {
        {"plugin", *plugin},
        {"total", total},
        {"groups", std::move(groups)},
        {"plugins", std::move(plugins)},
    };
    return textResult(document);
}

Result<nlohmann::json> browseFolder(const AssetCatalog& catalog, const nlohmann::json& arguments) {
    const auto path = readText(argumentAt(arguments, 0), "path");
    if (!path) return std::unexpected(path.error());
    const auto group = readText(argumentAt(arguments, 1), "group");
    if (!group) return std::unexpected(group.error());
    const auto depth = readCount(argumentAt(arguments, 2), "depth");
    if (!depth) return std::unexpected(depth.error());
    const auto canonical = canonicalGroup(*group);
    if (!canonical) return std::unexpected(canonical.error());

    AssetFilter filter;
    filter.group = *canonical;
    const auto levels = std::clamp(depth->value_or(k_default_depth), std::size_t{1}, k_max_depth);
    const auto browse = catalog.browseFolder(filter, *path, levels, k_folder_samples);

    nlohmann::json folders = nlohmann::json::array();
    for (const auto& folder : browse.folders) {
        nlohmann::json samples = nlohmann::json::array();
        for (const auto index : folder.samples) samples.push_back(rowJson(catalog.rows()[index]));
        folders.push_back({
            {"name", folder.name},
            {"path", folder.path},
            {"count", folder.count},
            {"samples", std::move(samples)},
        });
    }
    const nlohmann::json document = {
        {"path", *path},
        {"group", *canonical},
        {"total", browse.total},
        {"direct", browse.direct},
        {"folders", std::move(folders)},
    };
    return textResult(document);
}

Result<TextMatch> parseMatch(const std::string& text) {
    if (text.empty() || text == "contains") return TextMatch::contains;
    if (text == "startsWith") return TextMatch::starts_with;
    if (text == "exact") return TextMatch::exact;
    return std::unexpected(argumentError("Unknown match mode: " + text));
}

Result<TextField> parseField(const std::string& text) {
    if (text.empty() || text == "any") return TextField::any;
    if (text == "editorId") return TextField::editorId;
    if (text == "name") return TextField::name;
    if (text == "formKey") return TextField::formKey;
    if (text == "modelPath") return TextField::modelPath;
    return std::unexpected(argumentError("Unknown search field: " + text));
}

Result<AssetSort> parseSort(const std::string& text) {
    if (text.empty() || text == "editorId") return AssetSort::editorId;
    if (text == "name") return AssetSort::name;
    if (text == "type") return AssetSort::type;
    if (text == "modelPath") return AssetSort::modelPath;
    if (text == "plugin") return AssetSort::plugin;
    if (text == "formKey") return AssetSort::formKey;
    return std::unexpected(argumentError("Unknown sort: " + text));
}

std::string sortName(AssetSort sort) {
    switch (sort) {
        case AssetSort::name: return "name";
        case AssetSort::type: return "type";
        case AssetSort::modelPath: return "modelPath";
        case AssetSort::plugin: return "plugin";
        case AssetSort::formKey: return "formKey";
        case AssetSort::editorId: break;
    }
    return "editorId";
}

Result<nlohmann::json> searchAssets(const AssetCatalog& catalog, const nlohmann::json& arguments) {
    const auto text = readText(argumentAt(arguments, 0), "text");
    if (!text) return std::unexpected(text.error());
    const auto group = readText(argumentAt(arguments, 1), "group");
    if (!group) return std::unexpected(group.error());
    const auto folder = readText(argumentAt(arguments, 2), "folder");
    if (!folder) return std::unexpected(folder.error());
    const auto plugin = readText(argumentAt(arguments, 3), "plugin");
    if (!plugin) return std::unexpected(plugin.error());
    const auto match = readText(argumentAt(arguments, 4), "match");
    if (!match) return std::unexpected(match.error());
    const auto field = readText(argumentAt(arguments, 5), "field");
    if (!field) return std::unexpected(field.error());
    const auto hasModel = readBool(argumentAt(arguments, 6), "has_model");
    if (!hasModel) return std::unexpected(hasModel.error());
    const auto sort = readText(argumentAt(arguments, 7), "sort");
    if (!sort) return std::unexpected(sort.error());
    const auto limit = readCount(argumentAt(arguments, 8), "limit");
    if (!limit) return std::unexpected(limit.error());
    const auto offset = readCount(argumentAt(arguments, 9), "offset");
    if (!offset) return std::unexpected(offset.error());

    const auto canonical = canonicalGroup(*group);
    if (!canonical) return std::unexpected(canonical.error());
    const auto matchMode = parseMatch(*match);
    if (!matchMode) return std::unexpected(matchMode.error());
    const auto fieldMode = parseField(*field);
    if (!fieldMode) return std::unexpected(fieldMode.error());
    const auto sortMode = parseSort(*sort);
    if (!sortMode) return std::unexpected(sortMode.error());

    AssetSearch query;
    query.filter.group = *canonical;
    query.filter.folder = *folder;
    query.filter.plugin = *plugin;
    query.text = *text;
    query.match = *matchMode;
    query.field = *fieldMode;
    query.hasModel = *hasModel;
    query.sort = *sortMode;
    query.limit = std::clamp(limit->value_or(k_default_limit), std::size_t{1}, k_max_limit);
    query.offset = offset->value_or(0);

    const auto page = catalog.search(query);
    nlohmann::json items = nlohmann::json::array();
    for (const auto index : page.indices) items.push_back(rowJson(catalog.rows()[index]));
    const nlohmann::json document = {
        {"total", page.total},
        {"offset", query.offset},
        {"limit", query.limit},
        {"sort", sortName(*sortMode)},
        {"nextOffset", page.nextOffset ? nlohmann::json(*page.nextOffset) : nlohmann::json(nullptr)},
        {"items", std::move(items)},
    };
    return textResult(document);
}

Result<nlohmann::json> getAssetDetails(const AssetCatalog& catalog, const fo4::env::Environment& env,
                                       const nlohmann::json& arguments) {
    const auto formKey = readText(argumentAt(arguments, 0), "base_form_key");
    if (!formKey) return std::unexpected(formKey.error());
    const auto editorId = readText(argumentAt(arguments, 1), "editor_id");
    if (!editorId) return std::unexpected(editorId.error());
    if (formKey->empty() == editorId->empty()) {
        return std::unexpected(argumentError("Pass exactly one of base_form_key or editor_id"));
    }
    const auto row = formKey->empty() ? resolveEditorId(catalog, *editorId) : resolveFormKey(catalog, *formKey);
    if (!row) return std::unexpected(row.error());

    nlohmann::json document = rowJson(**row);
    nlohmann::json bounds = nullptr;
    if (const auto* ref = env.winner(fo4::FormKey::parse((*row)->formKey)); ref != nullptr) {
        const auto meta = env.metadata(*ref);
        if (meta.bounds) {
            const auto& box = *meta.bounds;
            bounds = nlohmann::json{
                {"min", vecJson(Vec3{static_cast<double>(box.min[0]), static_cast<double>(box.min[1]),
                                     static_cast<double>(box.min[2])})},
                {"max", vecJson(Vec3{static_cast<double>(box.max[0]), static_cast<double>(box.max[1]),
                                     static_cast<double>(box.max[2])})},
            };
        }
    }
    document["bounds"] = std::move(bounds);
    return textResult(document);
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::assetOperation(std::string_view method, const nlohmann::json& arguments) {
    if (method == "PlaceAsset") return placeAsset(arguments);
    if (method != "BrowseCategories" && method != "BrowseFolder" && method != "SearchAssets" &&
        method != "GetAssetDetails") {
        return std::unexpected(Error{"AssetMethodUnknown", "Unknown asset operation: " + std::string(method)});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto catalog = assetCatalog();
        if (method == "BrowseCategories") return browseCategories(*catalog, arguments);
        if (method == "BrowseFolder") return browseFolder(*catalog, arguments);
        if (method == "SearchAssets") return searchAssets(*catalog, arguments);
        const auto env = borrowedEnvironment();
        return getAssetDetails(*catalog, *env, arguments);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AssetOperationFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::placeAsset(const nlohmann::json& arguments) {
    const auto request = parsePlaceAsset(arguments);
    if (!request) return std::unexpected(request.error());

    try {
        std::shared_ptr<const AssetCatalog> catalog;
        {
            OpLock lock(*this, OpLock::Mode::Read);
            catalog = assetCatalog();
        }
        const auto row = resolveAsset(*catalog, request->asset);
        if (!row) return std::unexpected(row.error());

        Vec3 origin;
        if (request->position) {
            origin = *request->position;
        } else {
            const auto listed = getPlacedReferences(nlohmann::json::array({request->cellId}));
            if (!listed) return std::unexpected(listed.error());
            const auto document = nlohmann::json::parse(listed->get<std::string>());
            if (document.contains("error")) {
                return std::unexpected(Error{"PlacedCellNotFound", document.at("error").get<std::string>()});
            }
            bool found = false;
            for (const auto& reference : document.at("references")) {
                if (reference.at("formKey").get<std::string>() != request->nearKey) continue;
                const auto& position = reference.at("position");
                origin = Vec3{position.at("x").get<double>() + request->offset.x,
                              position.at("y").get<double>() + request->offset.y,
                              position.at("z").get<double>() + request->offset.z};
                found = true;
                break;
            }
            if (!found) {
                return std::unexpected(Error{"AssetNearReferenceMissing",
                                             "near_reference_key is not a reference in the cell: " +
                                                 request->nearKey});
            }
        }

        nlohmann::json placements = nlohmann::json::array();
        for (std::size_t index = 0; index < request->count; ++index) {
            const auto step = static_cast<double>(index);
            Vec3 point{origin.x + request->spacing.x * step,
                       origin.y + request->spacing.y * step,
                       origin.z + request->spacing.z * step};
            if (request->snapGrid > 0.0) {
                const auto snap = [&request](double value) {
                    return std::round(value / request->snapGrid) * request->snapGrid;
                };
                point = Vec3{snap(point.x), snap(point.y), snap(point.z)};
            }
            nlohmann::json item{
                {"baseFormKey", (*row)->formKey},
                {"transform", {{"x", point.x}, {"y", point.y}, {"z", point.z},
                               {"rx", request->rotation.x}, {"ry", request->rotation.y},
                               {"rz", request->rotation.z}}},
            };
            if (request->scale) item["scale"] = *request->scale;
            placements.push_back(std::move(item));
        }

        if (request->dryRun) {
            const nlohmann::json document = {
                {"dryRun", true},
                {"plugin", request->patchPlugin},
                {"cellId", request->cellId},
                {"asset", rowJson(**row)},
                {"placements", std::move(placements)},
            };
            return textResult(document);
        }

        const nlohmann::json options{{"persistent", request->persistent}};
        const auto placed = placeReferences(
            nlohmann::json::array({request->patchPlugin, request->cellId, placements, options}));
        if (!placed) return std::unexpected(placed.error());
        nlohmann::json document = nlohmann::json::parse(placed->get<std::string>());
        document["asset"] = rowJson(**row);
        document["count"] = request->count;
        return textResult(document);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AssetPlacementFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
