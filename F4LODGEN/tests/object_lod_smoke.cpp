#include "f4lod/object_lod.hpp"

#include <cassert>
#include <limits>
#include <string>
#include <vector>

int main() {
    using f4lod::ObjectLodReferenceDecision;
    using f4lod::ObjectLodSafety;

    assert(f4lod::object_lod_level_index(4U) == 0U);
    assert(f4lod::object_lod_level_index(8U) == 1U);
    assert(f4lod::object_lod_level_index(16U) == 2U);
    assert(f4lod::object_lod_level_index(32U) == 3U);
    assert(!f4lod::object_lod_level_index(2U).has_value());

    const auto origin = f4lod::object_lod_cell_from_world(0.0F, 4095.0F);
    assert(origin.has_value());
    assert(origin->x == 0);
    assert(origin->y == 0);

    const auto positive = f4lod::object_lod_cell_from_world(4096.0F, 8192.0F);
    assert(positive.has_value());
    assert(positive->x == 1);
    assert(positive->y == 2);

    const auto negative = f4lod::object_lod_cell_from_world(-0.25F, -4096.25F);
    assert(negative.has_value());
    assert(negative->x == -1);
    assert(negative->y == -2);

    assert(!f4lod::object_lod_cell_from_world(
        std::numeric_limits<float>::infinity(), 0.0F).has_value());
    assert(!f4lod::object_lod_cell_from_world(
        std::numeric_limits<float>::quiet_NaN(), 0.0F).has_value());

    const f4lod::ObjectLodCell southwest{-96, -96};
    const auto same_block = f4lod::object_lod_block_for_cell({-95, -93}, southwest, 4U);
    assert(same_block.has_value());
    assert(same_block->x == -96);
    assert(same_block->y == -96);
    const auto next_block = f4lod::object_lod_block_for_cell({-92, -88}, southwest, 4U);
    assert(next_block.has_value());
    assert(next_block->x == -92);
    assert(next_block->y == -88);
    const auto below_anchor = f4lod::object_lod_block_for_cell({-97, -100}, southwest, 4U);
    assert(below_anchor.has_value());
    assert(below_anchor->x == -96);
    assert(below_anchor->y == -100);
    assert(!f4lod::object_lod_block_for_cell({0, 0}, {0, 0}, 0U).has_value());

    f4lod::ObjectLodReferencePolicyInput policy;
    auto policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(policy_result.include);
    assert(!policy_result.export_enable_parent);

    policy = {};
    policy.deleted = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(!policy_result.include);
    assert(policy_result.decision == ObjectLodReferenceDecision::skip_deleted);

    policy = {};
    policy.persistent = true;
    policy.base_marker = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(!policy_result.include);
    assert(policy_result.decision == ObjectLodReferenceDecision::skip_marker);

    policy = {};
    policy.initially_disabled = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(!policy_result.include);
    assert(policy_result.decision == ObjectLodReferenceDecision::skip_initially_disabled);

    policy = {};
    policy.initially_disabled = true;
    policy.reference_visible_when_distant = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(policy_result.include);

    policy = {};
    policy.initially_disabled = true;
    policy.has_enable_parent = true;
    policy.enable_parent_target_present = true;
    policy.enable_parent_respects_lod_state = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(policy_result.include);
    assert(policy_result.export_enable_parent);

    policy = {};
    policy.has_enable_parent = true;
    policy.enable_parent_target_present = true;
    policy.enable_parent_initially_disabled = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(!policy_result.include);
    assert(policy_result.decision == ObjectLodReferenceDecision::skip_enable_parent_disabled);

    policy.enable_parent_opposite_state = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(policy_result.include);
    assert(!policy_result.export_enable_parent);

    policy = {};
    policy.has_enable_parent = true;
    policy.enable_parent_target_present = true;
    policy.enable_parent_initially_disabled = true;
    policy.reference_visible_when_distant = true;
    policy_result = f4lod::evaluate_object_lod_reference_policy(policy);
    assert(policy_result.include);

    assert(std::string(f4lod::object_lod_reference_decision_name(
        ObjectLodReferenceDecision::skip_enable_parent_disabled)) ==
        "skip-enable-parent-disabled");

    f4lod::ObjectLodDescriptor descriptor;
    descriptor.authored_lod_models[0] = "meshes/lod/example_lod_0.nif";
    descriptor.authored_lod_models[2] = "meshes/lod/example_lod_2.nif";
    assert(f4lod::object_lod_has_authored_model(descriptor, 4U));
    assert(!f4lod::object_lod_has_authored_model(descriptor, 8U));
    assert(f4lod::object_lod_has_authored_model(descriptor, 16U));
    assert(!f4lod::object_lod_has_authored_model(descriptor, 64U));

    assert(f4lod::classify_object_lod_safety(true, true, false, false) ==
           ObjectLodSafety::safe_static);
    assert(f4lod::classify_object_lod_safety(true, true, false, true) ==
           ObjectLodSafety::enable_state_sensitive);
    assert(f4lod::classify_object_lod_safety(true, true, true, true) ==
           ObjectLodSafety::potentially_stateful);
    assert(f4lod::classify_object_lod_safety(true, false, false, false) ==
           ObjectLodSafety::missing_authored_lod);
    assert(f4lod::classify_object_lod_safety(false, true, false, false) ==
           ObjectLodSafety::unsupported_asset_material);
    assert(f4lod::classify_object_lod_safety(false, false, true, true, true) ==
           ObjectLodSafety::forced);

    assert(std::string(f4lod::object_lod_safety_name(ObjectLodSafety::safe_static)) ==
           "safe-static");
    assert(std::string(f4lod::object_lod_safety_name(ObjectLodSafety::enable_state_sensitive)) ==
           "enable-state-sensitive");

    f4lod::ObjectLodDescriptor safe;
    safe.authored_lod_models[0] = "meshes/lod/safe.nif";

    f4lod::ObjectLodDescriptor missing;

    f4lod::ObjectLodDescriptor enable_sensitive;
    enable_sensitive.authored_lod_models[0] = "meshes/lod/enable.nif";
    enable_sensitive.enable_parent = f4lod::FormKey{"Fallout4.esm", 0x1234U};

    f4lod::ObjectLodDescriptor stateful;
    stateful.authored_lod_models[0] = "meshes/lod/stateful.nif";
    stateful.safety = ObjectLodSafety::potentially_stateful;

    f4lod::ObjectLodDescriptor unsupported;
    unsupported.authored_lod_models[0] = "meshes/lod/unsupported.nif";
    unsupported.safety = ObjectLodSafety::unsupported_asset_material;
    unsupported.exclusion_reason = "fixture unsupported material";

    f4lod::ObjectLodDescriptor forced;
    forced.safety = ObjectLodSafety::forced;

    const std::vector<f4lod::ObjectLodDescriptor> candidates{
        safe,
        missing,
        enable_sensitive,
        stateful,
        unsupported,
        forced,
    };
    const auto preflight = f4lod::preflight_object_lod_descriptors(candidates);
    assert(preflight.counts.total == 6U);
    assert(preflight.counts.included == 2U);
    assert(preflight.counts.excluded == 4U);
    assert(preflight.counts.safe_static == 1U);
    assert(preflight.counts.missing_authored_lod == 1U);
    assert(preflight.counts.enable_state_sensitive == 1U);
    assert(preflight.counts.potentially_stateful == 1U);
    assert(preflight.counts.unsupported_asset_material == 1U);
    assert(preflight.counts.forced == 1U);
    assert(preflight.diagnostics.size() == 5U);

    bool saw_missing = false;
    bool saw_enable = false;
    bool saw_fixture_reason = false;
    for (const auto& diagnostic : preflight.diagnostics) {
        if (diagnostic.code == "missing-authored-lod") saw_missing = true;
        if (diagnostic.code == "enable-state-sensitive") saw_enable = true;
        if (diagnostic.message == "fixture unsupported material") saw_fixture_reason = true;
    }
    assert(saw_missing);
    assert(saw_enable);
    assert(saw_fixture_reason);

    return 0;
}
