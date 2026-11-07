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

using DialogueTopics = std::map<FormKey, std::optional<FormKey>>;

DialogueCheckContext make_dialogue_check_context(std::span<const Plugin* const> load_order);

std::vector<CheckFinding> check_dialogue(const Plugin& plugin,
                                         const DialogueCheckContext& context);

std::vector<CheckFinding> check_dialogue_records(const Plugin& plugin,
                                                 std::span<const Record* const> records,
                                                 const DialogueTopics& topics,
                                                 const DialogueCheckContext& context);

std::optional<FormKey> first_link_target(const Record& record, Signature subrecord,
                                         const PluginFormIdContext& context);

bool scene_parent_is(const Record& record, const FormKey& quest,
                     const PluginFormIdContext& context);

}  // namespace fo4
