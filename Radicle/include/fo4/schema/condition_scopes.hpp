#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fo4::schema {

enum class ConditionScopeKind {
  effect,
  alias,
  package_branch,
  quest_target,
  perk_effect_tab,
  message_button,
  terminal_body_text,
  terminal_menu_item,
};

struct ConditionScopeDescriptor {
  Signature record{};
  std::string_view container;
  std::string_view member;
  ConditionScopeKind kind{ConditionScopeKind::effect};
};

#include "fo4/schema/generated_condition_scopes.inc"

struct ScopedConditionPath {
  ConditionScopeDescriptor descriptor;
  std::size_t container_index{0};
};

enum class ScopedConditionStatus {
  updated,
  unchanged,
  record_missing,
  record_mismatch,
  container_index_out_of_range,
  element_not_conditioned,
  alias_structure_invalid,
  multiple_lists,
  unrepresentable,
};

struct ScopedConditionsRead {
  bool ok{false};
  ScopedConditionStatus error{ScopedConditionStatus::container_index_out_of_range};
  std::vector<CtdaCondition> conditions;
};

enum class RootConditionStatus {
  ok,
  no_list,
  multiple_lists,
  alias_structure_invalid,
};

struct RootConditionWindow {
  RootConditionStatus status{RootConditionStatus::no_list};
  std::size_t begin{0};
  std::size_t end{0};
  std::optional<std::size_t> insert_anchor;
};

RootConditionWindow root_condition_window(const Record& record);

ScopedConditionsRead read_root_conditions(const Record& record);

// Resolves "<container>[i].<member>" for a record signature (e.g. SPEL +
// "Effects[2].Conditions"). Empty result: no descriptor for this record matches.
std::optional<ScopedConditionPath> scoped_condition_path(
    Signature record, std::string_view path) noexcept;

ScopedConditionsRead read_scoped_conditions(
    const Record& record,
    const ConditionScopeDescriptor& descriptor,
    std::size_t container_index);

ScopedConditionStatus set_record_scoped_conditions(
    Plugin& plugin,
    const FormKey& record_key,
    const ConditionScopeDescriptor& descriptor,
    std::size_t container_index,
    std::span<const CtdaCondition> conditions);

}  // namespace fo4::schema
