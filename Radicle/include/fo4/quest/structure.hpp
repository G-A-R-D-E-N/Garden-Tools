#pragma once

#include "fo4/forms/quest.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fo4::quest {

enum class QuestScope {
  dialogue,
  story_manager,
  log_entry,
  objective_target,
  alias,
  stages,
  objectives,
  aliases,
};

struct QuestConditionList {
  QuestScope scope{QuestScope::dialogue};
  std::size_t primary{0};
  std::size_t secondary{0};
  bool supported{true};
  std::vector<CtdaCondition> conditions;
};

struct QuestUnsupported {
  QuestScope scope{QuestScope::dialogue};
  std::size_t primary{0};
  std::size_t secondary{0};
  std::size_t member{0};
  Signature signature{};
  std::string reason;
};

struct QuestStructure {
  bool stages_supported{false};
  bool objectives_supported{false};
  bool aliases_supported{false};
  forms::QuestStages stages;
  forms::QuestObjectives objectives;
  forms::QuestAliases aliases;
  std::vector<QuestConditionList> condition_lists;
  std::vector<QuestUnsupported> unsupported;
};

QuestStructure read_quest_structure(const Plugin& plugin, const FormKey& quest);

}
