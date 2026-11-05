#pragma once

#include "fo4/graph/check_errors.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4 {

struct DialogueCheckContext {
  struct Winner {
    Signature signature{};
    PluginKey plugin;
  };

  struct InfoCycle {
    FormKey record;
    PluginKey plugin;
    std::string chain;
  };

  std::map<FormKey, Winner> winners;
  std::map<FormKey, std::optional<FormKey>> topics;
  std::map<FormKey, std::size_t> info_counts;
  std::map<FormKey, FormKey> branch_quests;
  std::vector<InfoCycle> info_cycles;
};

DialogueCheckContext make_dialogue_check_context(std::span<const Plugin* const> load_order);

std::vector<CheckFinding> check_dialogue(const Plugin& plugin,
                                         const DialogueCheckContext& context);

}  // namespace fo4
