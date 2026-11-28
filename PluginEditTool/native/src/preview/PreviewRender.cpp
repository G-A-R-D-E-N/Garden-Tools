#include <fo4ide/preview/PreviewRender.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <span>
#include <utility>

#include <fo4ide/core/Base64.hpp>
#include <fo4ide/preview/PreviewVolumes.hpp>
#include <fo4ide/textures/PngWriter.hpp>

namespace fo4ide::preview {
namespace {

using json = nlohmann::json;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultFovDegrees = 40.0;
constexpr double kMinFovDegrees = 10.0;
constexpr double kMaxFovDegrees = 120.0;
constexpr int kMinImageSide = 64;
constexpr int kMaxImageSide = 2048;
constexpr long long kMinReferenceBudget = 1;
constexpr long long kMaxReferenceBudget = 50000;
constexpr long long kDefaultReferenceBudget = 4000;
constexpr std::size_t kMaxTriangleBudget = 3000000;
constexpr std::size_t kMaxHighlightKeys = 64;
constexpr int kOccupancySide = 16;
constexpr int kGridLines = 16;
constexpr double kGridFraction = 0.75;
constexpr double kAxisFraction = 0.12;
constexpr double kMarkerHalfSize = 12.0;
constexpr double kFramePadding = 1.08;
constexpr double kPerspectiveFit = 1.15;
constexpr double kNearRadiusFactor = 1.5;
constexpr double kHeightBucket = 32.0;
constexpr double kFloorCosine = 0.7;
constexpr std::size_t kHeightBucketCount = 16;
constexpr std::size_t kLargeStructureCount = 25;
constexpr double kMaxUint32 = 4294967295.0;

const json kNullNode{};
const json kEmptyArray = json::array();

std::string lowered(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

const json& child(const json& node, std::string_view key) {
    if (!node.is_object()) return kNullNode;
    const auto it = node.find(std::string(key));
    return it == node.end() ? kNullNode : *it;
}

const json& arrayOf(const json& node, std::string_view key) {
    const auto& value = child(node, key);
    return value.is_array() ? value : kEmptyArray;
}

double numberOr(const json& node, std::string_view key, double fallback) {
    const auto& value = child(node, key);
    return value.is_number() ? value.get<double>() : fallback;
}

std::string stringOr(const json& node, std::string_view key) {
    const auto& value = child(node, key);
    return value.is_string() ? value.get<std::string>() : std::string{};
}

bool boolOr(const json& node, std::string_view key) {
    const auto& value = child(node, key);
    return value.is_boolean() && value.get<bool>();
}

double numberAt(const json& array, std::size_t index) {
    if (index >= array.size()) return 0.0;
    const auto& value = array[index];
    return value.is_number() ? value.get<double>() : 0.0;
}

std::uint32_t indexAt(const json& array, std::size_t index) {
    return static_cast<std::uint32_t>(std::clamp(numberAt(array, index), 0.0, kMaxUint32));
}

Vec3 readXyz(const json& node) {
    return {numberOr(node, "x", 0.0), numberOr(node, "y", 0.0), numberOr(node, "z", 0.0)};
}

Vec3 readFlatRotation(const json& node) {
    return {numberOr(node, "rx", 0.0), numberOr(node, "ry", 0.0), numberOr(node, "rz", 0.0)};
}

double readScale(double raw) {
    return raw == 0.0 ? 1.0 : raw;
}

json vecJson(Vec3 v) {
    return json::array({v.x, v.y, v.z});
}

std::optional<Vec3> parseVec3(const json& value) {
    if (!value.is_array() || value.size() != 3) return std::nullopt;
    for (const auto& item : value) {
        if (!item.is_number()) return std::nullopt;
    }
    return Vec3{numberAt(value, 0), numberAt(value, 1), numberAt(value, 2)};
}

Matrix3 eulerXyz(Vec3 r) {
    const double cx = std::cos(r.x);
    const double sx = std::sin(r.x);
    const double cy = std::cos(r.y);
    const double sy = std::sin(r.y);
    const double cz = std::cos(r.z);
    const double sz = std::sin(r.z);
    const Matrix3 rx{{{1.0, 0.0, 0.0}, {0.0, cx, -sx}, {0.0, sx, cx}}};
    const Matrix3 ry{{{cy, 0.0, sy}, {0.0, 1.0, 0.0}, {-sy, 0.0, cy}}};
    const Matrix3 rz{{{cz, -sz, 0.0}, {sz, cz, 0.0}, {0.0, 0.0, 1.0}}};
    Matrix3 product{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) {
                product[i][j] += rx[i][k] * ry[k][j];
            }
        }
    }
    Matrix3 out{};
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            for (std::size_t k = 0; k < 3; ++k) {
                out[i][j] += product[i][k] * rz[k][j];
            }
        }
    }
    return out;
}

bool knownPreset(std::string_view preset) {
    return preset == "front" || preset == "right" || preset == "top" || preset == "iso";
}

Vec3 presetViewDirection(std::string_view preset) {
    if (preset == "front") return {0.0, -1.0, 0.0};
    if (preset == "right") return {1.0, 0.0, 0.0};
    if (preset == "top") return {0.0, 0.0, 1.0};
    return normalized({0.6, -0.8, 0.7});
}

std::array<Vec3, 8> boxCorners(const Bounds& bounds) {
    const Vec3 lo = bounds.min;
    const Vec3 hi = bounds.max;
    return {{{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
             {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}}};
}

double niceStep(double raw) {
    if (raw <= 0.0) return 1.0;
    const double exponent = std::floor(std::log10(raw));
    const double base = std::pow(10.0, exponent);
    const double fraction = raw / base;
    const double nice = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0;
    return nice * base;
}

json boundsJson(const Bounds& bounds) {
    if (bounds.empty()) return nullptr;
    return {{"min", vecJson(bounds.min)}, {"max", vecJson(bounds.max)}, {"size", vecJson(bounds.size())}};
}

json cameraJson(const Camera& camera, std::string_view preset) {
    return {{"preset", std::string(preset)},
            {"orthographic", camera.orthographic},
            {"eye", vecJson(camera.eye)},
            {"target", vecJson(camera.target)},
            {"fovDegrees", camera.fovYDegrees},
            {"orthoHalfHeight", camera.orthoHalfHeight}};
}

json legendJson() {
    json rows = json::array();
    for (const auto& entry : recordTypeLegend()) {
        const auto hex = std::format("#{:02X}{:02X}{:02X}", entry.colour.r, entry.colour.g, entry.colour.b);
        rows.push_back({{"label", entry.label}, {"colour", hex}});
    }
    return rows;
}

Result<void> checkSize(const RenderOptions& options) {
    if (options.width < kMinImageSide || options.width > kMaxImageSide ||
        options.height < kMinImageSide || options.height > kMaxImageSide) {
        return std::unexpected(Error{"InvalidArgument", "image width and height must be from 64 to 2048"});
    }
    return {};
}

template <class Visit>
void forEachMeshTriangle(const ModelMesh& mesh, const Affine& placement, Visit&& visit) {
    for (const auto& shape : mesh.shapes) {
        const Affine world = shape.local ? compose(placement, *shape.local) : placement;
        for (const auto& tri : shape.tris) {
            if (tri[0] >= shape.verts.size() || tri[1] >= shape.verts.size() || tri[2] >= shape.verts.size()) {
                continue;
            }
            visit(std::array<Vec3, 3>{apply(world, shape.verts[tri[0]]),
                                      apply(world, shape.verts[tri[1]]),
                                      apply(world, shape.verts[tri[2]])});
        }
    }
}

template <class Visit>
void forEachReferenceTriangle(const PlacedReference& ref, const GeometryMap& geometry, Visit&& visit) {
    if (!ref.scolParts.empty()) {
        for (const auto& part : ref.scolParts) {
            const ModelMesh* mesh = findModel(geometry, part.modelPath);
            if (mesh == nullptr) continue;
            for (const auto& placement : part.placements) {
                forEachMeshTriangle(*mesh, compose(ref.pose, placement), visit);
            }
        }
        return;
    }
    if (const ModelMesh* mesh = findModel(geometry, ref.modelPath)) {
        forEachMeshTriangle(*mesh, ref.pose, visit);
    }
}

void drawFloor(SoftwareRaster& raster, const Bounds& bounds) {
    const Vec3 size = bounds.size();
    const Vec3 centre = bounds.centre();
    const double halfExtent = std::max({size.x, size.y, 1.0}) * kGridFraction;
    drawGroundGrid(raster, centre.x, centre.y, halfExtent, niceStep(halfExtent * 2.0 / kGridLines),
                   bounds.min.z, gridColour());
}

std::vector<std::size_t> matchingReferences(const CellData& cell, std::string_view key) {
    std::vector<std::size_t> matches;
    const auto wanted = lowered(key);
    for (std::size_t i = 0; i < cell.references.size(); ++i) {
        if (lowered(cell.references[i].formKey) == wanted) matches.push_back(i);
    }
    return matches;
}

}  // namespace

void Bounds::include(Vec3 point) noexcept {
    min = {std::min(min.x, point.x), std::min(min.y, point.y), std::min(min.z, point.z)};
    max = {std::max(max.x, point.x), std::max(max.y, point.y), std::max(max.z, point.z)};
}

void Bounds::include(const Bounds& other) noexcept {
    if (other.empty()) return;
    include(other.min);
    include(other.max);
}

double Bounds::radius() const noexcept {
    if (empty()) return 1.0;
    return std::max(length(size()) * 0.5, 1.0);
}

Vec3 apply(const Affine& affine, Vec3 point) {
    const auto& m = affine.m;
    return {m[0][0] * point.x + m[0][1] * point.y + m[0][2] * point.z + affine.t.x,
            m[1][0] * point.x + m[1][1] * point.y + m[1][2] * point.z + affine.t.y,
            m[2][0] * point.x + m[2][1] * point.y + m[2][2] * point.z + affine.t.z};
}

Affine compose(const Affine& outer, const Affine& inner) {
    Affine out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            out.m[i][j] = 0.0;
            for (std::size_t k = 0; k < 3; ++k) {
                out.m[i][j] += outer.m[i][k] * inner.m[k][j];
            }
        }
    }
    out.t = apply({outer.m, Vec3{}}, inner.t) + outer.t;
    return out;
}

Affine poseAffine(Vec3 position, Vec3 eulerXyzRadians, double scale) {
    Affine out;
    out.m = eulerXyz(eulerXyzRadians);
    for (auto& row : out.m) {
        for (auto& value : row) value *= scale;
    }
    out.t = position;
    return out;
}

Affine rowMajorAffine(const std::array<double, 9>& rotation, Vec3 translation, double scale) {
    Affine out;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            out.m[i][j] = rotation[i * 3 + j] * scale;
        }
    }
    out.t = translation;
    return out;
}

Result<CellData> parseCellReferences(const json& root) {
    if (!root.is_object() || !child(root, "references").is_array()) {
        return std::unexpected(Error{"InvalidCell", "cell payload has no references array"});
    }
    CellData cell;
    cell.cellFormKey = stringOr(root, "cellFormKey");
    for (const auto& node : arrayOf(root, "references")) {
        if (!node.is_object()) continue;
        PlacedReference ref;
        ref.formKey = stringOr(node, "formKey");
        ref.baseType = stringOr(node, "baseType");
        ref.modelPath = stringOr(node, "modelPath");
        ref.isMarker = boolOr(node, "isMarker");
        ref.editorId = stringOr(node, "editorId");
        ref.baseEditorId = stringOr(node, "baseEditorId");
        ref.light = child(node, "light").is_object();
        ref.decal = !stringOr(node, "decalDiffuse").empty();
        if (const auto& primitive = child(node, "primitive"); primitive.is_object()) {
            ref.primitive = true;
            ref.primitiveType = static_cast<int>(numberOr(primitive, "type", 0.0));
            ref.primitiveBounds = readXyz(child(primitive, "bounds"));
        }
        ref.position = readXyz(child(node, "position"));
        ref.pose = poseAffine(ref.position, readXyz(child(node, "rotation")),
                              readScale(numberOr(node, "scale", 1.0)));
        for (const auto& partNode : arrayOf(node, "scolParts")) {
            ScolPart part;
            part.modelPath = stringOr(partNode, "modelPath");
            for (const auto& placement : arrayOf(partNode, "placements")) {
                part.placements.push_back(poseAffine(readXyz(placement), readFlatRotation(placement),
                                                     readScale(numberOr(placement, "scale", 1.0))));
            }
            ref.scolParts.push_back(std::move(part));
        }
        cell.references.push_back(std::move(ref));
    }
    return cell;
}

ModelMesh parseModelMesh(const json& entry) {
    ModelMesh mesh;
    if (!entry.is_object()) {
        mesh.error = "geometry entry is not an object";
        return mesh;
    }
    if (entry.contains("error")) {
        mesh.error = stringOr(entry, "error");
        if (mesh.error.empty()) mesh.error = "geometry entry reports an error";
        return mesh;
    }
    for (const auto& shapeNode : arrayOf(entry, "shapes")) {
        if (!shapeNode.is_object()) continue;
        MeshShape shape;
        const auto& verts = arrayOf(shapeNode, "verts");
        for (std::size_t i = 0; i + 2 < verts.size(); i += 3) {
            shape.verts.push_back({numberAt(verts, i), numberAt(verts, i + 1), numberAt(verts, i + 2)});
        }
        const auto& tris = arrayOf(shapeNode, "tris");
        for (std::size_t i = 0; i + 2 < tris.size(); i += 3) {
            shape.tris.push_back({indexAt(tris, i), indexAt(tris, i + 1), indexAt(tris, i + 2)});
        }
        const auto& world = arrayOf(shapeNode, "world");
        if (world.size() == 16) {
            Affine local;
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t col = 0; col < 3; ++col) local.m[row][col] = numberAt(world, col * 4 + row);
            }
            local.t = {numberAt(world, 12), numberAt(world, 13), numberAt(world, 14)};
            shape.local = local;
        }
        if (!shape.verts.empty() && !shape.tris.empty()) mesh.shapes.push_back(std::move(shape));
    }
    return mesh;
}

Result<GeometryMap> parseGeometry(const json& root) {
    const json& geometry = root.is_object() && root.contains("geometry") ? root.at("geometry") : root;
    if (!geometry.is_object()) {
        return std::unexpected(Error{"InvalidGeometry", "geometry payload is not an object"});
    }
    GeometryMap map;
    for (const auto& [path, entry] : geometry.items()) {
        map[lowered(path)] = parseModelMesh(entry);
    }
    return map;
}

const ModelMesh* findModel(const GeometryMap& geometry, std::string_view modelPath) {
    if (modelPath.empty()) return nullptr;
    const auto it = geometry.find(lowered(modelPath));
    if (it == geometry.end()) return nullptr;
    if (!it->second.error.empty() || it->second.shapes.empty()) return nullptr;
    return &it->second;
}

WalkStats forEachWorldTriangle(const CellData& cell,
                               const GeometryMap& geometry,
                               WalkBudget budget,
                               const TriangleVisitor& visit) {
    WalkStats stats;
    for (std::size_t index = 0; index < cell.references.size(); ++index) {
        const auto& ref = cell.references[index];
        if (ref.isMarker) {
            ++stats.markers;
            ++stats.drawn;
            continue;
        }
        const bool referencesSpent = budget.maxReferences != 0 && stats.walked >= budget.maxReferences;
        const bool trianglesSpent = budget.maxTriangles != 0 && stats.triangles >= budget.maxTriangles;
        if (referencesSpent || trianglesSpent) {
            ++stats.overBudget;
            continue;
        }
        ++stats.walked;
        const std::size_t before = stats.triangles;
        forEachReferenceTriangle(ref, geometry, [&](const std::array<Vec3, 3>& corners) {
            ++stats.triangles;
            visit(WorldTriangle{corners, index});
        });
        if (stats.triangles > before) {
            ++stats.drawn;
        } else {
            ++stats.missingGeometry;
        }
    }
    return stats;
}

namespace {

// Invariant: a null infos pointer keeps the bounds to real geometry and isMarker positions, with no placeholder volumes.
CellScan scanReferences(const CellData& cell,
                        const GeometryMap& geometry,
                        WalkBudget budget,
                        const LayerSet& layers,
                        const std::vector<RefInfo>* infos) {
    CellScan scan;
    scan.referenceBounds.assign(cell.references.size(), Bounds{});
    const bool markerOn = layerOn(layers, LayerKind::marker);
    for (std::size_t i = 0; i < cell.references.size(); ++i) {
        const auto& ref = cell.references[i];
        scan.positions.include(ref.position);
        if (infos == nullptr) {
            if (ref.isMarker) scan.bounds.include(ref.position);
            continue;
        }
        const RefInfo& info = (*infos)[i];
        if (markerOn && (ref.isMarker || info.unresolvedMarker)) scan.bounds.include(ref.position);
        if (info.volume && layerOn(layers, info.volume->kind)) {
            for (const Vec3& corner : info.volume->corners) scan.bounds.include(corner);
        }
    }
    scan.stats = forEachWorldTriangle(cell, geometry, budget, [&](const WorldTriangle& tri) {
        for (const Vec3& corner : tri.corners) {
            scan.bounds.include(corner);
            scan.referenceBounds[tri.reference].include(corner);
        }
    });
    return scan;
}

}  // namespace

CellScan scanCell(const CellData& cell, const GeometryMap& geometry, WalkBudget budget, const LayerSet& layers) {
    const auto infos = classifyReferences(cell, geometry);
    return scanReferences(cell, geometry, budget, layers, &infos);
}

Result<CameraRequest> parseCameraRequest(const json& value) {
    CameraRequest request;
    if (value.is_null()) return request;
    if (value.is_string()) {
        request.preset = lowered(value.get<std::string>());
    } else if (value.is_object()) {
        if (value.contains("preset")) {
            if (!value.at("preset").is_string()) {
                return std::unexpected(Error{"InvalidArgument", "camera preset must be a string"});
            }
            request.preset = lowered(value.at("preset").get<std::string>());
        }
        if (value.contains("fov")) {
            if (!value.at("fov").is_number()) {
                return std::unexpected(Error{"InvalidArgument", "camera fov must be a number"});
            }
            const double fov = value.at("fov").get<double>();
            if (fov < kMinFovDegrees || fov > kMaxFovDegrees) {
                return std::unexpected(Error{"InvalidArgument", "camera fov must be from 10 to 120 degrees"});
            }
            request.fovDegrees = fov;
        }
        const bool hasEye = value.contains("eye");
        const bool hasTarget = value.contains("target");
        if (hasEye != hasTarget) {
            return std::unexpected(Error{"InvalidArgument", "camera eye and target must be given together"});
        }
        if (hasEye) {
            const auto eye = parseVec3(value.at("eye"));
            const auto target = parseVec3(value.at("target"));
            if (!eye || !target) {
                return std::unexpected(Error{"InvalidArgument", "camera eye and target must be [x, y, z] numbers"});
            }
            if (length(*eye - *target) == 0.0) {
                return std::unexpected(Error{"InvalidArgument", "camera eye and target must differ"});
            }
            request.eye = *eye;
            request.target = *target;
            request.preset = "custom";
        }
    } else {
        return std::unexpected(Error{"InvalidArgument", "camera must be a preset name or an object"});
    }
    if (!request.eye && !knownPreset(request.preset)) {
        return std::unexpected(Error{"InvalidArgument", "camera preset must be front, right, top or iso"});
    }
    return request;
}

Camera frameCamera(const CameraRequest& request, const Bounds& bounds, int width, int height) {
    Camera camera;
    camera.fovYDegrees = request.fovDegrees.value_or(kDefaultFovDegrees);
    camera.nearPlane = 1.0;
    if (request.eye && request.target) {
        camera.eye = *request.eye;
        camera.target = *request.target;
        camera.up = {0.0, 0.0, 1.0};
        return camera;
    }

    const Vec3 centre = bounds.centre();
    const double radius = bounds.radius();
    const Vec3 towardEye = presetViewDirection(request.preset);
    const Vec3 up = request.preset == "top" ? Vec3{0.0, 1.0, 0.0} : Vec3{0.0, 0.0, 1.0};
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    camera.target = centre;
    camera.up = up;

    if (request.preset != "iso") {
        camera.orthographic = true;
        camera.eye = centre + towardEye * (radius * 4.0);
        const Vec3 forward = normalized(centre - camera.eye);
        const Vec3 right = normalized(cross(forward, up));
        const Vec3 screenUp = cross(right, forward);
        double halfHeight = radius;
        if (!bounds.empty()) {
            halfHeight = 0.0;
            for (const Vec3& corner : boxCorners(bounds)) {
                const Vec3 offset = corner - centre;
                halfHeight = std::max(halfHeight, std::max(std::abs(dot(offset, screenUp)),
                                                           std::abs(dot(offset, right)) / aspect));
            }
            halfHeight *= kFramePadding;
        }
        camera.orthoHalfHeight = std::max(halfHeight, 1.0);
        return camera;
    }

    const double halfFov = camera.fovYDegrees * kPi / 360.0;
    const double halfWidthAngle = std::atan(std::tan(halfFov) * aspect);
    const double fit = std::sin(std::min(halfFov, halfWidthAngle));
    const double distance = radius * kPerspectiveFit / std::max(fit, 1e-3);
    camera.eye = centre + towardEye * distance;
    camera.nearPlane = std::max(1.0, distance - radius * kNearRadiusFactor);
    return camera;
}

Result<RenderOptions> parseRenderOptions(const json& args, int defaultWidth, int defaultHeight) {
    RenderOptions options;
    options.width = defaultWidth;
    options.height = defaultHeight;
    if (args.is_null()) return options;
    if (!args.is_object()) {
        return std::unexpected(Error{"InvalidArgument", "render arguments must be an object"});
    }

    const auto integerField = [&args](std::string_view key, long long fallback, long long low,
                                      long long high) -> Result<long long> {
        if (!args.contains(std::string(key))) return fallback;
        const auto& value = args.at(std::string(key));
        if (!value.is_number_integer()) {
            return std::unexpected(Error{"InvalidArgument", std::string(key) + " must be an integer"});
        }
        const long long number = value.get<long long>();
        if (number < low || number > high) {
            return std::unexpected(Error{"InvalidArgument",
                                         std::format("{} must be from {} to {}", key, low, high)});
        }
        return number;
    };

    const auto width = integerField("width", defaultWidth, kMinImageSide, kMaxImageSide);
    if (!width) return std::unexpected(width.error());
    const auto height = integerField("height", defaultHeight, kMinImageSide, kMaxImageSide);
    if (!height) return std::unexpected(height.error());
    const auto budget = integerField("max_references", kDefaultReferenceBudget, kMinReferenceBudget,
                                     kMaxReferenceBudget);
    if (!budget) return std::unexpected(budget.error());
    options.width = static_cast<int>(*width);
    options.height = static_cast<int>(*height);
    options.budget = WalkBudget{static_cast<std::size_t>(*budget), kMaxTriangleBudget};

    auto camera = parseCameraRequest(child(args, "camera"));
    if (!camera) return std::unexpected(camera.error());
    options.camera = std::move(*camera);

    auto layers = parseLayers(child(args, "layers"));
    if (!layers) return std::unexpected(layers.error());
    options.layers = *layers;

    const auto& keys = child(args, "highlight_keys");
    if (!keys.is_null()) {
        if (!keys.is_array()) {
            return std::unexpected(Error{"InvalidArgument", "highlight_keys must be an array of strings"});
        }
        if (keys.size() > kMaxHighlightKeys) {
            return std::unexpected(Error{"InvalidArgument", "highlight_keys takes at most 64 entries"});
        }
        for (const auto& key : keys) {
            if (!key.is_string()) {
                return std::unexpected(Error{"InvalidArgument", "highlight_keys must be an array of strings"});
            }
            options.highlightKeys.push_back(key.get<std::string>());
        }
    }
    return options;
}

Result<RenderedImage> renderCell(const CellData& cell, const GeometryMap& geometry, const RenderOptions& options) {
    if (const auto checked = checkSize(options); !checked) return std::unexpected(checked.error());

    const auto infos = classifyReferences(cell, geometry);
    const CellScan scan = scanReferences(cell, geometry, options.budget, options.layers, &infos);
    const Camera camera = frameCamera(options.camera, scan.bounds, options.width, options.height);
    SoftwareRaster raster(options.width, options.height, backgroundColour());
    raster.setCamera(camera);
    if (!scan.bounds.empty()) drawFloor(raster, scan.bounds);

    std::vector<Rgba> colours(cell.references.size());
    std::vector<std::vector<std::size_t>> matches;
    for (std::size_t i = 0; i < cell.references.size(); ++i) {
        colours[i] = cell.references[i].isMarker ? markerColour() : colourForRecordType(cell.references[i].baseType);
    }
    for (const auto& key : options.highlightKeys) {
        matches.push_back(matchingReferences(cell, key));
        for (const std::size_t index : matches.back()) colours[index] = highlightColour();
    }

    forEachWorldTriangle(cell, geometry, options.budget, [&](const WorldTriangle& tri) {
        if (!layerOn(options.layers, infos[tri.reference].layer)) return;
        raster.fillTriangle(tri.corners, colours[tri.reference]);
    });
    const Vec3 markerHalf{kMarkerHalfSize, kMarkerHalfSize, kMarkerHalfSize};
    std::size_t unresolvedMarkers = 0;
    std::size_t roomVolumes = 0;
    std::size_t triggerVolumes = 0;
    for (std::size_t i = 0; i < cell.references.size(); ++i) {
        const auto& ref = cell.references[i];
        if (layerOn(options.layers, LayerKind::marker)) {
            if (ref.isMarker) {
                raster.drawBox(ref.position - markerHalf, ref.position + markerHalf, colours[i]);
            } else if (infos[i].unresolvedMarker) {
                raster.drawBox(ref.position - markerHalf, ref.position + markerHalf, markerColour());
                ++unresolvedMarkers;
            }
        }
        const auto& volume = infos[i].volume;
        if (!volume || !layerOn(options.layers, volume->kind)) continue;
        drawVolume(raster, *volume);
        ++(volume->kind == LayerKind::room ? roomVolumes : triggerVolumes);
    }
    if (!scan.bounds.empty()) {
        const Vec3 size = scan.bounds.size();
        drawAxes(raster, scan.bounds.min, std::max({size.x, size.y, size.z}) * kAxisFraction);
    }

    json highlights = json::array();
    for (std::size_t k = 0; k < options.highlightKeys.size(); ++k) {
        json row{{"formKey", options.highlightKeys[k]}, {"found", !matches[k].empty()}};
        if (!matches[k].empty()) {
            const std::size_t index = matches[k].front();
            const Bounds& box = scan.referenceBounds[index];
            const Vec3 anchor = box.empty() ? cell.references[index].position : box.centre();
            const ScreenPoint point = raster.project(anchor);
            const bool onScreen = point.visible && point.x >= 0.0 && point.x < options.width &&
                                  point.y >= 0.0 && point.y < options.height;
            row["baseType"] = cell.references[index].baseType;
            row["matches"] = matches[k].size();
            row["onScreen"] = onScreen;
            row["pixel"] = onScreen ? json{{"x", static_cast<int>(std::floor(point.x))},
                                           {"y", static_cast<int>(std::floor(point.y))}}
                                    : json(nullptr);
        }
        highlights.push_back(std::move(row));
    }

    RenderedImage image;
    image.width = options.width;
    image.height = options.height;
    image.rgba = raster.rgba();
    image.report = {
        {"drawn", scan.stats.drawn},
        {"skipped", {{"missingGeometry", scan.stats.missingGeometry}, {"overBudget", scan.stats.overBudget}}},
        {"markers", scan.stats.markers},
        {"unresolvedMarkers", unresolvedMarkers},
        {"volumes", {{"room", roomVolumes}, {"trigger", triggerVolumes}}},
        {"layers", {{"room", options.layers.room}, {"trigger", options.layers.trigger},
                    {"marker", options.layers.marker}, {"effect", options.layers.effect}}},
        {"triangles", scan.stats.triangles},
        {"pixelsDrawn", raster.countDrawnPixels()},
        {"bounds", boundsJson(scan.bounds)},
        {"camera", cameraJson(camera, options.camera.preset)},
        {"legend", legendJson()},
        {"highlights", highlights},
    };
    return image;
}

Result<RenderedImage> renderModel(const ModelMesh& mesh, const RenderOptions& options) {
    if (const auto checked = checkSize(options); !checked) return std::unexpected(checked.error());
    if (!mesh.error.empty()) return std::unexpected(Error{"NoGeometry", mesh.error});

    Bounds bounds;
    forEachMeshTriangle(mesh, Affine{}, [&](const std::array<Vec3, 3>& corners) {
        for (const Vec3& corner : corners) bounds.include(corner);
    });
    if (bounds.empty()) return std::unexpected(Error{"NoGeometry", "model has no triangles"});

    const Camera camera = frameCamera(options.camera, bounds, options.width, options.height);
    SoftwareRaster raster(options.width, options.height, backgroundColour());
    raster.setCamera(camera);
    drawFloor(raster, bounds);

    const Rgba colour = colourForRecordType("STAT");
    std::size_t triangles = 0;
    forEachMeshTriangle(mesh, Affine{}, [&](const std::array<Vec3, 3>& corners) {
        ++triangles;
        raster.fillTriangle(corners, colour);
    });
    const Vec3 size = bounds.size();
    drawAxes(raster, bounds.min, std::max({size.x, size.y, size.z}) * kAxisFraction);

    RenderedImage image;
    image.width = options.width;
    image.height = options.height;
    image.rgba = raster.rgba();
    image.report = {
        {"shapes", mesh.shapes.size()},
        {"triangles", triangles},
        {"pixelsDrawn", raster.countDrawnPixels()},
        {"bounds", boundsJson(bounds)},
        {"camera", cameraJson(camera, options.camera.preset)},
        {"legend", legendJson()},
    };
    return image;
}

Result<std::string> encodePngBase64(const RenderedImage& image) {
    auto png = textures::writePng(image.rgba, image.width, image.height);
    if (!png) return std::unexpected(png.error());
    return core::encodeBase64(std::as_bytes(std::span(*png)));
}

json layoutSummary(const CellData& cell, const GeometryMap& geometry) {
    const CellScan scan = scanReferences(cell, geometry, WalkBudget{}, LayerSet{}, nullptr);
    const std::size_t cells = static_cast<std::size_t>(kOccupancySide) * kOccupancySide;

    std::map<std::string, std::size_t> byType;
    std::size_t markers = 0;
    std::vector<std::size_t> occupancy(cells, 0);
    std::vector<std::map<std::string, std::size_t>> models(cells);
    const Vec3 origin = scan.positions.empty() ? Vec3{} : scan.positions.min;
    const Vec3 span = scan.positions.size();
    const double cellWidth = std::max(span.x, 1.0) / kOccupancySide;
    const double cellHeight = std::max(span.y, 1.0) / kOccupancySide;
    for (std::size_t i = 0; i < cell.references.size(); ++i) {
        const auto& ref = cell.references[i];
        ++byType[ref.baseType.empty() ? std::string("UNKNOWN") : ref.baseType];
        if (ref.isMarker) ++markers;
        if (scan.positions.empty()) continue;
        const auto column = std::clamp(static_cast<int>(std::floor((ref.position.x - origin.x) / cellWidth)),
                                       0, kOccupancySide - 1);
        const auto row = std::clamp(static_cast<int>(std::floor((ref.position.y - origin.y) / cellHeight)),
                                    0, kOccupancySide - 1);
        const auto slot = static_cast<std::size_t>(row) * kOccupancySide + static_cast<std::size_t>(column);
        ++occupancy[slot];
        ++models[slot][ref.modelPath.empty() ? std::string("(none)") : ref.modelPath];
    }

    json byTypeJson = json::object();
    for (const auto& [type, count] : byType) byTypeJson[type] = count;

    json rows = json::array();
    json dominant = json::array();
    for (int row = 0; row < kOccupancySide; ++row) {
        json line = json::array();
        for (int column = 0; column < kOccupancySide; ++column) {
            const auto slot = static_cast<std::size_t>(row) * kOccupancySide + static_cast<std::size_t>(column);
            line.push_back(occupancy[slot]);
            if (occupancy[slot] == 0) continue;
            std::string best;
            std::size_t bestCount = 0;
            for (const auto& [model, count] : models[slot]) {
                if (count > bestCount) {
                    best = model;
                    bestCount = count;
                }
            }
            dominant.push_back({{"column", column}, {"row", row}, {"count", occupancy[slot]},
                                {"model", best}, {"modelCount", bestCount}});
        }
        rows.push_back(std::move(line));
    }

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < scan.referenceBounds.size(); ++i) {
        if (!scan.referenceBounds[i].empty()) order.push_back(i);
    }
    const auto diagonal = [&scan](std::size_t index) { return length(scan.referenceBounds[index].size()); };
    std::ranges::sort(order, [&](std::size_t left, std::size_t right) {
        const double a = diagonal(left);
        const double b = diagonal(right);
        return a != b ? a > b : left < right;
    });
    json large = json::array();
    for (std::size_t k = 0; k < std::min(order.size(), kLargeStructureCount); ++k) {
        const std::size_t index = order[k];
        const auto& ref = cell.references[index];
        large.push_back({{"formKey", ref.formKey},
                         {"baseType", ref.baseType},
                         {"modelPath", ref.modelPath},
                         {"position", vecJson(ref.position)},
                         {"centre", vecJson(scan.referenceBounds[index].centre())},
                         {"size", vecJson(scan.referenceBounds[index].size())},
                         {"diagonal", diagonal(index)}});
    }

    std::map<long long, std::size_t> floors;
    std::map<long long, std::size_t> ceilings;
    forEachWorldTriangle(cell, geometry, WalkBudget{}, [&](const WorldTriangle& tri) {
        const Vec3 normal = normalized(cross(tri.corners[1] - tri.corners[0], tri.corners[2] - tri.corners[0]));
        const double z = (tri.corners[0].z + tri.corners[1].z + tri.corners[2].z) / 3.0;
        const auto bucket = static_cast<long long>(std::floor(z / kHeightBucket));
        if (normal.z > kFloorCosine) ++floors[bucket];
        if (normal.z < -kFloorCosine) ++ceilings[bucket];
    });
    const auto histogram = [](const std::map<long long, std::size_t>& bins) {
        std::vector<std::pair<long long, std::size_t>> top(bins.begin(), bins.end());
        std::ranges::sort(top, [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        if (top.size() > kHeightBucketCount) top.resize(kHeightBucketCount);
        std::ranges::sort(top, [](const auto& a, const auto& b) { return a.first < b.first; });
        json out = json::array();
        for (const auto& [bucket, count] : top) {
            out.push_back({{"z", static_cast<double>(bucket) * kHeightBucket}, {"triangles", count}});
        }
        return out;
    };

    return {
        {"cellFormKey", cell.cellFormKey},
        {"references", {{"total", cell.references.size()},
                        {"markers", markers},
                        {"withGeometry", cell.references.size() - markers - scan.stats.missingGeometry},
                        {"missingGeometry", scan.stats.missingGeometry}}},
        {"bounds", boundsJson(scan.bounds)},
        {"counts", {{"byType", byTypeJson}}},
        {"occupancy", {{"columns", kOccupancySide},
                       {"rows", kOccupancySide},
                       {"minX", origin.x},
                       {"minY", origin.y},
                       {"cellWidth", cellWidth},
                       {"cellHeight", cellHeight},
                       {"counts", rows},
                       {"dominant", dominant}}},
        {"largeStructures", large},
        {"heights", {{"bucketSize", kHeightBucket},
                     {"floors", histogram(floors)},
                     {"ceilings", histogram(ceilings)}}},
    };
}

}  // namespace fo4ide::preview
