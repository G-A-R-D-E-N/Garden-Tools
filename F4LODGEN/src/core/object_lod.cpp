#include "f4lod/object_lod.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace f4lod {
namespace {

bool has_any_authored_model(const ObjectLodDescriptor& descriptor) noexcept {
    return std::any_of(
        descriptor.authored_lod_models.begin(),
        descriptor.authored_lod_models.end(),
        [](const std::string& path) { return !path.empty(); });
}

ObjectLodSafety effective_preflight_safety(
    const ObjectLodDescriptor& descriptor) noexcept {
    if (descriptor.safety != ObjectLodSafety::safe_static) return descriptor.safety;
    if (!has_any_authored_model(descriptor)) return ObjectLodSafety::missing_authored_lod;
    if (descriptor.enable_parent.has_value()) return ObjectLodSafety::enable_state_sensitive;
    return ObjectLodSafety::safe_static;
}

std::string default_preflight_message(ObjectLodSafety safety) {
    switch (safety) {
    case ObjectLodSafety::safe_static:
        return "Reference is eligible for unconditional authored object LOD.";
    case ObjectLodSafety::enable_state_sensitive:
        return "Reference has enable-parent state and must not be baked as unconditional LOD.";
    case ObjectLodSafety::potentially_stateful:
        return "Reference is potentially stateful and is excluded by the v0.1 safety policy.";
    case ObjectLodSafety::unsupported_asset_material:
        return "Reference uses an unsupported model or material feature and fails closed.";
    case ObjectLodSafety::missing_authored_lod:
        return "Reference has no usable authored LOD model and is excluded in v0.1.";
    case ObjectLodSafety::forced:
        return "Reference is explicitly forced by policy and bypasses the normal exclusion state.";
    }
    return "Reference has an unknown object LOD safety state.";
}

void increment_safety_count(
    ObjectLodPreflightCounts& counts,
    ObjectLodSafety safety) noexcept {
    switch (safety) {
    case ObjectLodSafety::safe_static: ++counts.safe_static; break;
    case ObjectLodSafety::enable_state_sensitive: ++counts.enable_state_sensitive; break;
    case ObjectLodSafety::potentially_stateful: ++counts.potentially_stateful; break;
    case ObjectLodSafety::unsupported_asset_material: ++counts.unsupported_asset_material; break;
    case ObjectLodSafety::missing_authored_lod: ++counts.missing_authored_lod; break;
    case ObjectLodSafety::forced: ++counts.forced; break;
    }
}

ObjectLodReferencePolicyResult skip_reference(
    ObjectLodReferenceDecision decision) noexcept {
    return ObjectLodReferencePolicyResult{decision, false, false};
}

} // namespace

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

std::optional<ObjectLodCell> object_lod_block_for_cell(
    ObjectLodCell cell,
    ObjectLodCell southwest_cell,
    std::uint32_t lod_level) noexcept {
    if (lod_level == 0U ||
        lod_level > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }

    const auto level = static_cast<std::int64_t>(lod_level);
    const auto delta_x = static_cast<std::int64_t>(cell.x) - southwest_cell.x;
    const auto delta_y = static_cast<std::int64_t>(cell.y) - southwest_cell.y;
    const auto block_x = static_cast<std::int64_t>(southwest_cell.x) +
        (delta_x / level) * level;
    const auto block_y = static_cast<std::int64_t>(southwest_cell.y) +
        (delta_y / level) * level;

    if (block_x < std::numeric_limits<std::int32_t>::min() ||
        block_x > std::numeric_limits<std::int32_t>::max() ||
        block_y < std::numeric_limits<std::int32_t>::min() ||
        block_y > std::numeric_limits<std::int32_t>::max()) {
        return std::nullopt;
    }

    return ObjectLodCell{
        static_cast<std::int32_t>(block_x),
        static_cast<std::int32_t>(block_y),
    };
}

ObjectLodReferencePolicyResult evaluate_object_lod_reference_policy(
    const ObjectLodReferencePolicyInput& input) noexcept {
    if (input.deleted) {
        return skip_reference(ObjectLodReferenceDecision::skip_deleted);
    }
    if (!input.supported_static_base) {
        return skip_reference(ObjectLodReferenceDecision::skip_unsupported_base);
    }
    if (!input.has_position) {
        return skip_reference(ObjectLodReferenceDecision::skip_missing_position);
    }
    if (input.persistent &&
        !input.reference_visible_when_distant &&
        input.base_marker) {
        return skip_reference(ObjectLodReferenceDecision::skip_marker);
    }
    if (input.initially_disabled &&
        !input.has_enable_parent &&
        !input.reference_visible_when_distant) {
        return skip_reference(ObjectLodReferenceDecision::skip_initially_disabled);
    }

    ObjectLodReferencePolicyResult result;
    if (input.has_enable_parent && input.enable_parent_target_present) {
        if (input.enable_parent_respects_lod_state) {
            result.export_enable_parent = true;
        } else if (!input.reference_visible_when_distant) {
            bool parent_disabled = input.enable_parent_initially_disabled;
            if (input.enable_parent_opposite_state) parent_disabled = !parent_disabled;
            if (parent_disabled) {
                return skip_reference(ObjectLodReferenceDecision::skip_enable_parent_disabled);
            }
        }
    }
    return result;
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

ObjectLodPreflight preflight_object_lod_descriptors(
    std::span<const ObjectLodDescriptor> descriptors) {
    ObjectLodPreflight result;
    result.counts.total = descriptors.size();

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const auto& descriptor = descriptors[i];
        const auto safety = effective_preflight_safety(descriptor);
        increment_safety_count(result.counts, safety);

        const bool included = safety == ObjectLodSafety::safe_static ||
                              safety == ObjectLodSafety::forced;
        if (included) {
            ++result.counts.included;
        } else {
            ++result.counts.excluded;
        }

        if (safety != ObjectLodSafety::safe_static) {
            ObjectLodPreflightDiagnostic diagnostic;
            diagnostic.descriptor_index = i;
            diagnostic.safety = safety;
            diagnostic.code = object_lod_safety_name(safety);
            diagnostic.message = descriptor.exclusion_reason.empty()
                ? default_preflight_message(safety)
                : descriptor.exclusion_reason;
            result.diagnostics.push_back(std::move(diagnostic));
        }
    }

    return result;
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

const char* object_lod_reference_decision_name(
    ObjectLodReferenceDecision decision) noexcept {
    switch (decision) {
    case ObjectLodReferenceDecision::include: return "include";
    case ObjectLodReferenceDecision::skip_deleted: return "skip-deleted";
    case ObjectLodReferenceDecision::skip_unsupported_base: return "skip-unsupported-base";
    case ObjectLodReferenceDecision::skip_missing_position: return "skip-missing-position";
    case ObjectLodReferenceDecision::skip_marker: return "skip-marker";
    case ObjectLodReferenceDecision::skip_initially_disabled: return "skip-initially-disabled";
    case ObjectLodReferenceDecision::skip_enable_parent_disabled: return "skip-enable-parent-disabled";
    }
    return "include";
}

} // namespace f4lod
