#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/formid.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace fo4::forms {

class GameplayError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class CombatReaction : std::uint8_t {
  neutral = 0,
  enemy = 1,
  ally = 2,
  friendly = 3,
  unknown = 4,
};

enum class ReactionReason : std::uint8_t {
  relation = 0,
  no_relation = 1,
  conflict = 2,
};

enum class LevelReason : std::uint8_t {
  fixed = 0,
  level_mult_unproven = 1,
  template_stats_unproven = 2,
  xlcm_unproven = 3,
  zone_unproven = 4,
};

enum class AvSource : std::uint8_t {
  none = 0,
  calculated_health = 1,
  calculated_action_points = 2,
  stored_property = 3,
};

enum class AvEvidence : std::uint8_t {
  record_stored = 0,
  unproven = 1,
};

struct FactionMembership {
  FormKey faction{};
  std::int32_t rank{0};

  friend bool operator==(const FactionMembership&, const FactionMembership&) = default;
};

struct ActorLevel {
  std::uint32_t level{0};
  std::uint32_t raw_level{0};
  LevelReason reason{LevelReason::fixed};
  std::uint32_t zone_level{0};
};

struct FactionReaction {
  CombatReaction reaction{CombatReaction::neutral};
  ReactionReason reason{ReactionReason::no_relation};
  std::optional<FormKey> decider_actor_faction;
  std::optional<FormKey> decider_target_faction;
};

struct ActorValueBase {
  std::int32_t base{0};
  AvSource source{AvSource::none};
  AvEvidence evidence{AvEvidence::unproven};
};

enum class CombatStyleSource : std::uint8_t {
  own_record = 0,
  absent_fallback_unproven = 1,
};

struct CombatProfile {
  FormKey npc{};
  std::optional<FormKey> style;
  CombatStyleSource source{CombatStyleSource::own_record};
};

struct CrimeValues {
  std::uint8_t arrest{0};
  std::uint8_t attack_on_sight{0};
  std::uint16_t murder{0};
  std::uint16_t assault{0};
  std::uint16_t trespass{0};
  std::uint16_t pickpocket{0};
  std::uint16_t unknown{0};

  friend bool operator==(const CrimeValues&, const CrimeValues&) = default;
};

struct CrimeRules {
  FormKey faction{};
  std::optional<CrimeValues> values;
  std::optional<float> steal_multiplier;
  std::optional<std::uint16_t> escape;
  std::optional<std::uint16_t> werewolf;
  std::optional<FormKey> shared_crime_list;
};

ActorLevel actor_level_for_spawn(const env::Environment& environment,
                                 const env::RecordRef& npc,
                                 std::uint32_t player_level,
                                 std::int32_t level_modifier,
                                 const std::optional<FormKey>& encounter_zone);

FactionReaction faction_reaction(const env::Environment& environment,
                                 std::span<const FormKey> actor_factions,
                                 std::span<const FormKey> target_factions);

ActorValueBase actor_value_base(const env::Environment& environment,
                                const env::RecordRef& npc,
                                const FormKey& avif);

CombatProfile combat_profile(const env::Environment& environment, const env::RecordRef& npc);

CrimeRules decode_crime_rules(const env::Environment& environment, const env::RecordRef& faction);

bool ownership_allowed(const env::Environment& environment,
                       const FormKey& owner,
                       const FormKey& actor,
                       std::span<const FactionMembership> actor_factions,
                       std::int32_t required_rank);

}  // namespace fo4::forms
