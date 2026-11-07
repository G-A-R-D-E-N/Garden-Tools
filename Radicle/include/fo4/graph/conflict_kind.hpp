#pragma once

#include "fo4/graph/conflict_graph.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
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

struct FieldConflict {
  std::string path;
  ConflictCategory category{ConflictCategory::unknown};
  std::vector<ConflictRole> roles;
};

struct RecordConflict {
  FormKey key;
  ConflictCategory category{ConflictCategory::unknown};
  std::vector<ConflictRole> roles;
  std::vector<SubrecordConflict> subrecords;
  std::vector<FieldConflict> fields;
};

std::string_view conflict_category_name(ConflictCategory category) noexcept;
std::string_view conflict_role_name(ConflictRole role) noexcept;

struct ConflictQuery {
  std::span<const Plugin* const> load_order;
  FormKey key;
  ConflictPriority record_priority{ConflictPriority::normal};
  std::span<const ConflictPriorityRule> priorities{};
};

struct FieldValueGroup {
  std::vector<PluginKey> plugins;
};

struct FieldComparison {
  Signature signature{};
  std::size_t ordinal{0};
  std::vector<FieldValueGroup> groups;
};

struct RecordComparison {
  FormKey key;
  std::vector<FieldValueGroup> record_groups;
  std::vector<FieldComparison> fields;
};

RecordComparison compare_record_across_plugins(
    const ConflictGraph& graph,
    std::span<const Plugin* const> load_order,
    const FormKey& key,
    std::span<const ConflictPriorityRule> priorities = {});

std::vector<RecordComparison> compare_conflicts_across_plugins(
    const ConflictGraph& graph,
    std::span<const Plugin* const> load_order,
    std::span<const ConflictPriorityRule> priorities = {});

RecordConflict classify_record_conflict(const ConflictGraph& graph, const ConflictQuery& query);

std::vector<RecordConflict> classify_conflicts(const ConflictGraph& graph,
                                               std::span<const Plugin* const> load_order,
                                               std::span<const ConflictPriorityRule> priorities = {});

}  // namespace fo4
