#include <fo4ide/app/LayoutTarget.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

namespace fo4ide::app {
namespace {

constexpr double kButtThreshold = 16.0;
constexpr double kDefaultTolerance = 1.0;
constexpr std::size_t kMaxItems = 200;

std::string lowerText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalisedPath(std::string value) {
    std::replace(value.begin(), value.end(), '/', '\\');
    value = lowerText(value);
    while (!value.empty() && value.front() == '\\') value.erase(value.begin());
    while (!value.empty() && value.back() == '\\') value.pop_back();
    constexpr std::string_view meshes = "meshes\\";
    if (value.rfind(meshes, 0) == 0) value.erase(0, meshes.size());
    return value;
}

std::string pluginOf(const std::string& key) {
    const auto colon = key.find(':');
    return lowerText(colon == std::string::npos ? std::string{} : key.substr(colon + 1));
}

const nlohmann::json* childOf(const nlohmann::json& object, std::string_view name) {
    if (!object.is_object()) return nullptr;
    const auto it = object.find(std::string(name));
    return it == object.end() ? nullptr : &*it;
}

std::string stringField(const nlohmann::json& object, std::string_view name) {
    const auto* value = childOf(object, name);
    return value != nullptr && value->is_string() ? value->get<std::string>() : std::string{};
}

bool flagField(const nlohmann::json& object, std::string_view name) {
    const auto* value = childOf(object, name);
    return value != nullptr && value->is_boolean() && value->get<bool>();
}

double numberField(const nlohmann::json& object, std::string_view name, double fallback) {
    const auto* value = childOf(object, name);
    return value != nullptr && value->is_number() ? value->get<double>() : fallback;
}

std::optional<layout::Vec3> vec3Of(const nlohmann::json* value) {
    if (value == nullptr) return std::nullopt;
    if (value->is_array() && value->size() == 3 && (*value)[0].is_number() && (*value)[1].is_number() &&
        (*value)[2].is_number()) {
        return layout::Vec3{(*value)[0].get<double>(), (*value)[1].get<double>(), (*value)[2].get<double>()};
    }
    if (value->is_object()) {
        return layout::Vec3{numberField(*value, "x", 0.0), numberField(*value, "y", 0.0),
                            numberField(*value, "z", 0.0)};
    }
    return std::nullopt;
}

nlohmann::json vec3Json(layout::Vec3 v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

nlohmann::json boundsJson(const layout::Bounds& bounds) {
    return nlohmann::json{{"min", vec3Json(bounds.min)}, {"max", vec3Json(bounds.max)},
                          {"size", vec3Json(bounds.size())}};
}

const nlohmann::json* argumentAt(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || args[index].is_null()) return nullptr;
    return &args[index];
}

std::optional<std::string> optionalString(const nlohmann::json& args, std::size_t index) {
    const auto* value = argumentAt(args, index);
    if (value == nullptr || !value->is_string()) return std::nullopt;
    return value->get<std::string>();
}

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    return optionalString(args, index).value_or(std::string{});
}

std::optional<double> numberArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argumentAt(args, index);
    if (value == nullptr || !value->is_number()) return std::nullopt;
    return value->get<double>();
}

std::optional<std::vector<std::string>> stringListArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argumentAt(args, index);
    if (value == nullptr || !value->is_array()) return std::nullopt;
    std::vector<std::string> out;
    for (const auto& entry : *value) {
        if (entry.is_string()) out.push_back(entry.get<std::string>());
    }
    return out;
}

Result<nlohmann::json> documentOf(const nlohmann::json& value) {
    nlohmann::json doc = value.is_string() ? nlohmann::json::parse(value.get<std::string>(), nullptr, false) : value;
    if (doc.is_discarded() || !doc.is_object()) {
        return std::unexpected(Error{"LayoutBackendBadResponse", "Plugin backend returned an unreadable document"});
    }
    if (const auto* error = childOf(doc, "error"); error != nullptr && error->is_string()) {
        return std::unexpected(Error{"LayoutBackendRefused", error->get<std::string>()});
    }
    return doc;
}

Result<layout::Bounds> boundsOfGeometry(const nlohmann::json& geometry) {
    const auto* shapes = childOf(geometry, "shapes");
    if (shapes == nullptr || !shapes->is_array()) {
        return std::unexpected(Error{"GeometryUnreadable", "Geometry has no shapes array"});
    }
    layout::Bounds out = layout::emptyBounds();
    for (const auto& shape : *shapes) {
        const auto* verts = childOf(shape, "verts");
        if (verts == nullptr || !verts->is_array()) continue;
        std::vector<double> flat;
        flat.reserve(verts->size());
        for (const auto& value : *verts) {
            if (!value.is_number()) return std::unexpected(Error{"GeometryUnreadable", "Vertex is not a number"});
            flat.push_back(value.get<double>());
        }
        const auto* world = childOf(shape, "world");
        if (world != nullptr && world->is_array() && world->size() == 16) {
            layout::Affine affine;
            for (std::size_t i = 0; i < 16; ++i) {
                if (!(*world)[i].is_number()) return std::unexpected(Error{"GeometryUnreadable", "World matrix entry is not a number"});
            }
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t col = 0; col < 3; ++col) affine.rotation.m[row * 3 + col] = (*world)[col * 4 + row].get<double>();
            }
            affine.translation = layout::Vec3{(*world)[12].get<double>(), (*world)[13].get<double>(), (*world)[14].get<double>()};
            for (std::size_t i = 0; i + 2 < flat.size(); i += 3) {
                out = layout::expandBounds(out, layout::apply(affine, layout::Vec3{flat[i], flat[i + 1], flat[i + 2]}));
            }
        } else {
            if (flat.size() % 3 != 0) return std::unexpected(Error{"GeometryUnreadable", "Vertex list is not xyz triples"});
            out = layout::mergeBounds(out, layout::boundsOfPoints(flat));
        }
    }
    if (!out.valid()) return std::unexpected(Error{"GeometryEmpty", "Geometry has no vertices"});
    return out;
}

const BaseModelRow* findRow(const std::vector<BaseModelRow>& rows, const std::string& key) {
    const auto wanted = lowerText(key);
    for (const auto& row : rows) {
        if (lowerText(row.key) == wanted) return &row;
    }
    return nullptr;
}

bool pathUnder(const std::string& path, const std::string& dir) {
    return dir.empty() || path.rfind(dir + '\\', 0) == 0;
}

nlohmann::json skippedEntry(const std::string& key, const std::string& reason) {
    return nlohmann::json{{"base_form_key", key}, {"error", reason}};
}

std::optional<std::string> kitError(const nlohmann::json& kit) {
    for (const char* role : {"floor", "wall", "corner", "ceiling", "door"}) {
        const auto* piece = childOf(kit, role);
        if (piece == nullptr || piece->is_null()) {
            if (std::string_view(role) == "floor" || std::string_view(role) == "wall") {
                return std::string("Room kit needs a ") + role + " piece";
            }
            continue;
        }
        if (stringField(*piece, "baseFormKey").empty() || !vec3Of(childOf(*piece, "min")) ||
            !vec3Of(childOf(*piece, "max"))) {
            return std::string("Kit piece ") + role + " needs baseFormKey and min/max bounds";
        }
    }
    return std::nullopt;
}
}

GeometryProvider nifGeometryProvider(assets::NifService& nif, assets::TextureResolver& textures) {
    return [&nif, &textures](const std::string& modelPath) -> Result<nlohmann::json> {
        const auto resolved = textures.resolveNif(modelPath);
        if (!resolved) return std::unexpected(Error{"ModelNotFound", "Not found (loose or in any indexed archive)."});
        const auto geometry = nif.geometry(*resolved);
        if (!geometry) return std::unexpected(geometry.error());
        auto doc = nlohmann::json::parse(*geometry, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) return std::unexpected(Error{"GeometryUnreadable", *geometry});
        return doc;
    };
}

LayoutTarget::LayoutTarget(plugin::IPluginBackend& backend, GeometryProvider geometry)
    : backend_(backend), geometry_(std::move(geometry)) {}

Result<nlohmann::json> LayoutTarget::invoke(std::string_view method, const nlohmann::json& args) {
    if (method == "GetModelBounds") return getModelBounds(args);
    if (method == "DescribeKit") return describeKit(args);
    if (method == "SnapTransform") return snapTransform(args);
    if (method == "CheckOverlaps") return checkOverlaps(args);
    if (method == "BuildRoom") return buildRoom(args);
    if (method == "AlignReferences") return alignReferences(args);
    return std::unexpected(Error{"UnknownMethod", "Unknown layout RPC method: " + std::string(method)});
}

Result<layout::Bounds> LayoutTarget::localBounds(const std::string& modelPath) {
    if (modelPath.empty()) return std::unexpected(Error{"ModelMissing", "Piece has no model path"});
    const auto key = normalisedPath(modelPath);
    {
        std::lock_guard lock(mutex_);
        const auto cached = boundsCache_.find(key);
        if (cached != boundsCache_.end()) return cached->second;
    }
    auto geometry = geometry_(modelPath);
    if (!geometry) return std::unexpected(geometry.error());
    auto bounds = boundsOfGeometry(*geometry);
    if (!bounds) return std::unexpected(bounds.error());
    std::lock_guard lock(mutex_);
    boundsCache_[key] = *bounds;
    return *bounds;
}

Result<std::vector<BaseModelRow>> LayoutTarget::baseRows(const nlohmann::json& filter) {
    auto response = backend_.listModelBases(nlohmann::json::array({filter}));
    if (!response) return std::unexpected(response.error());
    auto doc = documentOf(*response);
    if (!doc) return std::unexpected(doc.error());
    std::vector<BaseModelRow> rows;
    const auto* list = childOf(*doc, "rows");
    if (list == nullptr || !list->is_array()) return rows;
    for (const auto& row : *list) {
        if (flagField(row, "deleted")) continue;
        BaseModelRow entry{stringField(row, "key"), stringField(row, "editorId"), stringField(row, "modelPath")};
        if (entry.modelPath.empty()) continue;
        rows.push_back(std::move(entry));
    }
    return rows;
}

Result<nlohmann::json> LayoutTarget::getModelBounds(const nlohmann::json& args) {
    const auto models = stringListArgument(args, 0);
    const auto keys = stringListArgument(args, 1);
    if (models.has_value() == keys.has_value()) {
        return std::unexpected(Error{"LayoutArgumentsInvalid",
                                     "get_model_bounds needs exactly one of model_paths or base_form_keys"});
    }
    if (keys && keys->empty()) return std::unexpected(Error{"LayoutArgumentsInvalid", "get_model_bounds base_form_keys must not be empty"});
    nlohmann::json out = nlohmann::json::array();
    if (models) {
        for (const auto& path : *models) {
            nlohmann::json entry{{"model_path", path}};
            auto bounds = localBounds(path);
            if (bounds) {
                entry["bounds"] = boundsJson(*bounds);
            } else {
                entry["error"] = bounds.error().message;
            }
            out.push_back(std::move(entry));
        }
    } else {
        auto rows = baseRows(nlohmann::json{{"keys", *keys}});
        if (!rows) return std::unexpected(rows.error());
        for (const auto& key : *keys) {
            const auto* row = findRow(*rows, key);
            if (row == nullptr) {
                out.push_back(skippedEntry(key, "No base object with a model under this key"));
                continue;
            }
            nlohmann::json entry{{"base_form_key", key}, {"editor_id", row->editorId}, {"model_path", row->modelPath}};
            auto bounds = localBounds(row->modelPath);
            if (bounds) {
                entry["bounds"] = boundsJson(*bounds);
            } else {
                entry["error"] = bounds.error().message;
            }
            out.push_back(std::move(entry));
        }
    }
    return nlohmann::json{{"models", std::move(out)}};
}

Result<nlohmann::json> LayoutTarget::describeKit(const nlohmann::json& args) {
    const auto namePrefix = optionalString(args, 0);
    const auto plugin = optionalString(args, 1);
    const auto modelDir = optionalString(args, 2);
    if (!namePrefix && !plugin && !modelDir) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "describe_kit needs name_prefix, plugin or model_dir"});
    }
    nlohmann::json filter = nlohmann::json::object();
    if (namePrefix) filter["name_prefix"] = *namePrefix;
    if (plugin) filter["plugin"] = *plugin;
    if (modelDir) filter["model_dir"] = *modelDir;
    auto rows = baseRows(filter);
    if (!rows) return std::unexpected(rows.error());

    const auto prefix = namePrefix ? lowerText(*namePrefix) : std::string{};
    const auto pluginName = plugin ? lowerText(*plugin) : std::string{};
    const auto dir = modelDir ? normalisedPath(*modelDir) : std::string{};

    nlohmann::json pieces = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    nlohmann::json categories = nlohmann::json::object();
    std::vector<layout::Bounds> bounds;
    for (const auto& row : *rows) {
        if (namePrefix && lowerText(row.editorId).rfind(prefix, 0) != 0) continue;
        if (plugin && pluginOf(row.key) != pluginName) continue;
        if (modelDir && !pathUnder(normalisedPath(row.modelPath), dir)) continue;
        auto local = localBounds(row.modelPath);
        if (!local) {
            skipped.push_back(skippedEntry(row.key, local.error().message));
            continue;
        }
        auto category = layout::categorizeName(row.editorId);
        if (category == layout::PieceCategory::unknown) {
            category = layout::categorizeName(std::filesystem::path(row.modelPath).stem().string());
        }
        const auto name = std::string(layout::categoryName(category));
        categories[name] = categories.value(name, std::size_t{0}) + 1;
        bounds.push_back(*local);
        pieces.push_back(nlohmann::json{{"base_form_key", row.key}, {"editor_id", row.editorId},
                                        {"model_path", row.modelPath}, {"category", name},
                                        {"bounds", boundsJson(*local)}});
    }
    if (pieces.empty()) {
        return std::unexpected(Error{"KitEmpty", "No base objects with loadable models matched this kit query"});
    }
    const auto grid = layout::detectGrid(bounds);
    return nlohmann::json{
        {"pieces", std::move(pieces)},
        {"count", bounds.size()},
        {"categories", std::move(categories)},
        {"skipped", std::move(skipped)},
        {"grid", nlohmann::json{{"grid", grid.grid}, {"votes", grid.votes}, {"pieces", grid.pieces}}}};
}

Result<nlohmann::json> LayoutTarget::snapTransform(const nlohmann::json& args) {
    const auto key = stringArgument(args, 0);
    const auto nearPoint = vec3Of(argumentAt(args, 1));
    const auto grid = numberArgument(args, 2).value_or(0.0);
    if (key.empty() || !nearPoint) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "snap_transform needs base_form_key and near{x,y,z}"});
    }
    if (!std::isfinite(grid) || grid < 0.0) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "grid must be a finite number >= 0"});
    }
    auto rows = baseRows(nlohmann::json{{"keys", nlohmann::json::array({key})}});
    if (!rows) return std::unexpected(rows.error());
    const auto* row = findRow(*rows, key);
    if (row == nullptr) return std::unexpected(Error{"BaseNotFound", "No base object with a model: " + key});
    auto local = localBounds(row->modelPath);
    if (!local) return std::unexpected(local.error());

    layout::Vec3 pivot = *nearPoint;
    if (grid > 0.0) pivot = layout::snapPivotToGrid(pivot, *local, grid);

    std::vector<layout::Bounds> neighbourBounds;
    nlohmann::json used = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    const auto neighbours = stringListArgument(args, 3).value_or(std::vector<std::string>{});
    for (const auto& neighbour : neighbours) {
        auto details = backend_.getReferenceDetails(nlohmann::json::array({neighbour}));
        if (!details) {
            skipped.push_back(skippedEntry(neighbour, details.error().message));
            continue;
        }
        auto doc = documentOf(*details);
        if (!doc) {
            skipped.push_back(skippedEntry(neighbour, doc.error().message));
            continue;
        }
        const auto* base = childOf(*doc, "base");
        const auto modelPath = base != nullptr ? stringField(*base, "model") : std::string{};
        const auto* transform = childOf(*doc, "transform");
        const auto position = transform != nullptr ? vec3Of(childOf(*transform, "position")) : std::nullopt;
        if (modelPath.empty() || !position) {
            skipped.push_back(skippedEntry(neighbour, "Neighbour has no model or position"));
            continue;
        }
        const auto rotation = vec3Of(childOf(*transform, "rotation")).value_or(layout::Vec3{});
        const auto scale = numberField(*transform, "scale", 1.0);
        auto neighbourLocal = localBounds(modelPath);
        if (!neighbourLocal) {
            skipped.push_back(skippedEntry(neighbour, neighbourLocal.error().message));
            continue;
        }
        neighbourBounds.push_back(layout::transformBounds(*neighbourLocal, layout::poseAffine(*position, rotation, scale)));
        used.push_back(neighbour);
    }
    pivot = layout::buttPivotToNeighbours(pivot, *local, neighbourBounds, kButtThreshold);

    const auto placed = layout::transformBounds(*local, layout::poseAffine(pivot, layout::Vec3{}, 1.0));
    return nlohmann::json{
        {"base_form_key", key},
        {"model_path", row->modelPath},
        {"transform", nlohmann::json{{"x", pivot.x}, {"y", pivot.y}, {"z", pivot.z},
                                     {"rx", 0.0}, {"ry", 0.0}, {"rz", 0.0}, {"scale", 1.0}}},
        {"snapped", grid > 0.0},
        {"neighbours_used", std::move(used)},
        {"neighbours_skipped", std::move(skipped)},
        {"bounds", boundsJson(placed)}};
}

Result<nlohmann::json> LayoutTarget::checkOverlaps(const nlohmann::json& args) {
    const auto cellId = stringArgument(args, 0);
    const auto tolerance = numberArgument(args, 1).value_or(kDefaultTolerance);
    if (cellId.empty()) return std::unexpected(Error{"LayoutArgumentsInvalid", "check_overlaps needs cell_id"});
    if (!std::isfinite(tolerance) || tolerance < 0.0) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "tolerance must be a finite number >= 0"});
    }
    auto response = backend_.getPlacedReferences(nlohmann::json::array({cellId}));
    if (!response) return std::unexpected(response.error());
    auto doc = documentOf(*response);
    if (!doc) return std::unexpected(doc.error());
    const auto* references = childOf(*doc, "references");
    if (references == nullptr || !references->is_array()) {
        return std::unexpected(Error{"CellReadFailed", "Cell has no placed-reference list"});
    }

    std::vector<layout::Bounds> items;
    std::vector<std::string> itemKeys;
    nlohmann::json skipped = nlohmann::json::array();
    for (const auto& ref : *references) {
        const auto formKey = stringField(ref, "formKey");
        if (flagField(ref, "isMarker")) {
            skipped.push_back(nlohmann::json{{"form_key", formKey}, {"reason", "marker"}});
            continue;
        }
        const auto modelPath = stringField(ref, "modelPath");
        auto local = localBounds(modelPath);
        if (!local) {
            skipped.push_back(nlohmann::json{{"form_key", formKey}, {"reason", local.error().message}});
            continue;
        }
        const auto position = vec3Of(childOf(ref, "position")).value_or(layout::Vec3{});
        const auto rotation = vec3Of(childOf(ref, "rotation")).value_or(layout::Vec3{});
        const auto scale = numberField(ref, "scale", 1.0);
        items.push_back(layout::transformBounds(*local, layout::poseAffine(position, rotation, scale)));
        itemKeys.push_back(formKey);
    }

    nlohmann::json overlaps = nlohmann::json::array();
    for (const auto& pair : layout::findOverlaps(items, tolerance)) {
        overlaps.push_back(nlohmann::json{{"a", itemKeys[pair.a]}, {"b", itemKeys[pair.b]}, {"depth", vec3Json(pair.depth)}});
    }
    return nlohmann::json{
        {"cell_id", cellId},
        {"tolerance", tolerance},
        {"checked", items.size()},
        {"overlap_count", overlaps.size()},
        {"overlaps", std::move(overlaps)},
        {"skipped", std::move(skipped)}};
}

Result<nlohmann::json> LayoutTarget::buildRoom(const nlohmann::json& args) {
    const auto patch = stringArgument(args, 0);
    const auto cell = stringArgument(args, 1);
    const auto* kit = argumentAt(args, 2);
    const auto* size = argumentAt(args, 3);
    const auto origin = vec3Of(argumentAt(args, 4));
    const auto grid = numberArgument(args, 5).value_or(0.0);
    const auto* dryRun = argumentAt(args, 6);
    if (patch.empty() || cell.empty() || kit == nullptr || !kit->is_object() || size == nullptr || !size->is_object()) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "build_room needs patch_plugin, cell_id, kit and size"});
    }
    if (const auto problem = kitError(*kit)) return std::unexpected(Error{"LayoutArgumentsInvalid", *problem});
    if (!origin) return std::unexpected(Error{"LayoutArgumentsInvalid", "build_room origin needs numeric x, y and z"});
    if (!std::isfinite(grid) || grid < 0.0) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "grid must be a finite number >= 0"});
    }
    layout::Vec3 snapped = *origin;
    if (grid > 0.0) {
        snapped.x = std::round(snapped.x / grid) * grid;
        snapped.y = std::round(snapped.y / grid) * grid;
    }
    const bool dry = dryRun != nullptr && dryRun->is_boolean() && dryRun->get<bool>();
    const nlohmann::json snappedOrigin{{"x", snapped.x}, {"y", snapped.y}, {"z", snapped.z}};
    return backend_.buildRoom(nlohmann::json::array({patch, cell, *kit, *size, snappedOrigin, dry}));
}

Result<nlohmann::json> LayoutTarget::alignReferences(const nlohmann::json& args) {
    const auto patch = stringArgument(args, 0);
    const auto keys = stringListArgument(args, 1);
    const auto mode = stringArgument(args, 2);
    const auto grid = numberArgument(args, 3).value_or(0.0);
    if (patch.empty() || !keys || keys->empty()) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "align_references needs patch_plugin and keys"});
    }
    if (keys->size() > kMaxItems) {
        return std::unexpected(Error{"LayoutItemsInvalid", "align_references takes at most 200 keys per call"});
    }
    if (!std::isfinite(grid) || grid < 0.0) {
        return std::unexpected(Error{"LayoutArgumentsInvalid", "grid must be a finite number >= 0"});
    }
    layout::AlignRequest request;
    request.grid = grid;
    if (mode == "floor") {
        request.mode = layout::AlignMode::floor;
    } else if (mode == "grid") {
        request.mode = layout::AlignMode::grid;
    } else if (mode == "distribute_x" || mode == "distribute_y") {
        request.mode = layout::AlignMode::distribute;
        request.axis = mode == "distribute_x" ? 0 : 1;
    } else {
        return std::unexpected(Error{"LayoutArgumentsInvalid",
                                     "mode must be floor, grid, distribute_x or distribute_y"});
    }

    struct Placed {
        std::string key;
        layout::Vec3 position;
        layout::Vec3 rotation;
        layout::Bounds world;
    };
    std::vector<Placed> placed;
    std::vector<layout::Bounds> worlds;
    for (const auto& key : *keys) {
        auto details = backend_.getReferenceDetails(nlohmann::json::array({key}));
        if (!details) return std::unexpected(Error{"AlignReadFailed", key + ": " + details.error().message});
        auto doc = documentOf(*details);
        if (!doc) return std::unexpected(Error{"AlignReadFailed", key + ": " + doc.error().message});
        const auto* base = childOf(*doc, "base");
        const auto model = base != nullptr ? stringField(*base, "model") : std::string{};
        const auto* transform = childOf(*doc, "transform");
        const auto position = transform != nullptr ? vec3Of(childOf(*transform, "position")) : std::nullopt;
        if (model.empty() || !position) {
            return std::unexpected(Error{"AlignReferenceUnreadable", key + " has no model or position"});
        }
        const auto rotation = vec3Of(childOf(*transform, "rotation")).value_or(layout::Vec3{});
        const auto scale = numberField(*transform, "scale", 1.0);
        auto local = localBounds(model);
        if (!local) return std::unexpected(Error{"AlignModelUnreadable", key + ": " + local.error().message});
        const auto world = layout::transformBounds(*local, layout::poseAffine(*position, rotation, scale));
        placed.push_back(Placed{key, *position, rotation, world});
        worlds.push_back(world);
    }

    std::string error;
    const auto deltas = layout::alignDeltas(worlds, request, error);
    if (!deltas) return std::unexpected(Error{"LayoutAlignInvalid", error});

    nlohmann::json items = nlohmann::json::array();
    nlohmann::json moves = nlohmann::json::array();
    for (std::size_t i = 0; i < placed.size(); ++i) {
        const auto target = placed[i].position + (*deltas)[i];
        const auto& rotation = placed[i].rotation;
        const nlohmann::json transform{{"x", target.x}, {"y", target.y}, {"z", target.z},
                                       {"rx", rotation.x}, {"ry", rotation.y}, {"rz", rotation.z}};
        items.push_back(nlohmann::json{{"formKey", placed[i].key}, {"transform", transform}});
        moves.push_back(nlohmann::json{{"formKey", placed[i].key}, {"delta", vec3Json((*deltas)[i])}});
    }
    auto written = backend_.editPlacedReferences(nlohmann::json::array({patch, items}));
    if (!written) return std::unexpected(written.error());
    auto doc = documentOf(*written);
    if (!doc) return std::unexpected(doc.error());
    (*doc)["mode"] = mode;
    (*doc)["moves"] = std::move(moves);
    return *doc;
}

}
