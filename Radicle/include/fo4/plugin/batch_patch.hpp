#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4 {

inline constexpr std::size_t k_batch_default_limit = 5000;
inline constexpr std::size_t k_batch_max_errors = 20;

struct BatchOperationSpec {
  std::string op{"set"};
  std::string field;
  std::string value;
};

struct BatchError {
  std::string code;
  std::string message;
};

struct BatchPatchHost {
  std::function<std::optional<FormKey>(std::string_view)> resolve_value;
  std::function<std::optional<Signature>(const FormKey&)> winner_signature;
  std::function<void(Plugin&, const PluginKey&)> ensure_master;
};

enum class BatchOpKind { set_scalar, set_string, set_link, add_to_list, remove_from_list };

struct BatchOperation {
  BatchOpKind kind{BatchOpKind::set_scalar};
  std::string op;
  std::string field;
  std::string value;
  std::string stored;
  std::optional<schema::ScalarFieldDescriptor> scalar;
  std::optional<schema::FormLinkFieldDescriptor> link;
  std::optional<schema::FormLinkListDescriptor> list;
  std::size_t index{0};
  FormKey target{};
};

struct BatchPlan {
  Signature signature{};
  std::string type_text;
  std::vector<BatchOperation> operations;
  std::string filter_field;
  std::string filter_value;
  std::string filter_path;
  bool filter_editor_id{false};
  bool filtering{false};
};

struct BatchSelection {
  std::vector<const Record*> matched;
  std::size_t total{0};
};

struct BatchApplyResult {
  std::size_t done{0};
  std::size_t failed{0};
  std::size_t operations_applied{0};
  std::size_t inserted{0};
  bool changed{false};
  std::vector<std::string> errors;
  std::optional<Plugin> plugin;
};

std::string record_editor_id(const Record& record);

std::expected<BatchPlan, BatchError> plan_batch_patch(std::string_view type_text,
                                                      std::span<const BatchOperationSpec> operations,
                                                      std::string_view filter_field,
                                                      std::string_view filter_value,
                                                      const BatchPatchHost& host);

BatchSelection select_batch_records(const Plugin& source, const BatchPlan& plan, std::size_t limit);

BatchApplyResult apply_batch_patch(const Plugin& base,
                                   const Plugin& source,
                                   std::span<const Record* const> matched,
                                   const BatchPlan& plan,
                                   const BatchPatchHost& host);

}  // namespace fo4
