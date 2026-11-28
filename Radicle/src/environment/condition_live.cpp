#include "fo4/environment/condition_live.hpp"

#include <array>
#include <utility>

namespace fo4::env {
namespace {

constexpr std::array<std::pair<std::string_view, ConditionFact>, 18> k_facts{{
    {"GetCurrentTime", ConditionFact::current_time},
    {"GetDead", ConditionFact::is_dead},
    {"GetLevel", ConditionFact::level},
    {"GetIsID", ConditionFact::is_id},
    {"GetIsRace", ConditionFact::is_race},
    {"GetInFaction", ConditionFact::in_faction},
    {"GetFactionRank", ConditionFact::faction_rank},
    {"HasKeyword", ConditionFact::has_keyword},
    {"GetGlobalValue", ConditionFact::global_value},
    {"GetValue", ConditionFact::actor_value},
    {"GetStage", ConditionFact::quest_stage},
    {"GetStageDone", ConditionFact::quest_stage_done},
    {"GetQuestRunning", ConditionFact::quest_running},
    {"GetQuestCompleted", ConditionFact::quest_completed},
    {"GetIsVoiceType", ConditionFact::is_voice_type},
    {"GetIsSex", ConditionFact::is_sex},
    {"GetItemCount", ConditionFact::item_count},
    {"GetIsAliasRef", ConditionFact::is_alias_ref},
}};

}  // namespace

std::optional<ConditionFact> condition_fact(std::string_view function_name) {
  for (const auto& [name, fact] : k_facts) {
    if (name == function_name) return fact;
  }
  return std::nullopt;
}

bool condition_fact_needs_form(ConditionFact fact) {
  return fact != ConditionFact::current_time && fact != ConditionFact::is_dead && fact != ConditionFact::level &&
         fact != ConditionFact::is_sex && fact != ConditionFact::is_alias_ref;
}

}  // namespace fo4::env
