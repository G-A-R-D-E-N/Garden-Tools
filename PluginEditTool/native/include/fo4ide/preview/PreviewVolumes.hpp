#pragma once

#include <array>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>
#include <fo4ide/preview/PreviewRender.hpp>

namespace fo4ide::preview {

enum class VolumeShape { box, sphere, plane, line, ellipsoid };

struct Volume {
    LayerKind kind{LayerKind::none};
    VolumeShape shape{VolumeShape::box};
    Affine pose;
    Vec3 centre{};
    Vec3 half{};
    std::array<Vec3, 8> corners{};
};

struct RefInfo {
    LayerKind layer{LayerKind::none};
    bool unresolvedMarker{false};
    std::optional<Volume> volume;
};

Result<LayerSet> parseLayers(const nlohmann::json& value);
bool layerOn(const LayerSet& layers, LayerKind kind);
LayerKind layerKindOf(const PlacedReference& ref);
bool isResolved(const PlacedReference& ref, const GeometryMap& geometry);
RefInfo classifyReference(const PlacedReference& ref, const GeometryMap& geometry);
std::vector<RefInfo> classifyReferences(const CellData& cell, const GeometryMap& geometry);
void drawVolume(SoftwareRaster& raster, const Volume& volume);

}  // namespace fo4ide::preview
