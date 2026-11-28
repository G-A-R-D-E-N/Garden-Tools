#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace fo4::env {

// Invariant: numeric values equal radicle_condition_fact in c_api_condition_live.h and are append-only.
enum class ConditionFact : std::uint8_t {
  current_time = 1,
  is_dead = 2,
  level = 3,
  is_id = 4,
  is_race = 5,
  in_faction = 6,
  faction_rank = 7,
  has_keyword = 8,
  global_value = 9,
  actor_value = 10,
  quest_stage = 11,
  quest_stage_done = 12,
  quest_running = 13,
  quest_completed = 14,
  is_voice_type = 15,
  is_sex = 16,
  item_count = 17,
  is_alias_ref = 18,
};

// Constraint: the name is the canonical CTDA function name from the schema table, compared exactly.
std::optional<ConditionFact> condition_fact(std::string_view function_name);

bool condition_fact_needs_form(ConditionFact fact);

}  // namespace fo4::env
