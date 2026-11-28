#include "fo4/plugin/field_access.hpp"
#include "fo4/localization/strings.hpp"
#include "fo4/text/encoding.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"

#include <optional>
#include <stdexcept>

namespace fo4 {
namespace {

struct Resolved {
  std::optional<schema::ScalarFieldDescriptor> scalar;
  std::optional<schema::FormLinkFieldDescriptor> link;
  std::optional<schema::FormLinkListDescriptor> list;
  std::optional<schema::CountedArrayDescriptor> array;
  std::optional<schema::SubrecordGroupDescriptor> group;
  std::optional<schema::SubrecordGroupDescriptor> parent;
  std::size_t parent_index{0};

  bool any() const { return scalar || link || list || array || group; }
};

std::optional<Signature> path_signature(std::string_view path) {
  if (path.size() < 6 || path[4] != '\\') return std::nullopt;
  return Signature::from_string(path.substr(0, 4));
}

// Constraint: Parent[i]\Child names the elements of Child inside element i of its parent group; i is plain decimal digits.
Resolved resolve_scoped(Signature record, std::string_view path, std::size_t open) {
  Resolved row;
  const auto close = path.find(']', open);
  if (close == std::string_view::npos || close == open + 1 || close - open > 10 || close + 2 >= path.size() ||
      path[close + 1] != '\\' || path.find('[', close) != std::string_view::npos) {
    return row;
  }
  std::size_t index = 0;
  for (const char digit : path.substr(open + 1, close - open - 1)) {
    if (digit < '0' || digit > '9') return row;
    index = index * 10 + static_cast<std::size_t>(digit - '0');
  }
  const auto parent = schema::subrecord_group_descriptor(record, path.substr(0, open));
  const auto child = schema::subrecord_group_descriptor(record, std::string(path.substr(0, 5)) + std::string(path.substr(close + 2)));
  if (!parent || !child || child->parent != parent->path) return row;
  row.group = child;
  row.parent = parent;
  row.parent_index = index;
  return row;
}

Resolved resolve(Signature record, std::string_view path) {
  if (const auto open = path.find('['); open != std::string_view::npos) return resolve_scoped(record, path, open);
  Resolved row;
  row.scalar = schema::scalar_field_descriptor(record, path);
  if (row.scalar) return row;
  row.link = schema::formlink_field_descriptor(record, path);
  if (row.link) return row;
  row.list = schema::formlink_list_descriptor(record, path);
  if (row.list) return row;
  row.array = schema::counted_array_descriptor(record, path);
  if (row.array) return row;
  row.group = schema::subrecord_group_descriptor(record, path);
  return row;
}

bool is_string(const schema::ScalarFieldDescriptor& field) {
  return field.scalar.storage == schema::ScalarStorage::zstring ||
         field.scalar.storage == schema::ScalarStorage::fixed_string;
}

std::int32_t to_storage(schema::ScalarStorage storage) {
  switch (storage) {
    case schema::ScalarStorage::u8: return 0;
    case schema::ScalarStorage::u16: return 1;
    case schema::ScalarStorage::u32: return 2;
    case schema::ScalarStorage::i32: return 3;
    case schema::ScalarStorage::f32: return 4;
    case schema::ScalarStorage::i8: return 5;
    case schema::ScalarStorage::i16: return 6;
    case schema::ScalarStorage::zstring: return 7;
    case schema::ScalarStorage::fixed_string: return 8;
    case schema::ScalarStorage::bytes: return 9;
  }
  return -1;
}

std::int32_t to_presentation(schema::ScalarPresentation presentation) {
  switch (presentation) {
    case schema::ScalarPresentation::identity: return 0;
    case schema::ScalarPresentation::percent_0_100: return 1;
    case schema::ScalarPresentation::enumeration: return 2;
    case schema::ScalarPresentation::flags: return 3;
  }
  return -1;
}

void fill_scalar_ex(FieldSchemaExRow& row, const schema::ScalarFieldDescriptor& field) {
  const auto& scalar = field.scalar;
  row.kind = is_string(field) ? FieldKind::string : FieldKind::scalar;
  row.can_insert = field.can_insert;
  row.repeated = scalar.address.repeated;
  row.subrecord = scalar.subrecord.to_string();
  row.storage = to_storage(scalar.storage);
  row.presentation = to_presentation(scalar.presentation);
  row.embedded = scalar.embedded;
  row.localized = scalar.localized;
  row.offset = scalar.offset;
  row.payload_size = scalar.payload_size;
  row.minimum = scalar.minimum;
  row.maximum = scalar.maximum;
  row.min_form_version = scalar.min_form_version;
  row.max_form_version = scalar.max_form_version;
  row.names.clear();
  row.names.reserve(scalar.names.size());
  for (const auto& name : scalar.names) {
    row.names.push_back({std::string(name.name), name.value});
  }
}

FieldStatus convert(ScalarMutationStatus status) {
  switch (status) {
    case ScalarMutationStatus::record_missing: return FieldStatus::record_missing;
    case ScalarMutationStatus::subrecord_missing: return FieldStatus::occurrence_missing;
    case ScalarMutationStatus::next_alias_id_too_low: return FieldStatus::next_alias_id_too_low;
    default: return FieldStatus::ok;
  }
}

FieldStatus convert(FormLinkFieldMutationStatus status) {
  switch (status) {
    case FormLinkFieldMutationStatus::record_missing: return FieldStatus::record_missing;
    case FormLinkFieldMutationStatus::record_mismatch: return FieldStatus::record_mismatch;
    case FormLinkFieldMutationStatus::subrecord_missing: return FieldStatus::occurrence_missing;
    default: return FieldStatus::ok;
  }
}

FieldStatus convert(schema::FormLinkListMutationStatus status) {
  using Status = schema::FormLinkListMutationStatus;
  switch (status) {
    case Status::record_missing: return FieldStatus::record_missing;
    case Status::record_mismatch: return FieldStatus::record_mismatch;
    case Status::container_index_out_of_range:
    case Status::index_out_of_range: return FieldStatus::index_out_of_range;
    case Status::target_type_mismatch: return FieldStatus::target_type_mismatch;
    case Status::insertion_boundary_unavailable: return FieldStatus::boundary_unavailable;
    default: return FieldStatus::ok;
  }
}

FieldStatus convert(CountedArrayMutationStatus status) {
  switch (status) {
    case CountedArrayMutationStatus::record_missing: return FieldStatus::record_missing;
    case CountedArrayMutationStatus::index_out_of_range: return FieldStatus::index_out_of_range;
    case CountedArrayMutationStatus::subrecord_missing: return FieldStatus::occurrence_missing;
    case CountedArrayMutationStatus::would_empty: return FieldStatus::would_empty;
    default: return FieldStatus::ok;
  }
}

FieldStatus convert(SubrecordGroupMutationStatus status) {
  switch (status) {
    case SubrecordGroupMutationStatus::record_missing: return FieldStatus::record_missing;
    case SubrecordGroupMutationStatus::index_out_of_range: return FieldStatus::index_out_of_range;
    case SubrecordGroupMutationStatus::subrecord_missing: return FieldStatus::occurrence_missing;
    default: return FieldStatus::ok;
  }
}

FieldStatus check_record(const Plugin& plugin, const FormKey& key, Signature signature) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) return FieldStatus::record_missing;
  return record->header.signature == signature ? FieldStatus::ok : FieldStatus::record_mismatch;
}

FieldStatus parent_element(const Record& record, const Resolved& row, std::optional<SubrecordGroupSpan>& within) {
  if (!row.parent) return FieldStatus::ok;
  const auto spans = subrecord_group_spans(record, *row.parent);
  if (row.parent_index >= spans.size()) return FieldStatus::index_out_of_range;
  within = spans[row.parent_index];
  return FieldStatus::ok;
}

FieldStatus parent_element(const Plugin& plugin,
                           const FormKey& key,
                           const Resolved& row,
                           std::optional<SubrecordGroupSpan>& within) {
  const auto* record = find_record(plugin, key);
  if (record == nullptr) return FieldStatus::record_missing;
  return parent_element(*record, row, within);
}

const SubrecordGroupSpan* span_ptr(const std::optional<SubrecordGroupSpan>& within) {
  return within ? &*within : nullptr;
}

std::string link_text(const FormKey& key) { return key.owner().empty() ? std::string{} : key.to_string(); }

std::string link_text(const Plugin& plugin, RawFormId raw) {
  return link_text(resolve_plugin_link(raw, plugin_form_id_context(plugin)));
}

}  // namespace

std::vector<FieldSchemaRow> record_field_schema_rows(Signature record) {
  std::vector<FieldSchemaRow> rows;
  for (const auto& field : schema::scalar_fields(record)) {
    rows.push_back({std::string(field.path), is_string(field) ? FieldKind::string : FieldKind::scalar,
                    field.can_insert, field.scalar.address.repeated});
  }
  for (const auto& field : schema::formlink_fields(record)) {
    rows.push_back({std::string(field.path), FieldKind::formlink, false, field.address.repeated});
  }
  for (const auto& list : schema::formlink_lists(record)) {
    rows.push_back({std::string(list.path), FieldKind::formlink_list, list.can_insert, true});
  }
  for (const auto& array : schema::counted_arrays(record)) {
    rows.push_back({std::string(array.path), FieldKind::counted_array, true, true});
  }
  for (const auto& group : schema::subrecord_groups(record)) {
    rows.push_back({std::string(group.path), FieldKind::subrecord_group, true, true});
  }
  return rows;
}

std::vector<FieldSchemaExRow> record_field_schema_ex_rows(Signature record) {
  std::vector<FieldSchemaExRow> rows;
  for (const auto& field : schema::scalar_fields(record)) {
    FieldSchemaExRow row;
    row.path = std::string(field.path);
    fill_scalar_ex(row, field);
    rows.push_back(std::move(row));
  }
  for (const auto& field : schema::formlink_fields(record)) {
    rows.push_back({std::string(field.path), FieldKind::formlink, false, field.address.repeated,
                    field.subrecord.to_string(), {}, -1, -1, field.offset != 0, false, field.offset, 0, 0, 0.0, 0.0,
                    field.min_form_version, field.max_form_version, {}});
  }
  for (const auto& list : schema::formlink_lists(record)) {
    rows.push_back({std::string(list.path), FieldKind::formlink_list, list.can_insert, true,
                    list.subrecord.to_string(), {}, -1, -1, false, false, 0, 0, 0, 0.0, 0.0, 0, 0, {}});
  }
  for (const auto& array : schema::counted_arrays(record)) {
    rows.push_back({std::string(array.path), FieldKind::counted_array, true, true, array.subrecord.to_string(),
                    array.count_subrecord == Signature{} ? std::string{} : array.count_subrecord.to_string(),
                    -1, -1, false, false, 0, 0, array.stride, 0.0, 0.0, 0, 0, {}});
  }
  for (const auto& group : schema::subrecord_groups(record)) {
    rows.push_back({std::string(group.path), FieldKind::subrecord_group, true, true, {},
                    group.count_subrecord == Signature{} ? std::string{} : group.count_subrecord.to_string(),
                    -1, -1, false, false, 0, 0, 0, 0.0, 0.0, 0, 0, {}});
  }
  return rows;
}

FieldStatus read_record_field(const Plugin& plugin,
                              const Record& record,
                              std::string_view path,
                              std::size_t index,
                              std::string& out_text,
                              const LocalizedStringLookup* strings,
                              std::size_t plugin_index) {
  out_text.clear();
  const auto signature = path_signature(path);
  if (!signature) return FieldStatus::unknown_path;
  const auto row = resolve(*signature, path);
  if (!row.any()) return FieldStatus::unknown_path;
  if (record.header.signature != *signature) return FieldStatus::record_mismatch;
  if (row.array) {
    const auto text = record_counted_array_element_hex(record, *row.array, index);
    if (!text) {
      return record_counted_array_size(plugin, record.form_key, *row.array).value_or(0) == 0
                 ? FieldStatus::occurrence_missing
                 : FieldStatus::index_out_of_range;
    }
    out_text = *text;
    return FieldStatus::ok;
  }
  if (row.group) {
    std::optional<SubrecordGroupSpan> within;
    if (const auto status = parent_element(record, row, within); status != FieldStatus::ok) {
      return status;
    }
    const auto text = record_subrecord_group_element_hex(record, *row.group, index, span_ptr(within));
    if (!text) {
      if (subrecord_group_spans(record, *row.group, span_ptr(within)).empty()) return FieldStatus::occurrence_missing;
      return FieldStatus::index_out_of_range;
    }
    out_text = *text;
    return FieldStatus::ok;
  }
  if (row.scalar) {
    if (record_scalar_is_localized_string(plugin, *row.scalar)) {
      const auto id = record_localized_string_id(plugin, record, *row.scalar, index);
      if (!id) return FieldStatus::occurrence_missing;
      if (*id == 0) return FieldStatus::ok;
      if (strings == nullptr) {
        throw schema::ScalarError(std::string(path) + " is a localized string in a localized plugin");
      }
      const auto text = strings->lookup_field(plugin_index, record.header.signature,
                                              row.scalar->scalar.subrecord, *id);
      if (!text) return FieldStatus::occurrence_missing;
      out_text = text::windows1252_to_utf8(*text);
      return FieldStatus::ok;
    }
    const auto text = record_scalar_text(plugin, record, *row.scalar, index);
    if (!text) return FieldStatus::occurrence_missing;
    out_text = *text;
    return FieldStatus::ok;
  }
  if (row.link) {
    const auto raw = record_formlink_field_raw(record, *row.link, index);
    if (!raw) return FieldStatus::occurrence_missing;
    out_text = link_text(plugin, *raw);
    return FieldStatus::ok;
  }
  const auto values = schema::record_formlink_list_values(plugin, record.form_key, *row.list);
  if (!values) return FieldStatus::record_missing;
  if (index >= values->size()) return FieldStatus::index_out_of_range;
  out_text = link_text((*values)[index]);
  return FieldStatus::ok;
}

FieldStatus write_record_field(Plugin& plugin,
                               const FormKey& record_key,
                               std::string_view path,
                               std::size_t index,
                               std::string_view text,
                               localization::LocalizationTables* tables) {
  const auto signature = path_signature(path);
  if (!signature) return FieldStatus::unknown_path;
  const auto row = resolve(*signature, path);
  if (!row.any()) return FieldStatus::unknown_path;
  assign_record_form_keys(plugin);
  if (const auto status = check_record(plugin, record_key, *signature); status != FieldStatus::ok) {
    return status;
  }
  if (row.array) return convert(set_record_counted_element_hex(plugin, record_key, *row.array, index, text));
  if (row.group) {
    std::optional<SubrecordGroupSpan> within;
    if (const auto status = parent_element(plugin, record_key, row, within); status != FieldStatus::ok) {
      return status;
    }
    const auto spans = subrecord_group_spans(plugin, record_key, *row.group, span_ptr(within));
    if (!spans || index != spans->size()) {
      return convert(set_record_subrecord_group_element_hex(plugin, record_key, *row.group, index, text,
                                                            span_ptr(within)));
    }
    const Record snapshot = *find_record(plugin, record_key);
    const auto plugin_range = plugin.original_range;
    const auto restore = [&] {
      *detail::find_mutable_record(plugin, record_key) = snapshot;
      plugin.original_range = plugin_range;
    };
    try {
      auto status = convert(insert_record_subrecord_group(plugin, record_key, *row.group, index, span_ptr(within)));
      if (status == FieldStatus::ok) {
        within.reset();
        status = parent_element(plugin, record_key, row, within);
      }
      if (status == FieldStatus::ok) {
        const auto set = set_record_subrecord_group_element_hex(plugin, record_key, *row.group, index, text,
                                                                span_ptr(within), true, false);
        if (set == SubrecordGroupMutationStatus::updated || set == SubrecordGroupMutationStatus::unchanged) {
          return FieldStatus::ok;
        }
        status = convert(set);
      }
      restore();
      return status;
    } catch (...) {
      restore();
      throw;
    }
  }
  if (row.scalar) {
    if (!is_string(*row.scalar)) {
      return convert(set_record_scalar(plugin, record_key, *row.scalar, index, text));
    }
    const auto stored = text::utf8_to_windows1252(text);
    if (!stored) throw std::invalid_argument("text is not valid UTF-8 representable in Windows-1252");
    if (tables != nullptr && record_scalar_is_localized_string(plugin, *row.scalar)) {
      return convert(set_record_localized_string(plugin, record_key, *row.scalar, index, *stored, *tables));
    }
    return convert(set_record_string(plugin, record_key, *row.scalar, index, *stored));
  }
  const FormKey target = text.empty() ? FormKey{} : FormKey::parse(text);
  if (row.link) return convert(set_record_formlink_field(plugin, record_key, *row.link, index, target));
  if (text.empty() && !row.list->nullable) {
    throw std::invalid_argument("this list does not allow NULL elements");
  }
  return convert(schema::set_record_formlink_list_element(plugin, record_key, *row.list, index, target));
}

FieldStatus record_list_size(const Plugin& plugin,
                             const FormKey& record_key,
                             std::string_view path,
                             std::size_t& out_size) {
  out_size = 0;
  const auto signature = path_signature(path);
  if (!signature) return FieldStatus::unknown_path;
  const auto row = resolve(*signature, path);
  if (!row.any()) return FieldStatus::unknown_path;
  if (const auto status = check_record(plugin, record_key, *signature); status != FieldStatus::ok) {
    return status;
  }
  std::optional<std::size_t> size;
  if (row.scalar) {
    size = record_field_count(plugin, record_key, *row.scalar);
  } else if (row.link) {
    size = record_field_count(plugin, record_key, *row.link);
  } else if (row.list) {
    size = schema::record_formlink_list_size(plugin, record_key, *row.list);
  } else if (row.array) {
    size = record_counted_array_size(plugin, record_key, *row.array);
  } else {
    std::optional<SubrecordGroupSpan> within;
    if (const auto status = parent_element(plugin, record_key, row, within); status != FieldStatus::ok) {
      return status;
    }
    const auto spans = subrecord_group_spans(plugin, record_key, *row.group, span_ptr(within));
    if (spans) size = spans->size();
  }
  if (!size) return FieldStatus::record_missing;
  out_size = *size;
  return FieldStatus::ok;
}

FieldStatus insert_record_list_element(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index,
                                       const FormKey* target) {
  const auto signature = path_signature(path);
  if (!signature) return FieldStatus::unknown_path;
  const auto row = resolve(*signature, path);
  if (!row.any()) return FieldStatus::unknown_path;
  if (row.scalar || row.link) return FieldStatus::wrong_kind;
  assign_record_form_keys(plugin);
  if (const auto status = check_record(plugin, record_key, *signature); status != FieldStatus::ok) {
    return status;
  }
  if (row.list) {
    return convert(target == nullptr
                       ? schema::insert_record_formlink_list_null_element(plugin, record_key, *row.list, index)
                       : schema::insert_record_formlink_list_element(plugin, record_key, *row.list, index, *target));
  }
  if (target != nullptr) throw std::invalid_argument("only FormLink lists take a target");
  if (row.array) return convert(insert_record_counted_element(plugin, record_key, *row.array, index));
  std::optional<SubrecordGroupSpan> within;
  if (const auto status = parent_element(plugin, record_key, row, within); status != FieldStatus::ok) {
    return status;
  }
  return convert(insert_record_subrecord_group(plugin, record_key, *row.group, index, span_ptr(within)));
}

FieldStatus remove_record_list_element(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index) {
  const auto signature = path_signature(path);
  if (!signature) return FieldStatus::unknown_path;
  const auto row = resolve(*signature, path);
  if (!row.any()) return FieldStatus::unknown_path;
  if (row.scalar || row.link) return FieldStatus::wrong_kind;
  assign_record_form_keys(plugin);
  if (const auto status = check_record(plugin, record_key, *signature); status != FieldStatus::ok) {
    return status;
  }
  if (row.list) return convert(schema::remove_record_formlink_list_element(plugin, record_key, *row.list, index));
  if (row.array) return convert(remove_record_counted_element(plugin, record_key, *row.array, index));
  std::optional<SubrecordGroupSpan> within;
  if (const auto status = parent_element(plugin, record_key, row, within); status != FieldStatus::ok) {
    return status;
  }
  return convert(remove_record_subrecord_group(plugin, record_key, *row.group, index, span_ptr(within)));
}

FieldStatus resolve_group_path(const Plugin& plugin,
                               const FormKey& record_key,
                               std::string_view path,
                               ScopedGroupPath& out) {
  const auto signature = path_signature(path);
  if (!signature) return FieldStatus::unknown_path;
  const auto row = resolve(*signature, path);
  if (!row.group) return FieldStatus::unknown_path;
  out.group = *row.group;
  out.within.reset();
  return parent_element(plugin, record_key, row, out.within);
}

}  // namespace fo4
