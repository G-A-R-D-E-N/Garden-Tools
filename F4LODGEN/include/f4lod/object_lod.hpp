#pragma once

#include "f4lod/record_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

enum class ObjectLodReferenceDecision {
    include,
    skip_deleted,
    skip_unsupported_base,
    skip_missing_position,
    skip_marker,
    skip_initially_disabled,
    skip_enable_parent_disabled,
};

struct ObjectLodReferencePolicyInput {
    bool deleted{};
    bool supported_static_base{true};
    bool has_position{true};
    bool persistent{};
    bool reference_visible_when_distant{};
    bool base_marker{};
    bool initially_disabled{};
    bool has_enable_parent{};
    bool enable_parent_target_present{};
    bool enable_parent_respects_lod_state{};
    bool enable_parent_initially_disabled{};
    bool enable_parent_opposite_state{};
};

struct ObjectLodReferencePolicyResult {
    ObjectLodReferenceDecision decision{ObjectLodReferenceDecision::include};
    bool include{true};
    bool export_enable_parent{};
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

struct ObjectLodPreflightCounts {
    std::size_t total{};
    std::size_t included{};
    std::size_t excluded{};
    std::size_t safe_static{};
    std::size_t enable_state_sensitive{};
    std::size_t potentially_stateful{};
    std::size_t unsupported_asset_material{};
    std::size_t missing_authored_lod{};
    std::size_t forced{};
};

struct ObjectLodPreflightDiagnostic {
    std::size_t descriptor_index{};
    ObjectLodSafety safety{ObjectLodSafety::safe_static};
    std::string code;
    std::string message;
};

struct ObjectLodPreflight {
    ObjectLodPreflightCounts counts;
    std::vector<ObjectLodPreflightDiagnostic> diagnostics;
};

[[nodiscard]] std::optional<std::size_t> object_lod_level_index(
    std::uint32_t lod_level) noexcept;

[[nodiscard]] std::optional<ObjectLodCell> object_lod_cell_from_world(
    float world_x,
    float world_y) noexcept;

[[nodiscard]] std::optional<ObjectLodCell> object_lod_block_for_cell(
    ObjectLodCell cell,
    ObjectLodCell southwest_cell,
    std::uint32_t lod_level) noexcept;

[[nodiscard]] ObjectLodReferencePolicyResult evaluate_object_lod_reference_policy(
    const ObjectLodReferencePolicyInput& input) noexcept;

[[nodiscard]] bool object_lod_has_authored_model(
    const ObjectLodDescriptor& descriptor,
    std::uint32_t lod_level) noexcept;

[[nodiscard]] ObjectLodSafety classify_object_lod_safety(
    bool asset_material_supported,
    bool has_authored_lod,
    bool potentially_stateful,
    bool has_enable_parent,
    bool explicitly_forced = false) noexcept;

[[nodiscard]] ObjectLodPreflight preflight_object_lod_descriptors(
    std::span<const ObjectLodDescriptor> descriptors);

[[nodiscard]] const char* object_lod_safety_name(ObjectLodSafety safety) noexcept;
[[nodiscard]] const char* object_lod_reference_decision_name(
    ObjectLodReferenceDecision decision) noexcept;

} // namespace f4lod
