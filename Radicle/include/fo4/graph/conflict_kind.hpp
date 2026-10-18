#pragma once

#include "fo4/graph/conflict_graph.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fo4 {

enum class ConflictCategory {
  unknown,
  only_one,
  no_conflict,
  benign,
  override_,
  conflict,
  critical,
};

enum class ConflictRole {
  unknown,
  ignored,
  identical_to_master,
  only_one,
  master,
  benign,
  override_,
  identical_to_master_wins,
  wins,
  loses,
};

enum class ConflictPriority {
  ignore,
  benign_if_added,
  benign,
  override_,
  normal,
  critical,
};

struct ConflictPriorityRule {
  Signature record{};
  Signature subrecord{};
  ConflictPriority priority{ConflictPriority::normal};
};

struct SubrecordConflict {
  Signature signature{};
  std::size_t ordinal{0};
  ConflictCategory category{ConflictCategory::unknown};
  std::vector<ConflictRole> roles;
};

struct RecordConflict {
  FormKey key;
  ConflictCategory category{ConflictCategory::unknown};
  std::vector<ConflictRole> roles;
  std::vector<SubrecordConflict> subrecords;
};

struct ConflictQuery {
  std::span<const Plugin* const> load_order;
  FormKey key;
  ConflictPriority record_priority{ConflictPriority::normal};
  std::span<const ConflictPriorityRule> priorities{};
};

RecordConflict classify_record_conflict(const ConflictGraph& graph, const ConflictQuery& query);

std::vector<RecordConflict> classify_conflicts(const ConflictGraph& graph,
                                               std::span<const Plugin* const> load_order,
                                               std::span<const ConflictPriorityRule> priorities = {});

}  // namespace fo4
