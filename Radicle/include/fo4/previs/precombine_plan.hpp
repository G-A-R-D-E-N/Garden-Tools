#pragma once

#include "fo4/environment/cell_view.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/formid.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/previs/xcri.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::previs {

struct PrecombinePlanInstance {
  FormKey reference;
  std::optional<std::string> editor_id;
  forms::PositionRotation transform{};
  float scale{1.0f};
  // Constraint: a base object that clears occludes must not be used as an occluder box by the tome builder.
  bool occludes{true};
};

// Constraint: a shipped object's own triangle ratios are the exact target, keyed by model path.
struct PrecombineLodTarget {
  float level1{0.0f};
  float level2{0.0f};
};

// Constraint: the caller names targets by the model path the plan groups under, so lookup is case and separator free.
using PrecombineLodTargets = std::map<std::string, PrecombineLodTarget, std::less<>>;

struct PrecombineModelGroup {
  std::string model_path;
  std::vector<PrecombinePlanInstance> instances;
  // Constraint: the writer reads the target off the group it is emitting, so a pinned ratio survives material reordering.
  std::optional<PrecombineLodTarget> lod_target;
};

struct PrecombineSkipCounts {
  std::size_t scripted{0};
  std::size_t enable_parent{0};
};

struct PrecombinePlan {
  FormKey cell;
  std::string plugin;
  bool interior{false};
  std::size_t temporary_considered{0};
  std::vector<PrecombineModelGroup> groups;
  // Constraint: targets are reported with the plan, so a caller can see which objects were pinned.
  PrecombineLodTargets lod_targets;
  // Invariant: counts include only references that passed the STAT, model, ownership and not-disabled tests.
  PrecombineSkipCounts skipped;
};

std::optional<PrecombinePlan> build_precombine_plan(const env::Environment& env,
                                                      const FormKey& cell,
                                                      std::string_view plugin_filename,
                                                      std::size_t min_instances = 2,
                                                      const PrecombineLodTargets& lod_targets = {});

// Constraint: targets arrive from C++ and from a backend request, so one attach sets the plan and every group it pins.
void attach_lod_targets(PrecombinePlan& plan, const PrecombineLodTargets& lod_targets);

// Constraint: a request names a target by model path, so the JSON form is an object of path to two fractions.
PrecombineLodTargets lod_targets_from_json(const nlohmann::json& document);

// Constraint: mesh ids follow group order and each instance pairs its REFR with its group's OC id.
// XCRI reference words are file Form-IDs, so each REFR is encoded against dest's master list; a REFR
// whose owner dest does not declare must throw fo4::FormIdError.
XcriRaw xcri_from_plan(const PrecombinePlan& plan,
                       std::span<const std::uint32_t> oc_ids,
                       const PluginFormIdContext& dest);


// Constraint: the CK groups by shader material, so meshes of one material are emitted together.
struct PrecombineMaterialOrder {
  PrecombinePlan plan;
  // Constraint: parallel to plan.groups, the material each mesh was grouped under.
  std::vector<std::string> materials;
};

// Constraint: one XCRI mesh id per emitted _OC, so a material file contributing many objects is listed once.
XcriRaw xcri_from_group_ids(const PrecombinePlan& plan,
                            std::span<const std::uint32_t> group_ids,
                            const PluginFormIdContext& dest);

// Constraint: a model with no material groups under the empty key, last, and is not dropped.
PrecombineMaterialOrder order_plan_by_material(
    const PrecombinePlan& plan,
    const std::map<std::string, std::string, std::less<>>& material_by_model);

}  // namespace fo4::previs
