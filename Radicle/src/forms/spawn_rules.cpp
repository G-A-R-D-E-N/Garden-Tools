#include "fo4/forms/spawn_rules.hpp"

#include "npc_visual_detail.hpp"
#include "fo4/flags.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace fo4::forms {
namespace {

using env::Environment;
using env::RecordRef;

constexpr std::uint8_t k_xesp_opposite = 0x01;
// Constraint: ECZN DATA is 12 bytes; Min Level is an s8 at byte 9 (xEdit 93cc0bc5 Core/wbDefinitionsFO4.pas:8022-8037).
constexpr std::size_t k_eczn_data_size = 12;
constexpr std::size_t k_eczn_min_level_offset = 9;

EnableLink link_from(const env::PlacedReferenceData& placed, const RecordRef& ref) {
  EnableLink link;
  link.initially_disabled = ref.record->header.flags.test(RecordFlags::initially_disabled);
  if (placed.enable_parent) {
    link.parent = placed.enable_parent->reference;
    link.opposite = (placed.enable_parent->flags.value_or(0) & k_xesp_opposite) != 0;
  }
  return link;
}

SpawnBaseKind kind_of_signature(Signature signature) {
  if (signature == sig_npc_) return SpawnBaseKind::npc;
  if (signature == sig_lvln) return SpawnBaseKind::leveled_npc;
  if (signature == sig_lvli) return SpawnBaseKind::leveled_item;
  return SpawnBaseKind::other;
}

std::optional<std::int32_t> zone_min_level_of(const Environment& environment, const FormKey& zone) {
  const auto* ref = detail::winner_of(environment, zone);
  if (ref == nullptr || ref->record->header.signature != sig_eczn) return std::nullopt;
  for (const auto& sub : ref->record->subrecords) {
    if (sub.signature != sig_data) continue;
    if (sub.data.size() < k_eczn_data_size) return std::nullopt;
    const auto raw = std::to_integer<std::uint8_t>(sub.data[k_eczn_min_level_offset]);
    return static_cast<std::int32_t>(static_cast<std::int8_t>(raw));
  }
  return std::nullopt;
}

// Constraint: an NPC_ base is itself; a leveled base resolves to its first NPC_ winner at the level.
std::optional<FormKey> resolve_base(const Environment& environment,
                                    const FormKey& base,
                                    SpawnBaseKind kind,
                                    std::uint16_t level,
                                    std::uint64_t seed) {
  if (kind == SpawnBaseKind::npc) return base;
  if (kind != SpawnBaseKind::leveled_npc) return std::nullopt;
  for (const auto& entry : pick_leveled(environment, base, level, seed).entries) {
    if (spawn_base_kind_of(environment, entry) == SpawnBaseKind::npc) return entry;
  }
  return std::nullopt;
}

}  // namespace

EnableVerdict evaluate_enable_state(const FormKey& ref,
                                    const EnableLink& start,
                                    const EnableLinkLookup& lookup,
                                    const EnableOverrideLookup& overrides,
                                    std::size_t max_depth) {
  // Constraint: check order (override, root, depth, cycle, missing) mirrors EnableState.InitiallyEnabled.
  std::vector<FormKey> seen{ref};
  EnableVerdict verdict;
  verdict.deciding = ref;
  bool flip = false;
  FormKey current = ref;
  EnableLink link = start;
  for (;;) {
    verdict.deciding = current;
    if (overrides) {
      if (const auto forced = overrides(current)) {
        verdict.enabled = flip != *forced;
        verdict.stop = EnableStop::overridden;
        return verdict;
      }
    }
    if (!link.parent) {
      verdict.enabled = flip != !link.initially_disabled;
      verdict.stop = EnableStop::root;
      return verdict;
    }
    if (seen.size() > max_depth) {
      verdict.enabled = flip != !link.initially_disabled;
      verdict.stop = EnableStop::depth_limit;
      return verdict;
    }
    if (std::find(seen.begin(), seen.end(), *link.parent) != seen.end()) {
      verdict.enabled = flip != !link.initially_disabled;
      verdict.stop = EnableStop::cycle;
      return verdict;
    }
    seen.push_back(*link.parent);
    const auto parent = lookup(*link.parent);
    if (!parent) {
      verdict.enabled = flip != !link.initially_disabled;
      verdict.stop = EnableStop::missing_parent;
      return verdict;
    }
    flip = flip != link.opposite;
    current = *link.parent;
    link = *parent;
    ++verdict.chain_length;
  }
}

std::optional<EnableLink> enable_link_of(const Environment& environment, const FormKey& key) {
  const auto* ref = detail::winner_of(environment, key);
  if (ref == nullptr || !env::is_placed_reference_signature(ref->record->header.signature)) {
    return std::nullopt;
  }
  try {
    return link_from(env::decode_placed_reference(environment, *ref), *ref);
  } catch (const env::EnvironmentError&) {
    return std::nullopt;
  }
}

SpawnBaseKind spawn_base_kind_of(const Environment& environment, const FormKey& base) {
  const auto* ref = detail::winner_of(environment, base);
  if (ref == nullptr) return SpawnBaseKind::none;
  return kind_of_signature(ref->record->header.signature);
}

PlacedActorSpawn decode_placed_actor_spawn(const Environment& environment,
                                           const RecordRef& placed,
                                           std::uint16_t level,
                                           std::uint64_t seed,
                                           const EnableOverrideLookup& overrides) {
  const auto data = env::decode_placed_reference(environment, placed);
  PlacedActorSpawn out;
  out.actor_reference = placed.record->header.signature == sig_achr;
  out.base = data.base;
  if (data.base) {
    out.base_kind = spawn_base_kind_of(environment, *data.base);
    if (out.base_kind == SpawnBaseKind::none) {
      detail::add_stub(&out.stubs, "placed base " + data.base->to_string() + " is not a live record");
    }
    out.resolved_base = resolve_base(environment, *data.base, out.base_kind, level, seed);
    if (out.base_kind == SpawnBaseKind::leveled_npc && !out.resolved_base) {
      detail::add_stub(&out.stubs, "placed base " + data.base->to_string() + " picks no NPC at level " +
                                       std::to_string(level));
    }
  }
  out.count = data.count;
  out.level_modifier = data.level_modifier;
  out.encounter_zone = data.encounter_zone;
  if (data.encounter_zone) out.encounter_zone_min_level = zone_min_level_of(environment, *data.encounter_zone);
  out.persistent = placed.record->header.flags.test(RecordFlags::persistent);
  out.initially_disabled = placed.record->header.flags.test(RecordFlags::initially_disabled);
  const auto lookup = [&environment](const FormKey& key) { return enable_link_of(environment, key); };
  out.enable = evaluate_enable_state(placed.record->form_key, link_from(data, placed), lookup, overrides);
  if (out.enable.stop == EnableStop::cycle) {
    detail::add_stub(&out.stubs, "enable chain of " + placed.record->form_key.to_string() + " loops; stopped");
  } else if (out.enable.stop == EnableStop::depth_limit) {
    detail::add_stub(&out.stubs, "enable chain of " + placed.record->form_key.to_string() + " passes " +
                                     std::to_string(k_enable_max_depth) + " links; stopped");
  }
  return out;
}

SpawnPlan plan_script_spawn(const Environment& environment,
                            const FormKey& base,
                            std::uint16_t level,
                            std::uint64_t seed,
                            std::optional<std::int32_t> level_modifier,
                            std::vector<std::string>* stubs) {
  SpawnPlan plan;
  plan.base = base;
  plan.level_modifier = level_modifier;
  plan.base_kind = spawn_base_kind_of(environment, base);
  switch (plan.base_kind) {
    case SpawnBaseKind::none:
      plan.reason = LeveledPickReason::list_missing;
      detail::add_stub(stubs, "spawn base " + base.to_string() + " is not a live record");
      break;
    case SpawnBaseKind::npc:
      plan.entries.push_back(base);
      plan.reason = LeveledPickReason::picked;
      break;
    case SpawnBaseKind::leveled_npc:
    case SpawnBaseKind::leveled_item: {
      auto picked = pick_leveled(environment, base, level, seed, stubs);
      plan.entries = std::move(picked.entries);
      plan.reason = picked.reason;
      plan.depth_used = picked.depth_used;
      break;
    }
    case SpawnBaseKind::other:
      plan.reason = LeveledPickReason::not_spawnable;
      break;
  }
  return plan;
}

}  // namespace fo4::forms
