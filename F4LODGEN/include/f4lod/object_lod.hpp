#pragma once

#include "f4lod/record_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace f4lod {

inline constexpr std::array<std::uint32_t, 4> kObjectLodLevels{4U, 8U, 16U, 32U};

struct ObjectLodCell {
    std::int32_t x{};
    std::int32_t y{};
};

struct ObjectLodReferenceTransform {
    float x{};
    float y{};
    float z{};
    float rotation_x{};
    float rotation_y{};
    float rotation_z{};
    float scale{1.0F};
};

struct ObjectLodModelTransform {
    bool present{};
    std::array<float, 3> translation{};
    std::array<float, 3> rotation{};
    float scale{1.0F};
};

struct ObjectLodMaterialSwap {
    std::string source;
    std::string target;
};

enum class ObjectLodSafety {
    safe_static,
    enable_state_sensitive,
    potentially_stateful,
    unsupported_asset_material,
    missing_authored_lod,
    forced,
};

struct ObjectLodDescriptor {
    FormKey reference;
    FormKey base;
    std::optional<FormKey> worldspace;
    std::optional<FormKey> cell;
    std::optional<FormKey> enable_parent;

    ObjectLodReferenceTransform reference_transform;
    ObjectLodModelTransform model_transform;
    ObjectLodCell exterior_cell;

    std::uint32_t reference_flags{};
    std::uint32_t static_flags{};
    std::uint8_t alpha_threshold{};

    std::string static_name;
    std::string material_name;
    std::string full_model;
    std::array<std::string, 4> authored_lod_models;
    std::vector<ObjectLodMaterialSwap> material_swaps;

    ObjectLodSafety safety{ObjectLodSafety::safe_static};
    std::string exclusion_reason;
};

[[nodiscard]] std::optional<std::size_t> object_lod_level_index(
    std::uint32_t lod_level) noexcept;

[[nodiscard]] std::optional<ObjectLodCell> object_lod_cell_from_world(
    float world_x,
    float world_y) noexcept;

[[nodiscard]] bool object_lod_has_authored_model(
    const ObjectLodDescriptor& descriptor,
    std::uint32_t lod_level) noexcept;

[[nodiscard]] ObjectLodSafety classify_object_lod_safety(
    bool asset_material_supported,
    bool has_authored_lod,
    bool potentially_stateful,
    bool has_enable_parent,
    bool explicitly_forced = false) noexcept;

[[nodiscard]] const char* object_lod_safety_name(ObjectLodSafety safety) noexcept;

} // namespace f4lod
