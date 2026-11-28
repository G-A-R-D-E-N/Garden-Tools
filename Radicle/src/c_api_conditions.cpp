#include "c_api_env_internal.hpp"
#include "fo4/c_api_conditions.h"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/condition_functions.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct ConditionListHolder {
  radicle_condition_list view{};
  std::deque<std::string> text;
  std::vector<radicle_condition> rows;
};

radicle_status unknown_scope() {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT,
              "scope path does not name a condition list of this record type");
}

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

const char* utf8_text(std::deque<std::string>& text, const std::optional<std::string>& value) {
  if (!value) return nullptr;
  text.push_back(fo4::text::windows1252_to_utf8(*value));
  return text.back().c_str();
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
  if (found != nullptr) {
    fo4::require_decodable(*found);
    if (matches_current(fo4::schema::read_root_conditions(*found), conditions)) {
      status = RADICLE_CONDITION_UNCHANGED;
      return RADICLE_STATUS_OK;
    }
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
  fo4::require_decodable(*found);
  const auto path = fo4::schema::scoped_condition_path(found->header.signature, scope_path);
  if (!path) return unknown_scope();
  const auto current =
      fo4::schema::read_scoped_conditions(*found, path->descriptor, path->container_index, path->inner_index);
  if (matches_current(current, conditions)) {
    status = RADICLE_CONDITION_UNCHANGED;
    return RADICLE_STATUS_OK;
  }
  if (current.ok && (path->descriptor.kind == fo4::schema::ConditionScopeKind::scene_phase_start ||
                     path->descriptor.kind == fo4::schema::ConditionScopeKind::scene_phase_completion ||
                     path->descriptor.kind == fo4::schema::ConditionScopeKind::scene_start_scene_conditions) &&
      std::any_of(current.conditions.begin(), current.conditions.end(), [](const auto& condition) {
        return std::any_of(condition.padding.begin(), condition.padding.end(),
                           [](std::byte value) { return value != std::byte{0}; });
      })) {
    status = RADICLE_CONDITION_UNREPRESENTABLE;
    return RADICLE_STATUS_OK;
  }
  switch (fo4::schema::set_record_scoped_conditions(plugin, key, path->descriptor,
                                                    path->container_index, conditions, path->inner_index)) {
    case fo4::schema::ScopedConditionStatus::updated:
      status = RADICLE_CONDITION_UPDATED;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::unchanged:
      status = RADICLE_CONDITION_UNCHANGED;
      return RADICLE_STATUS_OK;
    case fo4::schema::ScopedConditionStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::schema::ScopedConditionStatus::record_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "record type does not match the condition scope");
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
                                         const radicle_condition_list** out_list) {
  clear_error();
  if (env == nullptr || record == nullptr || out_list == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_list are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_list = nullptr;
  const auto& source = *to_ref(record)->record;
  return guarded("unknown exception while reading conditions", [&] {
    fo4::schema::ScopedConditionsRead result;
    if (scope_path == nullptr || *scope_path == '\0') {
      result = fo4::schema::read_root_conditions(source);
    } else {
      const auto path = fo4::schema::scoped_condition_path(source.header.signature, scope_path);
      if (!path) return unknown_scope();
      result = fo4::schema::read_scoped_conditions(source, path->descriptor,
                                                   path->container_index, path->inner_index);
    }
    if (!result.ok) return read_failure(result.error);

    auto holder = std::make_unique<ConditionListHolder>();
    holder->rows.reserve(result.conditions.size());
    for (const auto& c : result.conditions) {
      holder->rows.push_back({c.flags_byte, c.comparison_raw, c.function, c.unknown2, c.param1,
                              c.param2, c.run_on, c.reference, c.unknown3,
                              utf8_text(holder->text, c.parameter1_string),
                              utf8_text(holder->text, c.parameter2_string)});
    }
    holder->view.rows = holder->rows.data();
    holder->view.count = holder->rows.size();
    holder->view.internal = holder.get();
    *out_list = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_condition_list_free(const radicle_condition_list* list) {
  if (list != nullptr) delete static_cast<ConditionListHolder*>(list->internal);
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
    try {
      std::vector<fo4::CtdaCondition> conditions(count);
      for (size_t i = 0; i < count; ++i) {
        if (!to_ctda(rows[i], conditions[i])) {
          return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                      "condition text is not representable in Windows-1252");
        }
      }
      const std::span<const fo4::CtdaCondition> view(conditions);
      if (scope_path == nullptr || *scope_path == '\0') {
        return set_root(plugin->value, *key, view, *out_status);
      }
      return set_scoped(plugin->value, *key, scope_path, view, *out_status);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

namespace {

// Constraint: The schema view points straight at the generated table's string literals; the holder is built once, never freed, so radicle_condition_schema_free is a no-op and needs no holder cast.
struct ConditionSchemaHolder {
  radicle_condition_schema_meta view{};
  std::vector<radicle_condition_function_meta> functions;
  std::vector<radicle_condition_run_on_meta> run_ons;
};

}  // namespace

radicle_status radicle_condition_schema_get(
    const radicle_condition_schema_meta** out_schema) {
  clear_error();
  if (out_schema == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_schema is required");
  }
  *out_schema = nullptr;
  return guarded("unknown exception while reading the condition schema", [&] {
    static const ConditionSchemaHolder* const cached = [] {
      auto holder = std::make_unique<ConditionSchemaHolder>();
      const auto editor = fo4::schema::condition_editor_function_indices();
      const auto rows = fo4::schema::condition_functions();
      holder->functions.reserve(rows.size());
      for (const auto& row : rows) {
        radicle_condition_function_meta meta{};
        meta.index = row.index;
        meta.name = row.name.data();
        std::uint8_t count = 0;
        for (const auto* parameter : row.parameters) {
          if (parameter == nullptr) break;
          meta.parameter_names[count++] = parameter->name.data();
        }
        meta.parameter_count = count;
        meta.editor = std::binary_search(editor.begin(), editor.end(), row.index) ? 1 : 0;
        holder->functions.push_back(meta);
      }
      const auto run_ons = fo4::schema::condition_run_ons();
      holder->run_ons.reserve(run_ons.size());
      for (const auto& row : run_ons) {
        holder->run_ons.push_back(radicle_condition_run_on_meta{row.value, row.name.data()});
      }
      holder->view.functions = holder->functions.data();
      holder->view.function_count = holder->functions.size();
      holder->view.run_ons = holder->run_ons.data();
      holder->view.run_on_count = holder->run_ons.size();
      return holder.release();
    }();
    *out_schema = &cached->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_condition_schema_free(const radicle_condition_schema_meta*) {}

radicle_status radicle_condition_function_id(const char* name, uint16_t* out_id) {
  clear_error();
  if (name == nullptr || out_id == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "name and out_id are required");
  const auto* function = fo4::schema::condition_function_by_name(name);
  if (function == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "no condition function has that name");
  *out_id = function->index;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_condition_function_info(uint16_t id, radicle_condition_function_facts* out) {
  clear_error();
  if (out == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out is required");
  const auto* function = fo4::schema::condition_function(id);
  if (function == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "no condition function has that id");
  const auto* first = function->parameters[0];
  out->name = function->name.data();
  out->takes_form_param = static_cast<uint8_t>(first != nullptr && first->kind == fo4::schema::ConditionParameterKind::form_link ? 1 : 0);
  out->first_param_is_quest = static_cast<uint8_t>(first != nullptr && first->name == "quest" ? 1 : 0);
  return RADICLE_STATUS_OK;
}


namespace {

using ParamKind = fo4::schema::ConditionParameterKind;

constexpr std::size_t k_ctda_param1_slot = 12;
constexpr std::size_t k_ctda_param2_slot = 16;

radicle_condition_param_kind param_kind(ParamKind kind) {
  switch (kind) {
    case ParamKind::none: return RADICLE_CONDITION_PARAM_KIND_NONE;
    case ParamKind::integer: return RADICLE_CONDITION_PARAM_KIND_INTEGER;
    case ParamKind::float_value: return RADICLE_CONDITION_PARAM_KIND_FLOAT;
    case ParamKind::string: return RADICLE_CONDITION_PARAM_KIND_STRING;
    case ParamKind::form_link: return RADICLE_CONDITION_PARAM_KIND_FORM;
    case ParamKind::enum_value: return RADICLE_CONDITION_PARAM_KIND_ENUM;
    case ParamKind::other: return RADICLE_CONDITION_PARAM_KIND_OTHER;
  }
  return RADICLE_CONDITION_PARAM_KIND_OTHER;
}

// Constraint: mirrors the checks of radicle_record_resolve_form_id but must never throw, so one bad FormID leaves only its own slot unresolved.
void resolve_param_form(const radicle_env& env, const fo4::env::RecordRef& ref,
                        radicle_condition_param& out) {
  const fo4::RawFormId raw{static_cast<std::uint32_t>(out.raw)};
  if (raw.is_null() || raw.is_none()) return;
  try {
    if (raw.is_created() || raw.index_byte() > env.value.plugins().at(ref.plugin).masters.size()) {
      out.form_unresolved = 1;
      return;
    }
    out.form = to_c_key(env, env.value.resolve_link(ref, raw));
  } catch (const fo4::env::EnvironmentError&) {
    out.form_unresolved = 1;
  } catch (const fo4::FormIdError&) {
    out.form_unresolved = 1;
  }
}

radicle_condition_param typed_param(const radicle_env& env, const fo4::env::RecordRef& ref,
                                    const fo4::schema::ConditionParameterType* type,
                                    bool form_slot, std::int32_t raw, const char* text) {
  radicle_condition_param out{};
  out.raw = raw;
  if (type == nullptr || type->kind == ParamKind::none) return out;
  out.kind = param_kind(type->kind);
  out.type_name = type->name.data();
  switch (type->kind) {
    case ParamKind::form_link:
      if (form_slot) {
        resolve_param_form(env, ref, out);
      } else {
        out.kind = RADICLE_CONDITION_PARAM_KIND_INDEX;
        out.number = raw;
      }
      break;
    case ParamKind::float_value: {
      float value;
      std::memcpy(&value, &raw, sizeof(value));
      out.number = value;
      break;
    }
    case ParamKind::string:
      out.text = text;
      break;
    default:
      out.number = raw;
      break;
  }
  return out;
}

radicle_condition_typed typed_row(const radicle_env& env, const fo4::env::RecordRef& ref,
                                  const radicle_condition& row) {
  fo4::CtdaCondition ctda;
  ctda.flags_byte = row.flags_byte;
  ctda.comparison_raw = row.comparison_raw;
  ctda.function = row.function;
  ctda.unknown2 = row.unknown2;
  ctda.param1 = row.param1;
  ctda.param2 = row.param2;
  ctda.run_on = row.run_on;
  ctda.reference = row.reference;
  ctda.unknown3 = row.unknown3;
  fo4::Subrecord subrecord;
  subrecord.signature = fo4::sig_ctda;
  subrecord.data = fo4::serialize_ctda_condition(ctda);
  const auto form_slots = fo4::ctda_formid_offsets(subrecord);
  const auto is_form_slot = [&](std::size_t slot) {
    return std::find(form_slots.begin(), form_slots.end(), slot) != form_slots.end();
  };

  radicle_condition_typed out{};
  const auto* function = fo4::schema::condition_function(row.function);
  if (function != nullptr) {
    out.function_name = function->name.data();
    out.param1 = typed_param(env, ref, function->parameters[0], is_form_slot(k_ctda_param1_slot),
                             row.param1, row.parameter1_string);
    out.param2 = typed_param(env, ref, function->parameters[1], is_form_slot(k_ctda_param2_slot),
                             row.param2, row.parameter2_string);
  } else {
    out.param1.raw = row.param1;
    out.param2.raw = row.param2;
  }

  out.comparison.raw = static_cast<std::int32_t>(row.comparison_raw);
  if ((row.flags_byte & fo4::k_ctda_flag_use_global) != 0) {
    out.comparison.kind = RADICLE_CONDITION_PARAM_KIND_FORM;
    out.comparison.type_name = "global";
    resolve_param_form(env, ref, out.comparison);
  } else {
    out.comparison.kind = RADICLE_CONDITION_PARAM_KIND_FLOAT;
    out.comparison.type_name = "float";
    out.comparison.number = ctda.comparison_float();
  }

  out.reference.raw = static_cast<std::int32_t>(row.reference);
  const auto* run_on = fo4::schema::condition_run_on(row.run_on);
  if (run_on != nullptr && run_on->reference_form_id) {
    out.reference.kind = RADICLE_CONDITION_PARAM_KIND_FORM;
    out.reference.type_name = "reference";
    resolve_param_form(env, ref, out.reference);
  } else if (run_on != nullptr) {
    out.reference = typed_param(env, ref, run_on->parameter3, false, out.reference.raw, nullptr);
  }
  return out;
}

}  // namespace

radicle_status radicle_condition_params(const radicle_env* env, const radicle_record* record,
                                        const radicle_condition* rows, size_t count,
                                        radicle_condition_typed* out) {
  clear_error();
  if (env == nullptr || record == nullptr || (count != 0 && (rows == nullptr || out == nullptr))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, rows and out are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return guarded("unknown exception while typing condition parameters", [&] {
    const auto& ref = *to_ref(record);
    for (size_t i = 0; i < count; ++i) out[i] = typed_row(*env, ref, rows[i]);
    return RADICLE_STATUS_OK;
  });
}

}
