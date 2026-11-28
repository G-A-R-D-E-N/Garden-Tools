#include "c_api_forms_internal.hpp"
#include "fo4/c_api_spawn.h"
#include "fo4/forms/spawn_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

// Constraint: only use-all (0x04) and calculate-for-each (0x02) may be overridden.
constexpr std::uint32_t k_flags_override_allowed = 0x06u;
// Constraint: the engine clamps a leveled spawn count at 0x7FFF.
constexpr std::uint32_t k_max_pick_count = 0x7FFFu;

struct LeveledPickHolder {
  radicle_leveled_pick_result view{};
  std::vector<radicle_form_key> entries;
};

struct LeveledCountHolder {
  radicle_leveled_pick_count_result view{};
  std::vector<radicle_form_key> entries;
  std::vector<uint16_t> counts;
};

struct SpawnPlanHolder {
  radicle_spawn_plan view{};
  std::vector<radicle_form_key> entries;
};

// Invariant: callers pass the load-order winner only.
radicle_status require_winner_record(const radicle_env* env, const radicle_record* record, const void* out) {
  if (const auto status = require_env_record(env, record, out); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(record);
  if (env->value.winner(ref.record->form_key) != &ref) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not the load-order winner");
  }
  return RADICLE_STATUS_OK;
}

radicle_status to_override_map(const radicle_ref_state_override* overrides,
                               size_t count,
                               std::map<fo4::FormKey, bool>& out) {
  if (count == 0) return RADICLE_STATUS_OK;
  if (overrides == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "overrides are required when override_count is set");
  }
  for (size_t i = 0; i < count; ++i) {
    const auto& item = overrides[i];
    if (item.enabled > 1) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "override enabled must be 0 or 1");
    const auto key = to_form_key(item.ref);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid override ref is required");
    if (!out.emplace(*key, item.enabled != 0).second) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "an override ref appears twice");
    }
  }
  return RADICLE_STATUS_OK;
}

fo4::forms::EnableOverrideLookup override_lookup(const std::map<fo4::FormKey, bool>& overrides) {
  return [&overrides](const fo4::FormKey& key) -> std::optional<bool> {
    const auto found = overrides.find(key);
    if (found == overrides.end()) return std::nullopt;
    return found->second;
  };
}

radicle_enable_verdict to_c_verdict(const radicle_env& env, const fo4::forms::EnableVerdict& verdict) {
  radicle_enable_verdict out{};
  out.enabled = static_cast<uint8_t>(verdict.enabled ? 1 : 0);
  out.stop = static_cast<uint8_t>(verdict.stop);
  out.chain_length = static_cast<uint32_t>(verdict.chain_length);
  out.deciding = to_c_key(env, verdict.deciding);
  return out;
}

}  // namespace

extern "C" {

radicle_status radicle_leveled_pick(const radicle_env* env,
                                    radicle_form_key list,
                                    uint32_t level,
                                    uint64_t seed,
                                    uint32_t flags_override_mask,
                                    radicle_leveled_pick_result** out_pick) {
  clear_error();
  if (env == nullptr || out_pick == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_pick are required");
  }
  *out_pick = nullptr;
  if (level == 0 || level > 0xFFFFu) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "level must be between 1 and 65535");
  }
  if ((flags_override_mask & ~k_flags_override_allowed) != 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "flags_override_mask may set only 0x02 and 0x04");
  }
  const auto key = to_form_key(list);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid list key is required");
  return guarded("unknown exception while picking a leveled entry", [&] {
    const auto pick = fo4::forms::pick_leveled(env->value, *key, static_cast<std::uint16_t>(level), seed,
                                               nullptr, static_cast<std::uint8_t>(flags_override_mask));
    auto holder = std::make_unique<LeveledPickHolder>();
    for (const auto& entry : pick.entries) holder->entries.push_back(to_c_key(*env, entry));
    holder->view.entries = holder->entries.empty() ? nullptr : holder->entries.data();
    holder->view.entry_count = holder->entries.size();
    holder->view.reason = static_cast<uint8_t>(pick.reason);
    holder->view.depth_used = static_cast<uint32_t>(pick.depth_used);
    holder->view.internal = holder.get();
    *out_pick = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_leveled_pick_result_free(radicle_leveled_pick_result* pick) {
  clear_error();
  if (pick == nullptr) return;
  delete static_cast<LeveledPickHolder*>(pick->internal);
}

radicle_status radicle_leveled_pick_count(const radicle_env* env,
                                          radicle_form_key list,
                                          uint32_t level,
                                          uint64_t seed,
                                          uint32_t flags_override_mask,
                                          uint32_t count,
                                          radicle_leveled_pick_count_result** out_pick) {
  clear_error();
  if (env == nullptr || out_pick == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_pick are required");
  }
  *out_pick = nullptr;
  if (level == 0 || level > 0xFFFFu) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "level must be between 1 and 65535");
  }
  if (count == 0 || count > k_max_pick_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "count must be between 1 and 32767");
  }
  if ((flags_override_mask & ~k_flags_override_allowed) != 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "flags_override_mask may set only 0x02 and 0x04");
  }
  const auto key = to_form_key(list);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid list key is required");
  return guarded("unknown exception while picking leveled entries", [&] {
    const auto pick = fo4::forms::pick_leveled_counted(env->value, *key, static_cast<std::uint16_t>(level), seed, count,
                                                       nullptr, static_cast<std::uint8_t>(flags_override_mask));
    auto holder = std::make_unique<LeveledCountHolder>();
    for (const auto& entry : pick.entries) holder->entries.push_back(to_c_key(*env, entry));
    holder->view.entries = holder->entries.empty() ? nullptr : holder->entries.data();
    holder->view.entry_count = holder->entries.size();
    holder->view.reason = static_cast<uint8_t>(pick.reason);
    holder->view.depth_used = static_cast<uint32_t>(pick.depth_used);
    holder->view.rolls = pick.rolls;
    holder->view.multiplier = pick.multiplier;
    holder->counts = pick.counts;
    holder->view.entry_counts = holder->counts.empty() ? nullptr : holder->counts.data();
    holder->view.internal = holder.get();
    *out_pick = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_leveled_pick_count_result_free(radicle_leveled_pick_count_result* pick) {
  clear_error();
  if (pick == nullptr) return;
  delete static_cast<LeveledCountHolder*>(pick->internal);
}

radicle_status radicle_ref_enable_state(const radicle_env* env,
                                        const radicle_record* ref,
                                        const radicle_ref_state_override* overrides,
                                        size_t override_count,
                                        radicle_enable_verdict* out_verdict) {
  clear_error();
  if (env == nullptr || ref == nullptr || out_verdict == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, ref and out_verdict are required");
  }
  if (const auto status = require_winner_record(env, ref, out_verdict); status != RADICLE_STATUS_OK) return status;
  const auto& record = *to_ref(ref);
  if (!fo4::env::is_placed_reference_signature(record.record->header.signature)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a placed reference");
  }
  std::map<fo4::FormKey, bool> state;
  if (const auto status = to_override_map(overrides, override_count, state); status != RADICLE_STATUS_OK) {
    return status;
  }
  return guarded("unknown exception while evaluating the enable state", [&] {
    const auto start = fo4::forms::enable_link_of(env->value, record.record->form_key);
    if (!start) return fail(RADICLE_STATUS_PARSE_ERROR, "placed reference is malformed");
    const auto lookup = [&env](const fo4::FormKey& key) { return fo4::forms::enable_link_of(env->value, key); };
    const auto verdict = fo4::forms::evaluate_enable_state(record.record->form_key, *start, lookup,
                                                           override_lookup(state));
    *out_verdict = to_c_verdict(*env, verdict);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_placed_actor_spawn_decode(const radicle_env* env,
                                                 const radicle_record* placed,
                                                 uint32_t player_level,
                                                 uint64_t seed,
                                                 const radicle_ref_state_override* overrides,
                                                 size_t override_count,
                                                 radicle_actor_spawn* out_spawn) {
  clear_error();
  if (env == nullptr || placed == nullptr || out_spawn == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, placed and out_spawn are required");
  }
  if (player_level == 0 || player_level > 0xFFFFu) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "player_level must be between 1 and 65535");
  }
  if (const auto status = require_winner_record(env, placed, out_spawn); status != RADICLE_STATUS_OK) return status;
  const auto& record = *to_ref(placed);
  if (!fo4::env::is_placed_reference_signature(record.record->header.signature)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a placed reference");
  }
  std::map<fo4::FormKey, bool> state;
  if (const auto status = to_override_map(overrides, override_count, state); status != RADICLE_STATUS_OK) {
    return status;
  }
  *out_spawn = radicle_actor_spawn{};
  return guarded("unknown exception while decoding the placed actor spawn", [&] {
    const auto data = fo4::forms::decode_placed_actor_spawn(env->value, record, static_cast<std::uint16_t>(player_level),
                                                            seed, override_lookup(state));
    out_spawn->has_base = static_cast<uint8_t>(data.base ? 1 : 0);
    out_spawn->base = to_c_key(*env, data.base);
    out_spawn->base_kind = static_cast<uint8_t>(data.base_kind);
    out_spawn->actor_reference = static_cast<uint8_t>(data.actor_reference ? 1 : 0);
    out_spawn->persistent = static_cast<uint8_t>(data.persistent ? 1 : 0);
    out_spawn->initially_disabled = static_cast<uint8_t>(data.initially_disabled ? 1 : 0);
    if (data.count) {
      out_spawn->has_count = 1;
      out_spawn->count = *data.count;
    }
    if (data.level_modifier) {
      out_spawn->has_level_modifier = 1;
      out_spawn->level_modifier = *data.level_modifier;
    }
    out_spawn->has_encounter_zone = static_cast<uint8_t>(data.encounter_zone ? 1 : 0);
    out_spawn->encounter_zone = to_c_key(*env, data.encounter_zone);
    out_spawn->enable = to_c_verdict(*env, data.enable);
    out_spawn->has_resolved_base = static_cast<uint8_t>(data.resolved_base ? 1 : 0);
    out_spawn->resolved_base = to_c_key(*env, data.resolved_base);
    if (data.encounter_zone_min_level) {
      out_spawn->has_zone_min_level = 1;
      out_spawn->zone_min_level = *data.encounter_zone_min_level;
    }
    // Constraint: the XLCM level rule is UNVERIFIED, so these stay zero.
    out_spawn->level_rule_verified = 0;
    out_spawn->resolved_level = 0;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_script_spawn_plan(const radicle_env* env,
                                         radicle_form_key base,
                                         uint32_t level,
                                         uint64_t seed,
                                         int32_t has_level_modifier,
                                         int32_t level_modifier,
                                         radicle_spawn_plan** out_plan) {
  clear_error();
  if (env == nullptr || out_plan == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_plan are required");
  }
  *out_plan = nullptr;
  if (level == 0 || level > 0xFFFFu) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "level must be between 1 and 65535");
  }
  if (has_level_modifier != 0 && has_level_modifier != 1) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "has_level_modifier must be 0 or 1");
  }
  const auto key = to_form_key(base);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid base key is required");
  return guarded("unknown exception while planning a script spawn", [&] {
    std::optional<std::int32_t> modifier;
    if (has_level_modifier == 1) modifier = level_modifier;
    const auto plan = fo4::forms::plan_script_spawn(env->value, *key, static_cast<std::uint16_t>(level), seed,
                                                    modifier);
    auto holder = std::make_unique<SpawnPlanHolder>();
    holder->view.base = to_c_key(*env, plan.base);
    holder->view.base_kind = static_cast<uint8_t>(plan.base_kind);
    holder->view.reason = static_cast<uint8_t>(plan.reason);
    holder->view.depth_used = static_cast<uint32_t>(plan.depth_used);
    for (const auto& entry : plan.entries) holder->entries.push_back(to_c_key(*env, entry));
    holder->view.entries = holder->entries.empty() ? nullptr : holder->entries.data();
    holder->view.entry_count = holder->entries.size();
    holder->view.has_level_modifier = modifier ? 1 : 0;
    holder->view.level_modifier = modifier.value_or(0);
    holder->view.internal = holder.get();
    *out_plan = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_spawn_plan_free(radicle_spawn_plan* plan) {
  clear_error();
  if (plan == nullptr) return;
  delete static_cast<SpawnPlanHolder*>(plan->internal);
}

}  // extern "C"
