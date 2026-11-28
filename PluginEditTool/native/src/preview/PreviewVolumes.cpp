#include <fo4ide/preview/PreviewVolumes.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <regex>
#include <string>
#include <utility>

namespace fo4ide::preview {
namespace {

constexpr double kTriggerDefaultSize = 64.0;
constexpr double kRoomDefaultSize = 256.0;
constexpr Rgba kRoomColour{0x00, 0xFF, 0xFF, 255};
constexpr Rgba kTriggerColour{0xFF, 0x99, 0x00, 255};

const std::regex kEffectPath(R"((^|[\\/])effects[\\/])", std::regex::icase);
const std::regex kRoomPath(R"(room(marker|bounds?))", std::regex::icase);
const std::regex kTriggerPath(R"(trigger|primitive)", std::regex::icase);
const std::regex kMarkerPath(R"(xmarker|cocmarker|soundmarker|mapmarker|playerstart)", std::regex::icase);

bool isRoomPrimitive(const PlacedReference& ref) {
    return ref.baseEditorId == "RoomMarker" || ref.baseEditorId == "PortalMarker";
}

bool isMarkerBaseType(const std::string& type) {
    return type == "SOUN" || type == "IDLM" || type == "NAVM" || type == "XMRK" || type == "COCM" || type == "MRKR";
}

bool startsWithCoc(const std::string& text) {
    return text.rfind("COC", 0) == 0;
}

std::optional<Vec3> modelSize(const PlacedReference& ref, const GeometryMap& geometry, Vec3& centre) {
    const ModelMesh* mesh = findModel(geometry, ref.modelPath);
    if (mesh == nullptr) return std::nullopt;
    Bounds bounds;
    for (const auto& shape : mesh->shapes) {
        for (const Vec3& vert : shape.verts) bounds.include(vert);
    }
    if (bounds.empty()) return std::nullopt;
    centre = bounds.centre();
    const Vec3 size = bounds.size();
    return Vec3{std::max(size.x, 1.0), std::max(size.y, 1.0), std::max(size.z, 1.0)};
}

}  // namespace

Result<LayerSet> parseLayers(const nlohmann::json& value) {
    LayerSet layers;
    if (value.is_null()) return layers;
    if (!value.is_object()) {
        return std::unexpected(Error{"InvalidArgument", "layers must be an object of booleans"});
    }
    for (const auto& [key, flag] : value.items()) {
        if (!flag.is_boolean()) {
            return std::unexpected(Error{"InvalidArgument", "layers." + key + " must be a boolean"});
        }
        if (key == "room") {
            layers.room = flag.get<bool>();
        } else if (key == "trigger") {
            layers.trigger = flag.get<bool>();
        } else if (key == "marker") {
            layers.marker = flag.get<bool>();
        } else if (key == "effect") {
            layers.effect = flag.get<bool>();
        } else {
            return std::unexpected(Error{"InvalidArgument", "layers has no layer named " + key});
        }
    }
    return layers;
}

bool layerOn(const LayerSet& layers, LayerKind kind) {
    switch (kind) {
        case LayerKind::room: return layers.room;
        case LayerKind::trigger: return layers.trigger;
        case LayerKind::marker: return layers.marker;
        case LayerKind::effect: return layers.effect;
        case LayerKind::none: break;
    }
    return true;
}

// Invariant: mirrors web/src/util/cellLayer.ts:cellLayerKindOf; lights and decals sit on no toggleable layer.
LayerKind layerKindOf(const PlacedReference& ref) {
    if (ref.decal || ref.light || ref.baseType == "LIGH") return LayerKind::none;
    if (ref.primitive && isRoomPrimitive(ref)) return LayerKind::room;
    const std::string& path = ref.modelPath;
    if (std::regex_search(path, kEffectPath)) return LayerKind::effect;
    if (std::regex_search(path, kRoomPath)) return LayerKind::room;
    if (ref.primitive && path.empty()) return LayerKind::trigger;
    if (std::regex_search(path, kTriggerPath)) return LayerKind::trigger;
    const bool unmodelled = path.empty() && ref.scolParts.empty();
    const bool coc = startsWithCoc(ref.editorId) || startsWithCoc(ref.baseEditorId);
    if (ref.isMarker || unmodelled || std::regex_search(path, kMarkerPath) || isMarkerBaseType(ref.baseType) || coc) {
        return LayerKind::marker;
    }
    return LayerKind::none;
}

bool isResolved(const PlacedReference& ref, const GeometryMap& geometry) {
    if (!ref.scolParts.empty()) {
        return std::ranges::any_of(ref.scolParts, [&](const ScolPart& part) {
            return findModel(geometry, part.modelPath) != nullptr;
        });
    }
    return findModel(geometry, ref.modelPath) != nullptr;
}

namespace {

constexpr int kRingSegments = 32;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr std::array<std::pair<std::size_t, std::size_t>, 4> kPlaneEdges{{{0, 1}, {1, 3}, {3, 2}, {2, 0}}};

struct PrimitiveSize {
    VolumeShape shape{VolumeShape::box};
    Vec3 half{};
};

// Invariant: primitive type numbers match web/src/cellPrimitive.ts PRIMITIVE_TYPE_NAMES; anything but 2, 3, 4 or 5 is a box.
std::optional<PrimitiveSize> primitiveSize(const PlacedReference& ref) {
    const Vec3 b = ref.primitiveBounds;
    if (ref.primitiveType == 2) {
        const double radius = std::max({b.x, b.y, b.z});
        if (radius > 0.0) return PrimitiveSize{VolumeShape::sphere, {radius, radius, radius}};
        return std::nullopt;
    }
    if (ref.primitiveType == 3) {
        if (b.x > 0.0 && b.y > 0.0) return PrimitiveSize{VolumeShape::plane, {b.x, b.y, 0.0}};
        return std::nullopt;
    }
    if (ref.primitiveType == 4) {
        const Vec3 half{std::max(b.x, 0.0), std::max(b.y, 0.0), std::max(b.z, 0.0)};
        if (half.x > 0.0 || half.y > 0.0 || half.z > 0.0) return PrimitiveSize{VolumeShape::line, half};
        return std::nullopt;
    }
    if (b.x > 0.0 && b.y > 0.0 && b.z > 0.0) {
        return PrimitiveSize{ref.primitiveType == 5 ? VolumeShape::ellipsoid : VolumeShape::box, b};
    }
    return std::nullopt;
}

Volume makeVolume(const PlacedReference& ref, const GeometryMap& geometry, LayerKind kind) {
    Volume volume;
    volume.kind = kind;
    volume.pose = ref.pose;
    std::optional<PrimitiveSize> primitive;
    if (ref.primitive) primitive = primitiveSize(ref);
    if (primitive) {
        volume.shape = primitive->shape;
        volume.half = primitive->half;
    } else {
        Vec3 size{};
        if (const auto fromModel = modelSize(ref, geometry, volume.centre)) {
            size = *fromModel;
        } else {
            const double edge = kind == LayerKind::room ? kRoomDefaultSize : kTriggerDefaultSize;
            size = {edge, edge, edge};
        }
        volume.half = size * 0.5;
    }
    const Vec3 c = volume.centre;
    const Vec3 h = volume.half;
    for (std::size_t i = 0; i < 8; ++i) {
        const Vec3 local{c.x + ((i & 1) != 0 ? h.x : -h.x),
                         c.y + ((i & 2) != 0 ? h.y : -h.y),
                         c.z + ((i & 4) != 0 ? h.z : -h.z)};
        volume.corners[i] = apply(ref.pose, local);
    }
    return volume;
}

void drawRing(SoftwareRaster& raster, const Volume& volume, std::size_t axisA, std::size_t axisB, Rgba colour) {
    const std::array<double, 3> half{volume.half.x, volume.half.y, volume.half.z};
    const auto pointAt = [&](double angle) {
        std::array<double, 3> local{volume.centre.x, volume.centre.y, volume.centre.z};
        local[axisA] += half[axisA] * std::cos(angle);
        local[axisB] += half[axisB] * std::sin(angle);
        return apply(volume.pose, Vec3{local[0], local[1], local[2]});
    };
    Vec3 previous = pointAt(0.0);
    for (int step = 1; step <= kRingSegments; ++step) {
        const Vec3 next = pointAt(kTwoPi * step / kRingSegments);
        raster.drawLine(previous, next, colour);
        previous = next;
    }
}

}  // namespace

// Invariant: mirrors web/src/cellPlaceholders.ts box rules: primitive bounds, then model bounds, then the default edge.
RefInfo classifyReference(const PlacedReference& ref, const GeometryMap& geometry) {
    RefInfo info;
    info.layer = layerKindOf(ref);
    const bool resolved = isResolved(ref, geometry);
    info.unresolvedMarker = !ref.isMarker && !ref.primitive && info.layer == LayerKind::marker && !resolved;

    LayerKind kind = LayerKind::none;
    if (ref.primitive) {
        kind = isRoomPrimitive(ref) ? LayerKind::room : LayerKind::trigger;
    } else if ((info.layer == LayerKind::room || info.layer == LayerKind::trigger) && !resolved) {
        kind = info.layer;
    }
    if (kind != LayerKind::none) info.volume = makeVolume(ref, geometry, kind);
    return info;
}

std::vector<RefInfo> classifyReferences(const CellData& cell, const GeometryMap& geometry) {
    std::vector<RefInfo> infos;
    infos.reserve(cell.references.size());
    for (const auto& ref : cell.references) infos.push_back(classifyReference(ref, geometry));
    return infos;
}

void drawVolume(SoftwareRaster& raster, const Volume& volume) {
    const Rgba colour = volume.kind == LayerKind::room ? kRoomColour : kTriggerColour;
    switch (volume.shape) {
        case VolumeShape::sphere:
        case VolumeShape::ellipsoid:
            drawRing(raster, volume, 0, 1, colour);
            drawRing(raster, volume, 0, 2, colour);
            drawRing(raster, volume, 1, 2, colour);
            return;
        case VolumeShape::plane:
            for (const auto& [from, to] : kPlaneEdges) {
                raster.drawLine(volume.corners[from], volume.corners[to], colour);
            }
            return;
        case VolumeShape::line:
            raster.drawLine(volume.corners[0], volume.corners[7], colour);
            return;
        case VolumeShape::box: break;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        for (const std::size_t bit : {std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
            if ((i & bit) == 0) raster.drawLine(volume.corners[i], volume.corners[i | bit], colour);
        }
    }
}

}  // namespace fo4ide::preview
