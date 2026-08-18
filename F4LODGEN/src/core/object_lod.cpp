#include "f4lod/object_lod.hpp"

#include <cmath>
#include <limits>

namespace f4lod {

std::optional<std::size_t> object_lod_level_index(
    std::uint32_t lod_level) noexcept {
    for (std::size_t i = 0; i < kObjectLodLevels.size(); ++i) {
        if (kObjectLodLevels[i] == lod_level) return i;
    }
    return std::nullopt;
}

std::optional<ObjectLodCell> object_lod_cell_from_world(
    float world_x,
    float world_y) noexcept {
    if (!std::isfinite(world_x) || !std::isfinite(world_y)) return std::nullopt;

    const double cell_x = std::floor(static_cast<double>(world_x) / 4096.0);
    const double cell_y = std::floor(static_cast<double>(world_y) / 4096.0);
    constexpr double kMin = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr double kMax = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    if (cell_x < kMin || cell_x > kMax || cell_y < kMin || cell_y > kMax) {
        return std::nullopt;
    }

    return ObjectLodCell{
        static_cast<std::int32_t>(cell_x),
        static_cast<std::int32_t>(cell_y),
    };
}

bool object_lod_has_authored_model(
    const ObjectLodDescriptor& descriptor,
    std::uint32_t lod_level) noexcept {
    const auto index = object_lod_level_index(lod_level);
    return index.has_value() && !descriptor.authored_lod_models[*index].empty();
}

ObjectLodSafety classify_object_lod_safety(
    bool asset_material_supported,
    bool has_authored_lod,
    bool potentially_stateful,
    bool has_enable_parent,
    bool explicitly_forced) noexcept {
    if (explicitly_forced) return ObjectLodSafety::forced;
    if (!asset_material_supported) return ObjectLodSafety::unsupported_asset_material;
    if (!has_authored_lod) return ObjectLodSafety::missing_authored_lod;
    if (potentially_stateful) return ObjectLodSafety::potentially_stateful;
    if (has_enable_parent) return ObjectLodSafety::enable_state_sensitive;
    return ObjectLodSafety::safe_static;
}

const char* object_lod_safety_name(ObjectLodSafety safety) noexcept {
    switch (safety) {
    case ObjectLodSafety::safe_static: return "safe-static";
    case ObjectLodSafety::enable_state_sensitive: return "enable-state-sensitive";
    case ObjectLodSafety::potentially_stateful: return "potentially-stateful";
    case ObjectLodSafety::unsupported_asset_material: return "unsupported-asset-material";
    case ObjectLodSafety::missing_authored_lod: return "missing-authored-lod";
    case ObjectLodSafety::forced: return "forced";
    }
    return "safe-static";
}

} // namespace f4lod
