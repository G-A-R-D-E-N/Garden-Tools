#include "fo4/forms/gameplay_rules.hpp"

#include "fo4/forms/faction.hpp"
#include "gameplay_detail.hpp"
#include "npc_visual_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::forms {
namespace {

using env::Environment;
using env::RecordRef;
using detail::ascii_lower;
using detail::winner_of;
using gameplay_detail::acbs_of;
using gameplay_detail::first_subrecord;
using gameplay_detail::link_at;
using gameplay_detail::link_of;
using gameplay_detail::read_at;

constexpr std::uint32_t k_level_mult_flag = 0x80;
constexpr std::uint16_t k_template_stats = 0x2;
constexpr std::uint8_t k_zone_match_pc_below_min = 0x02;
constexpr float k_level_mult_scale = 0.001f;
constexpr float k_zone_level_slope = 0.6f;
constexpr std::size_t k_acbs_calc_min = 8;
constexpr std::size_t k_acbs_calc_max = 10;
constexpr std::size_t k_eczn_data_size = 12;
constexpr std::size_t k_eczn_min_level = 9;
constexpr std::size_t k_eczn_flags = 10;
constexpr std::size_t k_eczn_max_level = 11;
constexpr std::size_t k_acbs_template_flags = 14;
constexpr std::size_t k_crva_min_size = 12;
constexpr std::size_t k_crva_steal_size = 16;
constexpr std::size_t k_crva_escape_size = 18;
constexpr std::size_t k_crva_werewolf_size = 20;
constexpr std::size_t k_prps_entry_size = 8;
constexpr float k_int32_float_limit = 2147483648.0f;
const Signature sig_prps = Signature::from_string("PRPS");

bool is_special_actor_value(const std::string& lowered_editor_id) {
  static constexpr std::string_view names[] = {"strength", "perception", "endurance", "charisma",
                                               "intelligence", "agility", "luck"};
  return std::find(std::begin(names), std::end(names), lowered_editor_id) != std::end(names);
}

// Requires: PRPS entries are {u32 AVIF FormID, f32 value}; the first entry resolving to `avif` in record order wins.
std::optional<float> stored_property(const Environment& environment, const RecordRef& npc, const FormKey& avif) {
  for (const auto& subrecord : npc.record->subrecords) {
    if (subrecord.signature != sig_prps) continue;
    if (subrecord.data.size() % k_prps_entry_size != 0) {
      throw GameplayError(npc.record->form_key.to_string() + " PRPS must be a whole number of 8-byte entries");
    }
    for (std::size_t offset = 0; offset < subrecord.data.size(); offset += k_prps_entry_size) {
      const auto key = link_at(environment, npc, subrecord, offset);
      if (key && *key == avif) return read_at<float>(subrecord, offset + 4);
    }
  }
  return std::nullopt;
}

void require_npc(const RecordRef& ref) {
  if (ref.record->header.signature != sig_npc_) {
    throw GameplayError(ref.record->form_key.to_string() + " is not an NPC_");
  }
}

const RecordRef& require_record(const Environment& environment, const FormKey& key, Signature signature) {
  const auto* ref = winner_of(environment, key);
  if (ref == nullptr) throw GameplayError(key.to_string() + " is not a live record");
  if (ref->record->header.signature != signature) {
    throw GameplayError(key.to_string() + " is not a " + signature.to_string());
  }
  return *ref;
}

CombatReaction to_reaction(FactionCombatReaction value) {
  return static_cast<CombatReaction>(static_cast<std::uint8_t>(value));
}

std::uint16_t clamp_u16(std::uint32_t value) {
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(value, 0xFFFFu));
}

std::uint32_t zone_level_of(const Environment& environment, const FormKey& zone_key, std::uint32_t player_level) {
  const auto& zone = require_record(environment, zone_key, sig_eczn);
  const auto* data = first_subrecord(*zone.record, sig_data);
  if (data == nullptr || data->data.size() < k_eczn_data_size) {
    throw GameplayError(zone_key.to_string() + " DATA must be at least 12 bytes");
  }
  const auto low = read_at<std::uint8_t>(*data, k_eczn_min_level);
  const auto flags = read_at<std::uint8_t>(*data, k_eczn_flags);
  const auto high = read_at<std::uint8_t>(*data, k_eczn_max_level);
  const std::uint32_t player = clamp_u16(player_level);
  std::uint32_t level = player;
  if (player < low) {
    if ((flags & k_zone_match_pc_below_min) == 0) level = low;
  } else {
    const float scaled = static_cast<float>(player - low) * k_zone_level_slope + static_cast<float>(low);
    level = static_cast<std::uint32_t>(scaled) & 0xFFFFu;
  }
  if (high != 0 && high < level) level = high;
  return level;
}

}  // namespace

ActorLevel actor_level_for_spawn(const Environment& environment,
                                 const RecordRef& npc,
                                 std::uint32_t player_level,
                                 std::int32_t level_modifier,
                                 const std::optional<FormKey>& encounter_zone) {
  require_npc(npc);
  const auto& acbs = acbs_of(*npc.record);

  ActorLevel out;
  out.raw_level = read_at<std::uint16_t>(acbs, 6);
  const auto flags = read_at<std::uint32_t>(acbs, 0);
  const auto template_flags = read_at<std::uint16_t>(acbs, k_acbs_template_flags);
  if (encounter_zone.has_value()) out.zone_level = zone_level_of(environment, *encounter_zone, player_level);

  if ((template_flags & k_template_stats) != 0) {
    out.reason = LevelReason::template_stats_unproven;
    return out;
  }
  if (level_modifier != 0) {
    out.reason = LevelReason::xlcm_unproven;
    return out;
  }
  out.level = out.raw_level;
  if ((flags & k_level_mult_flag) != 0) {
    const float product = static_cast<float>(clamp_u16(player_level)) * static_cast<float>(out.raw_level) *
                          k_level_mult_scale;
    out.level = static_cast<std::uint32_t>(product) & 0xFFFFu;
    const auto calc_min = read_at<std::uint16_t>(acbs, k_acbs_calc_min);
    const auto calc_max = read_at<std::uint16_t>(acbs, k_acbs_calc_max);
    if (calc_min != 0 && out.level < calc_min) {
      out.level = calc_min;
    } else if (calc_max != 0 && calc_max < out.level) {
      out.level = calc_max;
    }
  }
  out.reason = LevelReason::fixed;
  return out;
}

FactionReaction faction_reaction(const Environment& environment,
                                 std::span<const FormKey> actor_factions,
                                 std::span<const FormKey> target_factions) {
  for (const auto& target : target_factions) require_record(environment, target, sig_fact);

  struct Pair {
    std::int32_t modifier{0};
    CombatReaction reaction{CombatReaction::neutral};
    FormKey actor{};
    FormKey target{};
  };
  std::optional<Pair> shared;
  std::optional<Pair> cross;
  for (const auto& actor : actor_factions) {
    const auto& ref = require_record(environment, actor, sig_fact);
    const auto relations =
        decode_faction_relations(environment.plugin(ref.plugin), ref.record->form_key);
    for (const auto& target : target_factions) {
      const auto match = std::find_if(relations.begin(), relations.end(),
                                      [&](const FactionRelation& relation) { return relation.faction == target; });
      if (actor == target) {
        Pair pair{0, CombatReaction::neutral, actor, target};
        if (match != relations.end()) {
          pair.modifier = match->modifier;
          pair.reaction = to_reaction(match->reaction);
        }
        if (!shared || pair.modifier > shared->modifier) shared = pair;
        continue;
      }
      for (const auto& relation : relations) {
        if (relation.faction != target) continue;
        const Pair pair{relation.modifier, to_reaction(relation.reaction), actor, target};
        if (!cross || pair.modifier < cross->modifier) cross = pair;
      }
    }
  }

  FactionReaction out;
  const auto& chosen = shared ? shared : cross;
  if (!chosen) {
    out.reaction = CombatReaction::neutral;
    out.reason = ReactionReason::no_relation;
    return out;
  }
  out.reaction = chosen->reaction;
  out.reason = ReactionReason::relation;
  out.decider_actor_faction = chosen->actor;
  out.decider_target_faction = chosen->target;
  return out;
}

ActorValueBase actor_value_base(const Environment& environment,
                                const RecordRef& npc,
                                const FormKey& avif) {
  require_npc(npc);
  const auto& avif_ref = require_record(environment, avif, sig_avif);

  ActorValueBase out;
  const auto edid = editor_id_of(*avif_ref.record);
  if (!edid) return out;

  const auto name = ascii_lower(*edid);
  if (is_special_actor_value(name)) {
    const auto stored = stored_property(environment, npc, avif);
    if (!stored) return out;
    out.source = AvSource::stored_property;
    const auto& special_acbs = acbs_of(*npc.record);
    const auto special_template_flags = read_at<std::uint16_t>(special_acbs, k_acbs_template_flags);
    if ((special_template_flags & k_template_stats) != 0 || !std::isfinite(*stored) ||
        std::fabs(*stored) >= k_int32_float_limit) {
      out.evidence = AvEvidence::unproven;
      return out;
    }
    out.base = static_cast<std::int32_t>(std::lround(*stored));
    out.evidence = AvEvidence::record_stored;
    return out;
  }
  AvSource source = AvSource::none;
  if (name == "health") {
    source = AvSource::calculated_health;
  } else if (name == "actionpoints") {
    source = AvSource::calculated_action_points;
  }
  if (source == AvSource::none) return out;

  const auto* dnam = first_subrecord(*npc.record, sig_dnam);
  if (dnam == nullptr) return out;
  out.source = source;
  const auto& acbs = acbs_of(*npc.record);
  const auto flags = read_at<std::uint32_t>(acbs, 0);
  const auto template_flags = read_at<std::uint16_t>(acbs, k_acbs_template_flags);
  if ((template_flags & k_template_stats) != 0 || (flags & k_level_mult_flag) != 0) {
    out.evidence = AvEvidence::unproven;
    return out;
  }
  const auto health = read_at<std::uint16_t>(*dnam, 0);
  const auto action_points = read_at<std::uint16_t>(*dnam, 2);
  out.base = static_cast<std::int32_t>(source == AvSource::calculated_health ? health : action_points);
  out.evidence = AvEvidence::record_stored;
  return out;
}

CombatProfile combat_profile(const Environment& environment, const RecordRef& npc) {
  require_npc(npc);
  CombatProfile out;
  out.npc = npc.record->form_key;
  out.style = link_of(environment, npc, sig_znam);
  out.source = out.style ? CombatStyleSource::own_record : CombatStyleSource::absent_fallback_unproven;
  return out;
}

CrimeRules decode_crime_rules(const Environment& environment, const RecordRef& faction) {
  if (faction.record->header.signature != sig_fact) {
    throw GameplayError(faction.record->form_key.to_string() + " is not a FACT");
  }
  CrimeRules out;
  out.faction = faction.record->form_key;

  if (const auto* crva = first_subrecord(*faction.record, sig_crva)) {
    if (crva->data.size() < k_crva_min_size) {
      throw GameplayError(faction.record->form_key.to_string() + " CRVA must be at least 12 bytes");
    }
    CrimeValues values;
    values.arrest = read_at<std::uint8_t>(*crva, 0);
    values.attack_on_sight = read_at<std::uint8_t>(*crva, 1);
    values.murder = read_at<std::uint16_t>(*crva, 2);
    values.assault = read_at<std::uint16_t>(*crva, 4);
    values.trespass = read_at<std::uint16_t>(*crva, 6);
    values.pickpocket = read_at<std::uint16_t>(*crva, 8);
    values.unknown = read_at<std::uint16_t>(*crva, 10);
    out.values = values;
    if (crva->data.size() >= k_crva_steal_size) out.steal_multiplier = read_at<float>(*crva, 12);
    if (crva->data.size() >= k_crva_escape_size) out.escape = read_at<std::uint16_t>(*crva, 16);
    if (crva->data.size() >= k_crva_werewolf_size) out.werewolf = read_at<std::uint16_t>(*crva, 18);
  }

  out.shared_crime_list = link_of(environment, faction, sig_crgr);
  return out;
}

bool ownership_allowed(const Environment& environment,
                       const FormKey& owner,
                       const FormKey& actor,
                       std::span<const FactionMembership> actor_factions,
                       std::int32_t required_rank) {
  if (owner == actor) return true;
  const auto* ref = winner_of(environment, owner);
  if (ref == nullptr || ref->record->header.signature != sig_fact) return false;
  const auto first = std::find_if(actor_factions.begin(), actor_factions.end(),
                                  [&](const FactionMembership& member) { return member.faction == owner; });
  return first != actor_factions.end() && first->rank >= required_rank;
}

}  // namespace fo4::forms
