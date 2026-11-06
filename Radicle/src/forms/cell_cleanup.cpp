#include "fo4/forms/cell_edit.hpp"

#include "fo4/environment/placed.hpp"
#include "fo4/signature.hpp"

#include <cmath>
#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");

struct DedupeToken {
  std::string base;
  float scale;
  float x;
  float y;
  float z;
  float rx;
  float ry;
  float rz;
  auto operator<=>(const DedupeToken&) const = default;
};

std::string base_key(const fo4::env::Environment& env, const std::optional<FormKey>& base, bool by_model) {
  const auto* record = base ? env.winner(*base) : nullptr;
  if (record == nullptr) return {};
  if (by_model) {
    if (const auto model = fo4::env::primary_model_path(*record)) return std::string(*model);
  }
  const auto editor_id = env.metadata(*record).editor_id;
  return editor_id ? std::string(*editor_id) : base->to_string();
}

DedupeToken dedupe_token(const fo4::env::Environment& env,
                         const fo4::env::PlacedReferenceData& data,
                         bool by_model) {
  const auto position = data.position.value_or(fo4::env::Vec3{});
  const auto rotation = data.rotation.value_or(fo4::env::Vec3{});
  return {base_key(env, data.base, by_model), data.scale.value_or(1.0f),
          std::nearbyint(position[0]), std::nearbyint(position[1]), std::nearbyint(position[2]),
          std::nearbyint(rotation[0]), std::nearbyint(rotation[1]), std::nearbyint(rotation[2])};
}

bool is_special(const fo4::env::Environment& env,
                const fo4::env::RecordRef& ref,
                const fo4::env::PlacedReferenceData& data) {
  const auto editor_id = env.metadata(ref).editor_id;
  return (editor_id && !editor_id->empty()) || data.scripts.has_value() ||
         data.enable_parent.has_value() || data.teleport.has_value() ||
         !data.linked_references.empty() || !data.patrols.empty() || !data.activate_parents.empty();
}

bool accepted_delete(DeleteStatus status) {
  return status == DeleteStatus::deleted || status == DeleteStatus::marked_deleted ||
         status == DeleteStatus::already_deleted;
}

}  // namespace

CleanupPlan plan_cell_cleanup(const fo4::env::Environment& env,
                              const FormKey& cell,
                              CleanupMode mode,
                              std::size_t max_count,
                              bool by_model) {
  CleanupPlan plan;
  plan.cell = cell;
  const auto* winner = env.winner(cell);
  if (winner == nullptr) {
    plan.status = CleanupPlanStatus::cell_missing;
    return plan;
  }
  if (winner->record->header.signature != k_cell) {
    plan.status = CleanupPlanStatus::not_a_cell;
    return plan;
  }
  if (winner->record->header.flags.is_deleted()) {
    plan.status = CleanupPlanStatus::cell_deleted;
    return plan;
  }
  plan.status = CleanupPlanStatus::planned;

  const auto children = env.cell_children(cell);
  if (mode == CleanupMode::excess) {
    std::size_t kept = 0;
    for (const auto& child : children) {
      const auto& record = *child.ref->record;
      if (!fo4::is_placed_reference_signature(record.header.signature)) continue;
      if (record.header.flags.is_persistent()) continue;
      if (kept++ < max_count) continue;
      plan.entries.push_back({record.form_key, {}, CleanupReason::excess});
    }
    return plan;
  }

  struct Kept {
    FormKey reference;
    bool special;
  };
  std::map<DedupeToken, Kept> seen;
  for (const auto& child : children) {
    const auto& ref = *child.ref;
    if (!fo4::is_placed_reference_signature(ref.record->header.signature)) continue;
    const auto& key = ref.record->form_key;
    const auto data = fo4::env::decode_placed_reference(env, ref);
    const bool special = is_special(env, ref, data);
    const auto [slot, inserted] =
        seen.try_emplace(dedupe_token(env, data, by_model), Kept{key, special});
    if (inserted) continue;
    auto& existing = slot->second;
    if (existing.special && special) continue;
    if (existing.special) {
      plan.entries.push_back({key, existing.reference, CleanupReason::duplicate_of_special});
      continue;
    }
    if (special) {
      plan.entries.push_back({existing.reference, key, CleanupReason::duplicate_of_special});
      existing = Kept{key, true};
      continue;
    }
    plan.entries.push_back({key, existing.reference, CleanupReason::duplicate});
  }
  return plan;
}

CleanupApplyResult apply_cell_cleanup(Plugin& patch,
                                      const fo4::env::Environment& env,
                                      const CleanupPlan& plan) {
  CleanupApplyResult result;
  if (plan.status != CleanupPlanStatus::planned) return result;

  auto next = patch;
  bool accepted = true;
  for (const auto& entry : plan.entries) {
    const auto status = delete_placed_reference(next, env, entry.reference).status;
    result.entry_status.push_back(status);
    accepted = accepted && accepted_delete(status);
  }
  if (!accepted) return result;

  if (!plan.entries.empty()) {
    next.original_range.clear();
    patch = std::move(next);
  }
  result.status = CleanupApplyStatus::applied;
  return result;
}

}  // namespace fo4::forms
