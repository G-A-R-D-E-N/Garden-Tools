#include "c_api_forms_internal.hpp"
#include "fo4/c_api_gameplay.h"
#include "fo4/forms/faction.hpp"
#include "fo4/forms/gameplay_rules.hpp"
#include "fo4/forms/npc.hpp"
#include "fo4/forms/npc_gameplay.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

struct GameplayHolder {
  radicle_npc_gameplay view{};
  TextStore text;
  std::vector<radicle_form_key> template_chain;
  std::vector<radicle_form_key> spells;
  std::vector<radicle_npc_faction_membership> factions;
  std::vector<radicle_npc_perk_rank> perks;
  std::vector<const char*> stubs;
};

struct InventoryHolder {
  radicle_npc_inventory view{};
  std::vector<radicle_npc_inventory_item> items;
};

radicle_status require_decodable_record(const fo4::Record& record) {
  try {
    fo4::require_decodable(record);
  } catch (const std::invalid_argument& error) {
    return fail(RADICLE_STATUS_PARSE_ERROR, error.what());
  }
  return RADICLE_STATUS_OK;
}

template <class Fn>
radicle_status gameplay_guarded(const char* what, Fn&& fn) {
  return guarded(what, [&]() -> radicle_status {
    try {
      return fn();
    } catch (const fo4::forms::GameplayError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    } catch (const fo4::forms::NpcError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    } catch (const fo4::forms::FactionError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    } catch (const fo4::env::EnvironmentError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

radicle_status check_key(const radicle_env* env,
                         radicle_form_key c_key,
                         fo4::Signature signature,
                         std::optional<fo4::FormKey>& out) {
  const auto key = to_form_key(c_key);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record key is required");
  const auto* ref = env->value.winner(*key);
  if (ref == nullptr || ref->record->header.flags.is_deleted()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "record is not in this environment");
  }
  if (ref->record->header.signature != signature) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record has the wrong signature");
  }
  if (const auto status = require_decodable_record(*ref->record); status != RADICLE_STATUS_OK) return status;
  out = key;
  return RADICLE_STATUS_OK;
}

radicle_status require_readable(const radicle_record* record, const char* signature) {
  const auto& source = *to_ref(record)->record;
  if (source.header.signature != fo4::Signature::from_string(signature)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record has the wrong signature");
  }
  return require_decodable_record(source);
}

radicle_npc_template_source to_c_template_source(const radicle_env& env, const fo4::forms::NpcTemplateSource& source) {
  radicle_npc_template_source view{};
  view.source = to_c_key(env, source.source);
  view.reason = static_cast<radicle_npc_template_reason>(static_cast<int>(source.reason));
  view.hops = source.hops;
  view.has_blocker = static_cast<uint8_t>(source.blocker ? 1 : 0);
  view.blocker = to_c_key(env, source.blocker);
  return view;
}

}  // namespace

extern "C" {

radicle_status radicle_npc_gameplay_decode(const radicle_env* env,
                                           const radicle_record* npc,
                                           radicle_npc_gameplay** out_gameplay) {
  clear_error();
  if (out_gameplay != nullptr) *out_gameplay = nullptr;
  if (const auto status = require_env_record(env, npc, out_gameplay); status != RADICLE_STATUS_OK) {
    return status;
  }
  const auto& ref = *to_ref(npc);
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  return gameplay_guarded("unknown exception while decoding NPC_ gameplay", [&] {
    const auto data = fo4::forms::decode_npc_gameplay(env->value, ref);
    auto holder = std::make_unique<GameplayHolder>();
    auto& view = holder->view;
    view.npc = to_c_key(*env, data.npc);
    view.configuration_flags = data.configuration_flags;
    view.level_raw = data.level_raw;
    view.calc_min_level = data.calc_min_level;
    view.calc_max_level = data.calc_max_level;
    view.template_flags = data.template_flags;
    view.level_is_multiplier = static_cast<uint8_t>(data.level_is_multiplier ? 1 : 0);
    view.has_race = static_cast<uint8_t>(data.race ? 1 : 0);
    view.race = to_c_key(*env, data.race);
    view.has_class = static_cast<uint8_t>(data.class_form ? 1 : 0);
    view.class_form = to_c_key(*env, data.class_form);
    view.has_combat_style = static_cast<uint8_t>(data.combat_style ? 1 : 0);
    view.combat_style = to_c_key(*env, data.combat_style);
    view.has_crime_faction = static_cast<uint8_t>(data.crime_faction ? 1 : 0);
    view.crime_faction = to_c_key(*env, data.crime_faction);
    view.has_template = static_cast<uint8_t>(data.template_base ? 1 : 0);
    view.template_base = to_c_key(*env, data.template_base);
    for (const auto& key : data.template_chain) holder->template_chain.push_back(to_c_key(*env, key));
    for (const auto& key : data.spells) holder->spells.push_back(to_c_key(*env, key));
    for (const auto& faction : data.factions) {
    holder->factions.push_back({to_c_key(*env, faction.faction), static_cast<int32_t>(faction.rank)});
    }
    for (const auto& perk : data.perks) holder->perks.push_back({to_c_key(*env, perk.perk), perk.rank});
    view.has_ai_data = static_cast<uint8_t>(data.ai ? 1 : 0);
    if (data.ai) {
    view.ai.aggression = data.ai->aggression;
    view.ai.confidence = data.ai->confidence;
    view.ai.energy_level = data.ai->energy_level;
    view.ai.morality = data.ai->morality;
    view.ai.mood = data.ai->mood;
    view.ai.assistance = data.ai->assistance;
    }
    view.has_calculated_stats = static_cast<uint8_t>(data.calculated ? 1 : 0);
    if (data.calculated) {
    view.calculated_health = data.calculated->health;
    view.calculated_action_points = data.calculated->action_points;
    }
    for (const auto& stub : data.stubs) holder->stubs.push_back(holder->text.keep(std::string_view(stub)));
    view.template_chain = holder->template_chain.empty() ? nullptr : holder->template_chain.data();
    view.template_chain_count = holder->template_chain.size();
    view.spells = holder->spells.empty() ? nullptr : holder->spells.data();
    view.spell_count = holder->spells.size();
    view.factions = holder->factions.empty() ? nullptr : holder->factions.data();
    view.faction_count = holder->factions.size();
    view.perks = holder->perks.empty() ? nullptr : holder->perks.data();
    view.perk_count = holder->perks.size();
    view.stubs = holder->stubs.empty() ? nullptr : holder->stubs.data();
    view.stub_count = holder->stubs.size();
    view.internal = holder.get();
    *out_gameplay = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_npc_gameplay_free(radicle_npc_gameplay* gameplay) {
  clear_error();
  if (gameplay == nullptr) return;
  delete static_cast<GameplayHolder*>(gameplay->internal);
}

radicle_status radicle_npc_inventory_decode(const radicle_env* env,
                                            const radicle_record* npc,
                                            radicle_npc_inventory** out_inventory) {
  clear_error();
  if (out_inventory != nullptr) *out_inventory = nullptr;
  if (const auto status = require_env_record(env, npc, out_inventory); status != RADICLE_STATUS_OK) {
    return status;
  }
  const auto& ref = *to_ref(npc);
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  return gameplay_guarded("unknown exception while decoding NPC_ inventory", [&] {
    const auto items = fo4::forms::decode_npc_inventory(env->value.plugin(ref.plugin), ref.record->form_key);
    auto holder = std::make_unique<InventoryHolder>();
    for (const auto& item : items) {
      radicle_npc_inventory_item row{};
      row.item = to_c_key(*env, item.item);
      row.count = item.count;
      if (item.extra_data) {
        row.has_extra = 1;
        row.has_owner = static_cast<uint8_t>(item.extra_data->owner ? 1 : 0);
        row.owner = to_c_key(*env, item.extra_data->owner);
        row.global_or_rank = item.extra_data->global_or_rank;
        row.condition = item.extra_data->condition;
      }
      holder->items.push_back(row);
    }
    holder->view.items = holder->items.empty() ? nullptr : holder->items.data();
    holder->view.item_count = holder->items.size();
    holder->view.internal = holder.get();
    *out_inventory = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_npc_inventory_free(radicle_npc_inventory* inventory) {
  clear_error();
  if (inventory == nullptr) return;
  delete static_cast<InventoryHolder*>(inventory->internal);
}

radicle_status radicle_npc_template_source_resolve(const radicle_env* env,
                                                   const radicle_record* npc,
                                                   uint32_t slot,
                                                   radicle_npc_template_source* out) {
  clear_error();
  if (const auto status = require_env_record(env, npc, out); status != RADICLE_STATUS_OK) return status;
  *out = radicle_npc_template_source{};
  if (slot > static_cast<uint32_t>(fo4::forms::NpcTemplateSlot::keywords)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "slot is not a template slot");
  }
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(npc);
  return gameplay_guarded("unknown exception while resolving an NPC_ template slot", [&] {
    const auto result = fo4::forms::resolve_npc_template_source(
        env->value, ref, static_cast<fo4::forms::NpcTemplateSlot>(slot));
    *out = to_c_template_source(*env, result);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_npc_package_list(const radicle_env* env,
                                        const radicle_record* npc,
                                        radicle_form_key** out_keys,
                                        size_t* out_count,
                                        radicle_npc_template_source* out_reason) {
  clear_error();
  if (out_keys == nullptr || out_count == nullptr || out_reason == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_keys, out_count and out_reason are required");
  }
  *out_keys = nullptr;
  *out_count = 0;
  *out_reason = radicle_npc_template_source{};
  if (const auto status = require_env_record(env, npc, out_keys); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(npc);
  return gameplay_guarded("unknown exception while listing NPC_ packages", [&] {
    const auto result = fo4::forms::npc_package_list(env->value, ref);
    *out_reason = to_c_template_source(*env, result.source);
    const bool decided = result.source.reason == fo4::forms::NpcTemplateReason::own ||
                         result.source.reason == fo4::forms::NpcTemplateReason::template_npc;
    if (!decided) return RADICLE_STATUS_OK;
    std::vector<radicle_form_key> keys;
    for (const auto& key : result.keys) keys.push_back(to_c_key(*env, key));
    static constexpr radicle_c_detail::CopyMessages messages{"out_keys and out_count are required",
                                                              "failed to allocate the package key array"};
    return radicle_c_detail::copy_out(keys, out_keys, out_count, messages);
  });
}

radicle_status radicle_glob_value(const radicle_env* env,
                                  const radicle_record* glob,
                                  uint8_t constant_only,
                                  double* out_value,
                                  radicle_glob_reason* out_reason) {
  clear_error();
  if (out_value == nullptr || out_reason == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_value and out_reason are required");
  }
  *out_value = 0.0;
  *out_reason = RADICLE_GLOB_REASON_NO_VALUE;
  if (const auto status = require_env_record(env, glob, out_value); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(glob);
  if (const auto status = require_decodable_record(*ref.record); status != RADICLE_STATUS_OK) return status;
  return gameplay_guarded("unknown exception while reading a GLOB value", [&] {
    const auto result = fo4::forms::read_global_value(ref, constant_only != 0);
    *out_reason = static_cast<radicle_glob_reason>(static_cast<int>(result.reason));
    if (result.reason == fo4::forms::GlobalValueReason::ok) *out_value = result.value;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_npc_voice_type(const radicle_env* env,
                                      const radicle_record* npc,
                                      radicle_form_key* out_voice,
                                      radicle_npc_template_source* out_reason) {
  clear_error();
  if (out_voice == nullptr || out_reason == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_voice and out_reason are required");
  }
  *out_voice = radicle_form_key{};
  *out_reason = radicle_npc_template_source{};
  if (const auto status = require_env_record(env, npc, out_voice); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(npc);
  return gameplay_guarded("unknown exception while resolving an NPC_ voice type", [&] {
    const auto result = fo4::forms::resolve_npc_voice_type(env->value, ref);
    *out_reason = to_c_template_source(*env, result.origin);
    *out_voice = to_c_key(*env, result.voice);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_actor_level_for_spawn(const radicle_env* env,
                                             const radicle_record* npc,
                                             uint32_t player_level,
                                             int32_t level_modifier,
                                             const radicle_form_key* encounter_zone,
                                             radicle_actor_level* out) {
  clear_error();
  if (const auto status = require_env_record(env, npc, out); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  std::optional<fo4::FormKey> zone;
  if (encounter_zone != nullptr) {
    if (const auto status = check_key(env, *encounter_zone, fo4::Signature::from_string("ECZN"), zone);
        status != RADICLE_STATUS_OK) {
      return status;
    }
  }
  const auto& ref = *to_ref(npc);
  return gameplay_guarded("unknown exception while computing spawn level", [&] {
    const auto level =
      fo4::forms::actor_level_for_spawn(env->value, ref, player_level, level_modifier, zone);
    out->level = level.level;
    out->raw_level = level.raw_level;
    out->reason = static_cast<radicle_level_reason>(static_cast<int>(level.reason));
    out->zone_level = level.zone_level;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_faction_reaction(const radicle_env* env,
                                        const radicle_form_key* actor_factions,
                                        size_t actor_faction_count,
                                        const radicle_form_key* target_factions,
                                        size_t target_faction_count,
                                        radicle_faction_reaction_result* out) {
  clear_error();
  if (env == nullptr || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and an output are required");
  }
  if ((actor_factions == nullptr && actor_faction_count != 0) ||
      (target_factions == nullptr && target_faction_count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "faction arrays are required when counts are non-zero");
  }
  std::vector<fo4::FormKey> actors;
  for (size_t i = 0; i < actor_faction_count; ++i) {
    std::optional<fo4::FormKey> key;
    if (const auto status = check_key(env, actor_factions[i], fo4::Signature::from_string("FACT"), key);
        status != RADICLE_STATUS_OK) {
      return status;
    }
    actors.push_back(*key);
  }
  std::vector<fo4::FormKey> targets;
  for (size_t i = 0; i < target_faction_count; ++i) {
    std::optional<fo4::FormKey> key;
    if (const auto status = check_key(env, target_factions[i], fo4::Signature::from_string("FACT"), key);
        status != RADICLE_STATUS_OK) {
      return status;
    }
    targets.push_back(*key);
  }
  return gameplay_guarded("unknown exception while resolving faction reaction", [&] {
    const auto result = fo4::forms::faction_reaction(env->value, actors, targets);
    out->reaction = static_cast<radicle_combat_reaction>(static_cast<int>(result.reaction));
    out->reason = static_cast<radicle_reaction_reason>(static_cast<int>(result.reason));
    out->has_decider = static_cast<uint8_t>(result.decider_actor_faction ? 1 : 0);
    out->decider_actor_faction = to_c_key(*env, result.decider_actor_faction);
    out->decider_target_faction = to_c_key(*env, result.decider_target_faction);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_actor_value_base(const radicle_env* env,
                                        const radicle_record* npc,
                                        radicle_form_key avif,
                                        radicle_actor_value_base_result* out) {
  clear_error();
  if (const auto status = require_env_record(env, npc, out); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  std::optional<fo4::FormKey> key;
  if (const auto status = check_key(env, avif, fo4::Signature::from_string("AVIF"), key);
      status != RADICLE_STATUS_OK) {
    return status;
  }
  const auto& ref = *to_ref(npc);
  return gameplay_guarded("unknown exception while reading actor value base", [&] {
    const auto result = fo4::forms::actor_value_base(env->value, ref, *key);
    out->base = result.base;
    out->source = static_cast<radicle_av_source>(static_cast<int>(result.source));
    out->evidence = static_cast<radicle_av_evidence>(static_cast<int>(result.evidence));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_combat_profile_decode(const radicle_env* env,
                                             const radicle_record* npc,
                                             radicle_combat_profile* out) {
  clear_error();
  if (const auto status = require_env_record(env, npc, out); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_readable(npc, "NPC_"); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(npc);
  return gameplay_guarded("unknown exception while decoding combat profile", [&] {
    const auto profile = fo4::forms::combat_profile(env->value, ref);
    out->npc = to_c_key(*env, profile.npc);
    out->has_style = static_cast<uint8_t>(profile.style ? 1 : 0);
    out->style = to_c_key(*env, profile.style);
    out->source = static_cast<radicle_combat_style_source>(static_cast<int>(profile.source));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_crime_rules_decode(const radicle_env* env,
                                          const radicle_record* faction,
                                          radicle_crime_rules* out) {
  clear_error();
  if (const auto status = require_env_record(env, faction, out); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_readable(faction, "FACT"); status != RADICLE_STATUS_OK) return status;
  const auto& ref = *to_ref(faction);
  return gameplay_guarded("unknown exception while decoding crime rules", [&] {
    const auto rules = fo4::forms::decode_crime_rules(env->value, ref);
    out->faction = to_c_key(*env, rules.faction);
    out->has_crime_values = static_cast<uint8_t>(rules.values ? 1 : 0);
    if (rules.values) {
    out->arrest = rules.values->arrest;
    out->attack_on_sight = rules.values->attack_on_sight;
    out->murder = rules.values->murder;
    out->assault = rules.values->assault;
    out->trespass = rules.values->trespass;
    out->pickpocket = rules.values->pickpocket;
    out->unknown = rules.values->unknown;
    }
    out->has_steal_multiplier = static_cast<uint8_t>(rules.steal_multiplier ? 1 : 0);
    out->steal_multiplier = rules.steal_multiplier.value_or(0.0f);
    out->has_escape = static_cast<uint8_t>(rules.escape ? 1 : 0);
    out->escape = rules.escape.value_or(0);
    out->has_werewolf = static_cast<uint8_t>(rules.werewolf ? 1 : 0);
    out->werewolf = rules.werewolf.value_or(0);
    out->has_shared_crime_list = static_cast<uint8_t>(rules.shared_crime_list ? 1 : 0);
    out->shared_crime_list = to_c_key(*env, rules.shared_crime_list);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_ownership_check(const radicle_env* env,
                                       radicle_form_key owner,
                                       radicle_form_key actor,
                                       const radicle_npc_faction_membership* actor_factions,
                                       size_t actor_faction_count,
                                       int32_t required_rank,
                                       uint8_t* allowed) {
  clear_error();
  if (env == nullptr || allowed == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and an output are required");
  }
  if (actor_factions == nullptr && actor_faction_count != 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "actor_factions is required when the count is non-zero");
  }
  const auto owner_key = to_form_key(owner);
  const auto actor_key = to_form_key(actor);
  if (!owner_key || !actor_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "valid owner and actor keys are required");
  std::vector<fo4::forms::FactionMembership> memberships;
  for (size_t i = 0; i < actor_faction_count; ++i) {
    const auto key = to_form_key(actor_factions[i].faction);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid faction key is required");
    memberships.push_back({*key, actor_factions[i].rank});
  }
  return gameplay_guarded("unknown exception while checking ownership", [&] {
    *allowed = static_cast<uint8_t>(
      fo4::forms::ownership_allowed(env->value, *owner_key, *actor_key, memberships, required_rank) ? 1 : 0);
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
