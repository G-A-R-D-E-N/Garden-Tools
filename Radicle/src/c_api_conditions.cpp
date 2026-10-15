#include "c_api_env_internal.hpp"
#include "fo4/c_api_conditions.h"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/text/encoding.hpp"

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status read_failure(fo4::schema::ScopedConditionStatus status) {
  switch (status) {
    case fo4::schema::ScopedConditionStatus::container_index_out_of_range:
      return fail(RADICLE_STATUS_NOT_FOUND, "condition scope index is out of range");
    case fo4::schema::ScopedConditionStatus::element_not_conditioned:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scope element cannot carry conditions");
    case fo4::schema::ScopedConditionStatus::multiple_lists:
      return fail(RADICLE_STATUS_PARSE_ERROR, "condition scope has more than one list");
    case fo4::schema::ScopedConditionStatus::unrepresentable:
      return fail(RADICLE_STATUS_PARSE_ERROR, "condition list cannot be read losslessly");
    default:
      return fail(RADICLE_STATUS_PARSE_ERROR, "condition scope structure is invalid");
  }
}

bool convert_text(const char* utf8, std::optional<std::string>& out) {
  if (utf8 == nullptr) return true;
  auto converted = fo4::text::utf8_to_windows1252(utf8);
  if (!converted) return false;
  out = std::move(*converted);
  return true;
}

bool to_ctda(const radicle_condition& row, fo4::CtdaCondition& out) {
  out.flags_byte = row.flags_byte;
  out.comparison_raw = row.comparison_raw;
  out.function = row.function;
  out.unknown2 = row.unknown2;
  out.param1 = row.param1;
  out.param2 = row.param2;
  out.run_on = row.run_on;
  out.reference = row.reference;
  out.unknown3 = row.unknown3;
  return convert_text(row.parameter1_string, out.parameter1_string) &&
         convert_text(row.parameter2_string, out.parameter2_string);
}

bool matches_current(const fo4::schema::ScopedConditionsRead& current,
                     std::span<const fo4::CtdaCondition> wanted) {
  if (!current.ok || current.conditions.size() != wanted.size()) return false;
  for (std::size_t i = 0; i < wanted.size(); ++i) {
    const auto& a = current.conditions[i];
    const auto& b = wanted[i];
    if (a.flags_byte != b.flags_byte || a.comparison_raw != b.comparison_raw ||
        a.function != b.function || a.unknown2 != b.unknown2 || a.param1 != b.param1 ||
        a.param2 != b.param2 || a.run_on != b.run_on || a.reference != b.reference ||
        a.unknown3 != b.unknown3 || a.parameter1_string != b.parameter1_string ||
        a.parameter2_string != b.parameter2_string) {
      return false;
    }
  }
  return true;
}

radicle_status set_root(fo4::Plugin& plugin, const fo4::FormKey& key,
                        std::span<const fo4::CtdaCondition> conditions,
                        radicle_condition_write_status& status) {
  fo4::assign_record_form_keys(plugin);
  const auto* found = fo4::find_record(plugin, key);
  if (found != nullptr && matches_current(fo4::schema::read_root_conditions(*found), conditions)) {
    status = RADICLE_CONDITION_UNCHANGED;
    return RADICLE_STATUS_OK;
  }
  switch (fo4::set_record_conditions(plugin, key, conditions)) {
    case fo4::ConditionMutationStatus::updated:
      status = RADICLE_CONDITION_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::ConditionMutationStatus::unchanged:
      status = RADICLE_CONDITION_UNCHANGED;
      return RADICLE_STATUS_OK;
    case fo4::ConditionMutationStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::ConditionMutationStatus::multiple_lists:
      status = RADICLE_CONDITION_MULTIPLE_LISTS;
      return RADICLE_STATUS_OK;
    case fo4::ConditionMutationStatus::no_condition_list:
      status = RADICLE_CONDITION_NO_LIST;
      return RADICLE_STATUS_OK;
    case fo4::ConditionMutationStatus::alias_structure_invalid:
      status = RADICLE_CONDITION_ALIAS_STRUCTURE_INVALID;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected condition mutation status");
}

radicle_status set_scoped(fo4::Plugin& plugin, const fo4::FormKey& key, const char* scope_path,
                           std::span<const fo4::CtdaCondition> conditions,
                           radicle_condition_write_status& status) {
  fo4::assign_record_form_keys(plugin);
  const auto* found = fo4::find_record(plugin, key);
  if (found == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
  const auto path = fo4::schema::scoped_condition_path(found->header.signature, scope_path);
  if (!path) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scope path does not name a condition list of this record type");
  }
  if (matches_current(fo4::schema::read_scoped_conditions(*found, path->descriptor, path->container_index),
                      conditions)) {
    status = RADICLE_CONDITION_UNCHANGED;
    return RADICLE_STATUS_OK;
  }
  switch (fo4::schema::set_record_scoped_conditions(plugin, key, path->descriptor,
                                                    path->container_index, conditions)) {
    case fo4::schema::ScopedConditionStatus::updated:
      status = RADICLE_CONDITION_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::unchanged:
      status = RADICLE_CONDITION_UNCHANGED;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::schema::ScopedConditionStatus::record_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type does not match the condition scope");
    case fo4::schema::ScopedConditionStatus::container_index_out_of_range:
      status = RADICLE_CONDITION_SCOPE_INDEX_OUT_OF_RANGE;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::element_not_conditioned:
      status = RADICLE_CONDITION_ELEMENT_NOT_CONDITIONED;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::alias_structure_invalid:
      status = RADICLE_CONDITION_ALIAS_STRUCTURE_INVALID;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::multiple_lists:
      status = RADICLE_CONDITION_MULTIPLE_LISTS;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::unrepresentable:
      status = RADICLE_CONDITION_UNREPRESENTABLE;
      return RADICLE_STATUS_OK;
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected scoped condition status");
}

}  // namespace

extern "C" {

radicle_status radicle_record_conditions(const radicle_env* env,
                                         const radicle_record* record,
                                         const char* scope_path,
                                         const radicle_condition** out_rows,
                                         size_t* out_count) {
  clear_error();
  if (env == nullptr || record == nullptr || out_rows == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, out_rows and out_count are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_rows = nullptr;
  *out_count = 0;
  const auto& source = *to_ref(record)->record;
  return guarded("unknown exception while reading conditions", [&] {
    fo4::schema::ScopedConditionsRead result;
    if (scope_path == nullptr || *scope_path == '\0') {
      result = fo4::schema::read_root_conditions(source);
    } else {
      const auto path = fo4::schema::scoped_condition_path(source.header.signature, scope_path);
      if (!path) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scope path does not name a condition list of this record type");
      }
      result = fo4::schema::read_scoped_conditions(source, path->descriptor, path->container_index);
    }
    if (!result.ok) return read_failure(result.error);

    const std::lock_guard lock(env->text_mutex);
    const auto text = [env](const std::optional<std::string>& value) -> const char* {
      if (!value) return nullptr;
      env->condition_text.push_back(fo4::text::windows1252_to_utf8(*value));
      return env->condition_text.back().c_str();
    };
    std::vector<radicle_condition> rows;
    rows.reserve(result.conditions.size());
    for (const auto& c : result.conditions) {
      rows.push_back({c.flags_byte, c.comparison_raw, c.function, c.unknown2, c.param1, c.param2,
                      c.run_on, c.reference, c.unknown3, text(c.parameter1_string),
                      text(c.parameter2_string)});
    }
    env->condition_rows.push_back(std::move(rows));
    *out_rows = env->condition_rows.back().data();
    *out_count = env->condition_rows.back().size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_conditions(radicle_plugin* plugin,
                                             radicle_form_key record,
                                             const char* scope_path,
                                             const radicle_condition* rows,
                                             size_t count,
                                             radicle_condition_write_status* out_status) {
  clear_error();
  if (plugin == nullptr || out_status == nullptr || (rows == nullptr && count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, out_status and rows are required");
  }
  const auto key = to_form_key(record);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record form ID is required");
  return guarded("unknown exception while setting conditions", [&] {
    std::vector<fo4::CtdaCondition> conditions(count);
    for (size_t i = 0; i < count; ++i) {
      if (!to_ctda(rows[i], conditions[i])) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "condition text is not representable in Windows-1252");
      }
    }
    const std::span<const fo4::CtdaCondition> view(conditions);
    if (scope_path == nullptr || *scope_path == '\0') {
      return set_root(plugin->value, *key, view, *out_status);
    }
    return set_scoped(plugin->value, *key, scope_path, view, *out_status);
  });
}

}
