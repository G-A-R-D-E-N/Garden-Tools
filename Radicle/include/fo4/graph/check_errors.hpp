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

std::string_view check_code_name(CheckCode code) noexcept;
std::string_view check_severity_name(CheckSeverity severity) noexcept;

std::vector<std::vector<FormKey>> find_leveled_list_cycles(
    std::span<const Plugin* const> load_order);

std::vector<CheckFinding> check_load_order(std::span<const Plugin* const> load_order,
                                           const CheckOptions& options = {});

}  // namespace fo4
