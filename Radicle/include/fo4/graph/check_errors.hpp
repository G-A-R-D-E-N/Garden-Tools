#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {

enum class CheckCode {
  invalid_master_index,
  unresolved_link,
  wrong_target_type,
  deleted_target,
  circular_leveled_list,
  schema_preflight,
  branch_starting_topic_not_dial,
  topic_children_not_dial,
  deleted_record_content,
  partial_form_content,
  light_object_id_overflow,
  internal_form_id_mismatch,
  invalid_condition,
  injected_record,
  unsupported_vmad_property,
  results_truncated,
  dialogue_info_count_mismatch,
  dialogue_previous_info_cycle,
  dialogue_previous_info_topic_mismatch,
  dialogue_branch_quest_mismatch,
  dialogue_branch_category_invalid,
  scene_parent_quest_missing,
  scene_action_topic_not_dial,
  scene_action_phase_out_of_range,
  scene_action_alias_unknown,
  scene_action_index_order,
};

enum class CheckSeverity {
  warning,
  error,
};

struct CheckFinding {
  CheckCode code{CheckCode::unresolved_link};
  CheckSeverity severity{CheckSeverity::error};
  PluginKey plugin;
  FormKey record;
  Signature subrecord{};
  std::size_t offset{0};
  FormKey target;
  std::string message;
  std::string detail;
};

struct CheckOptions {
  std::vector<PluginKey> plugins;
  std::size_t limit{0};
};

struct CheckRunSummary {
  bool truncated{false};
};

struct QuestCheckRequest {
  PluginKey plugin;
  FormKey quest;
  std::size_t limit{0};
};

std::string_view check_code_name(CheckCode code) noexcept;
std::string_view check_severity_name(CheckSeverity severity) noexcept;

std::vector<std::vector<FormKey>> find_leveled_list_cycles(
    std::span<const Plugin* const> load_order);

std::vector<CheckFinding> check_load_order(std::span<const Plugin* const> load_order,
                                           const CheckOptions& options = {},
                                           CheckRunSummary* summary = nullptr);

std::vector<CheckFinding> check_quest(std::span<const Plugin* const> load_order,
                                      const QuestCheckRequest& request,
                                      CheckRunSummary* summary = nullptr);

}  // namespace fo4
