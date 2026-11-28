#include "fo4/plugin/batch_patch.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/semantic_fields.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <utility>

namespace fo4 {
namespace {

std::string ascii_lower(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

bool is_null_link(std::string_view value) { return value.empty() || ascii_lower(value) == "null"; }

bool bespoke_field(std::string_view field) {
  for (const std::string_view prefix : {"ARMO\\", "ARMA\\", "EYES\\", "LVSP\\"}) {
    if (field.starts_with(prefix)) return true;
  }
  return field == "EDID" || field == "EditorID" || field == "Subrecords\\EDID[0]";
}

bool contains_ignore_case(std::string_view haystack, std::string_view needle) {
  return ascii_lower(haystack).find(ascii_lower(needle)) != std::string::npos;
}

std::optional<std::string> scalar_status_error(ScalarMutationStatus status, const BatchOperation& op) {
  switch (status) {
    case ScalarMutationStatus::record_missing:
      return "record is missing from the patch";
    case ScalarMutationStatus::subrecord_missing:
      return op.field + " has no " + op.scalar->scalar.subrecord.to_string() +
             " subrecord; Radicle will not guess a missing subrecord insertion position";
    case ScalarMutationStatus::next_alias_id_too_low:
      return "QUST Next Alias ID must stay above every existing alias ID";
    case ScalarMutationStatus::updated:
    case ScalarMutationStatus::unchanged:
      break;
  }
  return std::nullopt;
}

std::optional<std::string> link_status_error(FormLinkFieldMutationStatus status, const BatchOperation& op) {
  switch (status) {
    case FormLinkFieldMutationStatus::record_missing:
      return "record is missing from the patch";
    case FormLinkFieldMutationStatus::record_mismatch:
      return op.field + " does not apply to this record";
    case FormLinkFieldMutationStatus::subrecord_missing:
      return op.field + " has no " + op.link->subrecord.to_string() +
             " subrecord; Radicle will not guess a missing subrecord insertion position";
    case FormLinkFieldMutationStatus::updated:
    case FormLinkFieldMutationStatus::unchanged:
      break;
  }
  return std::nullopt;
}

std::string list_status_error(schema::FormLinkListMutationStatus status, const BatchOperation& op) {
  switch (status) {
    case schema::FormLinkListMutationStatus::record_missing:
      return "record is missing from the patch";
    case schema::FormLinkListMutationStatus::record_mismatch:
      return "record type does not match " + op.field;
    case schema::FormLinkListMutationStatus::target_type_mismatch:
      return "target type is not valid for " + op.field;
    case schema::FormLinkListMutationStatus::insertion_boundary_unavailable:
      return "cannot determine a safe insertion boundary for " + op.field;
    case schema::FormLinkListMutationStatus::container_index_out_of_range:
    case schema::FormLinkListMutationStatus::index_out_of_range:
      return "append index is outside " + op.field;
    case schema::FormLinkListMutationStatus::unchanged:
    case schema::FormLinkListMutationStatus::updated:
      break;
  }
  return {};
}

std::unexpected<BatchError> refuse(std::string code, std::string message) {
  return std::unexpected(BatchError{std::move(code), std::move(message)});
}

std::expected<BatchOperation, BatchError> prepare_operation(const BatchOperationSpec& spec,
                                                            Signature signature,
                                                            const std::string& type_text,
                                                            const BatchPatchHost& host) {
  BatchOperation op;
  op.op = spec.op;
  op.field = spec.field;
  op.value = spec.value;
  if (op.op != "set" && op.op != "add" && op.op != "remove") {
    return refuse("InvalidOperations", "op must be set, add or remove: " + op.op);
  }
  if (op.field.empty()) return refuse("InvalidOperations", "every operation needs a field");
  if (bespoke_field(op.field)) {
    return refuse("BatchFieldRefused",
                  op.field + " has a bespoke write path (ARMO/ARMA, EYES, LVSP, EDID); use set_field per record");
  }

  if (op.op == "set") {
    const auto address = semantic_field_address(op.field);
    const auto path = address ? address->path : std::string_view{op.field};
    if (const auto link = schema::formlink_field_descriptor(path)) {
      if (link->record != signature) {
        return refuse("BatchFieldRecordMismatch",
                      op.field + " applies to " + link->record.to_string() + " records, not " + type_text);
      }
      op.kind = BatchOpKind::set_link;
      op.link = link;
      op.index = address ? address->index : 0;
      if (is_null_link(op.value)) {
        if (!link->nullable) return refuse("FormLinkNullNotAllowed", op.field + " does not allow NULL");
      } else {
        const auto resolved = host.resolve_value(op.value);
        if (!resolved) {
          return refuse("ListValueUnresolved",
                        "'" + op.value + "' is not a FormKey and no loaded record has that EditorID.");
        }
        op.target = *resolved;
        const auto winner = host.winner_signature(op.target);
        if (!winner) return refuse("FormLinkTargetUnresolved", "FormLink target is not loaded: " + op.value);
        if (!schema::formlink_field_target_allowed(*link, *winner)) {
          return refuse("FormLinkTargetTypeMismatch",
                        op.value + " is a " + winner->to_string() + ", not an allowed target for " + op.field);
        }
      }
    } else if (const auto scalar = schema::scalar_field_descriptor(path)) {
      if (scalar->record != signature) {
        return refuse("BatchFieldRecordMismatch",
                      op.field + " applies to " + scalar->record.to_string() + " records, not " + type_text);
      }
      op.scalar = scalar;
      op.index = address ? address->index : 0;
      const auto storage = scalar->scalar.storage;
      if (storage == schema::ScalarStorage::zstring || storage == schema::ScalarStorage::fixed_string) {
        const auto stored = text::utf8_to_windows1252(op.value);
        if (!stored) {
          return refuse("ScalarFieldWriteFailed", "Text must be valid UTF-8 representable in Windows-1252");
        }
        op.kind = BatchOpKind::set_string;
        op.stored = *stored;
      } else {
        op.kind = BatchOpKind::set_scalar;
      }
    } else {
      return refuse("BatchFieldUnsupported",
                    op.field + " is not a schema scalar or FormLink field; use add/remove for FormLink lists");
    }
    return op;
  }

  auto list = schema::formlink_list_descriptor(signature, op.field);
  if (!list) list = schema::formlink_list_descriptor_by_field(signature, op.field);
  if (!list) return refuse("ListFieldUnsupported", op.field + " is not a FormLink list on " + type_text + " records");
  if (op.value.empty()) return refuse("ListValueMissing", "List value is empty");
  const auto resolved = host.resolve_value(op.value);
  if (!resolved) {
    return refuse("ListValueUnresolved", "'" + op.value + "' is not a FormKey and no loaded record has that EditorID.");
  }
  op.kind = op.op == "add" ? BatchOpKind::add_to_list : BatchOpKind::remove_from_list;
  op.list = list;
  op.target = *resolved;
  if (op.kind == BatchOpKind::add_to_list && !list->wildcard_target) {
    const auto winner = host.winner_signature(op.target);
    if (!winner) {
      return refuse("FormLinkTargetMissing", "FormLink target does not resolve in the loaded environment: " + op.value);
    }
    if (!schema::formlink_list_target_allowed(*list, *winner)) {
      return refuse("FormLinkTargetTypeMismatch", op.field + " does not accept " + winner->to_string() + " targets");
    }
  }
  return op;
}

bool filter_hit(const Plugin& source, const Record& record, const BatchPlan& plan) {
  if (plan.filter_editor_id) return contains_ignore_case(record_editor_id(record), plan.filter_value);
  const auto cells = semantic_field_cells(source, record);
  if (!cells) return false;
  for (const auto& cell : *cells) {
    const bool named = cell.path == plan.filter_path || cell.path.starts_with(plan.filter_path + "[");
    if (named && cell.available && contains_ignore_case(cell.value, plan.filter_value)) return true;
  }
  return false;
}

}  // namespace

std::string record_editor_id(const Record& record) {
  const auto data = record.subrecords.first_nonempty_data(sig_edid);
  if (!data) return {};
  const auto* begin = reinterpret_cast<const char*>(data->data());
  const auto* end = begin + data->size();
  const auto* nul = std::find(begin, end, '\0');
  return text::windows1252_to_utf8(std::string(begin, nul));
}

std::expected<BatchPlan, BatchError> plan_batch_patch(std::string_view type_text,
                                                      std::span<const BatchOperationSpec> operations,
                                                      std::string_view filter_field,
                                                      std::string_view filter_value,
                                                      const BatchPatchHost& host) {
  if (type_text.size() != 4) {
    return std::unexpected(BatchError{"InvalidRecordType",
                                      "type must be a four-letter record signature, e.g. COBJ; got '" +
                                          std::string(type_text) + "'"});
  }
  if (!host.resolve_value || !host.winner_signature || !host.ensure_master) {
    return std::unexpected(BatchError{"BatchHostIncomplete", "batch host callbacks are not all provided"});
  }
  BatchPlan plan;
  plan.type_text = std::string(type_text);
  std::transform(plan.type_text.begin(), plan.type_text.end(), plan.type_text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  plan.signature = Signature::from_string(plan.type_text);

  for (const auto& spec : operations) {
    auto prepared = prepare_operation(spec, plan.signature, plan.type_text, host);
    if (!prepared) return std::unexpected(prepared.error());
    plan.operations.push_back(std::move(*prepared));
  }

  plan.filter_field = std::string(filter_field);
  plan.filter_value = std::string(filter_value);
  plan.filter_path = plan.filter_field;
  if (!plan.filter_field.empty()) {
    plan.filter_editor_id = plan.filter_field == "EditorID" || plan.filter_field == "EDID";
    if (!plan.filter_editor_id) {
      auto list = schema::formlink_list_descriptor(plan.signature, plan.filter_field);
      if (!list) list = schema::formlink_list_descriptor_by_field(plan.signature, plan.filter_field);
      if (list) {
        plan.filter_path = std::string(list->path);
      } else if (!semantic_field_address(plan.filter_field)) {
        return refuse("FilterFieldUnknown",
                      plan.filter_field + " is not a schema field, FormLink list or EditorID");
      }
    }
  }
  plan.filtering = !plan.filter_field.empty() && !plan.filter_value.empty();
  return plan;
}

BatchSelection select_batch_records(const Plugin& source, const BatchPlan& plan, std::size_t limit) {
  BatchSelection selection;
  for (const auto* record : collect_records(source)) {
    if (record->header.signature != plan.signature || record->header.flags.is_deleted()) continue;
    ++selection.total;
    if (selection.matched.size() >= limit) continue;
    if (plan.filtering && !filter_hit(source, *record, plan)) continue;
    selection.matched.push_back(record);
  }
  return selection;
}

BatchApplyResult apply_batch_patch(const Plugin& base,
                                   const Plugin& source,
                                   std::span<const Record* const> matched,
                                   const BatchPlan& plan,
                                   const BatchPatchHost& host) {
  BatchApplyResult result;
  if (!host.ensure_master) {
    result.failed = matched.size();
    result.errors.emplace_back("batch host is missing ensure_master");
    return result;
  }
  Plugin target = base;
  const auto fail = [&](const Record& record, const std::string& message) {
    if (result.errors.size() >= k_batch_max_errors) return;
    const auto eid = record_editor_id(record);
    result.errors.push_back((eid.empty() ? record.form_key.to_string() : eid) + " [" + record.form_key.to_string() +
                            "]: " + message);
  };

  for (const auto* record : matched) {
    const auto key = record->form_key;
    const auto copy = copy_record_as_override(target, source, key, false);
    if (copy == RecordCopyStatus::source_missing) {
      ++result.failed;
      fail(*record, "record is not present in the source plugin");
      continue;
    }
    if (copy == RecordCopyStatus::source_containment_unsupported) {
      ++result.failed;
      fail(*record, "record requires nested source GRUP containment that native insertion does not support yet");
      continue;
    }
    if (copy == RecordCopyStatus::inserted || copy == RecordCopyStatus::replaced) {
      ++result.inserted;
      result.changed = true;
    }

    bool record_failed = false;
    for (const auto& op : plan.operations) {
      try {
        if (op.kind == BatchOpKind::set_scalar || op.kind == BatchOpKind::set_string) {
          const auto status = op.kind == BatchOpKind::set_string
                                  ? set_record_string(target, key, *op.scalar, op.index, op.stored, true)
                                  : set_record_scalar(target, key, *op.scalar, op.index, op.value, true);
          if (const auto message = scalar_status_error(status, op)) {
            fail(*record, *message);
            record_failed = true;
            continue;
          }
          if (status == ScalarMutationStatus::updated) result.changed = true;
        } else if (op.kind == BatchOpKind::set_link) {
          host.ensure_master(target, op.target.owner());
          const auto status = set_record_formlink_field(target, key, *op.link, op.index, op.target);
          if (const auto message = link_status_error(status, op)) {
            fail(*record, *message);
            record_failed = true;
            continue;
          }
          if (status == FormLinkFieldMutationStatus::updated) result.changed = true;
        } else if (op.kind == BatchOpKind::add_to_list) {
          const auto size = schema::record_formlink_list_size(target, key, *op.list);
          if (!size) {
            fail(*record, "record is missing from the patch");
            record_failed = true;
            continue;
          }
          host.ensure_master(target, op.target.owner());
          const auto status = schema::insert_record_formlink_list_element(target, key, *op.list, *size, op.target);
          if (status != schema::FormLinkListMutationStatus::updated) {
            fail(*record, list_status_error(status, op));
            record_failed = true;
            continue;
          }
          result.changed = true;
        } else {
          const auto owner = op.target.owner();
          const bool encodable = owner.empty() || owner == target.key ||
                                 std::any_of(target.header.masters.begin(), target.header.masters.end(),
                                             [&](const auto& master) { return master.key == owner; });
          const auto removed =
              encodable ? schema::remove_record_formlink_list_elements_matching(target, key, *op.list, op.target) : 0;
          if (removed == 0) {
            fail(*record, "no entry matching " + op.value + " found in " + op.field);
            record_failed = true;
            continue;
          }
          result.changed = true;
        }
        ++result.operations_applied;
      } catch (const std::exception& error) {
        fail(*record, error.what());
        record_failed = true;
      }
    }
    if (record_failed) {
      ++result.failed;
    } else {
      ++result.done;
    }
  }
  if (result.failed == 0 && result.changed) result.plugin = std::move(target);
  return result;
}

}  // namespace fo4
