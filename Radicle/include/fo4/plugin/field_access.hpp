#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/localized_string_lookup.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/signature.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::localization {
struct LocalizationTables;
}

namespace fo4 {

enum class FieldKind {
  scalar,
  string,
  formlink,
  formlink_list,
  counted_array,
  subrecord_group,
};

struct FieldSchemaRow {
  std::string path;
  FieldKind kind{FieldKind::scalar};
  bool can_insert{false};
  bool repeated{false};
};

struct FieldSchemaExName {
  std::string name;
  std::int64_t value{0};
};

struct FieldSchemaExRow {
  std::string path;
  FieldKind kind{FieldKind::scalar};
  bool can_insert{false};
  bool repeated{false};
  std::string subrecord;
  std::string count_subrecord;
  std::int32_t storage{-1};
  std::int32_t presentation{-1};
  bool embedded{false};
  bool localized{false};
  std::size_t offset{0};
  std::size_t payload_size{0};
  std::size_t stride{0};
  double minimum{0.0};
  double maximum{0.0};
  std::uint16_t min_form_version{0};
  std::uint16_t max_form_version{0};
  std::vector<FieldSchemaExName> names;
};

enum class FieldStatus {
  ok,
  record_missing,
  record_mismatch,
  unknown_path,
  wrong_kind,
  occurrence_missing,
  index_out_of_range,
  target_type_mismatch,
  boundary_unavailable,
  would_empty,
  next_alias_id_too_low,
};

// Invariant: rows come only from the generated scalar, FormLink, counted-array and subrecord-group tables, so a row's path is accepted by the accessors below.
std::vector<FieldSchemaRow> record_field_schema_rows(Signature record);
std::vector<FieldSchemaExRow> record_field_schema_ex_rows(Signature record);

// Constraint: a path is the schema path such as ALCH\Weight and index selects one occurrence; text is UTF-8, a FormKey (LLLLLL:plugin.esp) for link rows, and empty for a null link. strings resolves a localized string id; without it that field throws ScalarError.
FieldStatus read_record_field(const Plugin& plugin,
                              const Record& record,
                              std::string_view path,
                              std::size_t index,
                              std::string& out_text,
                              const LocalizedStringLookup* strings = nullptr,
                              std::size_t plugin_index = 0);

// Constraint: a localized string write in a localized plugin needs tables and throws ScalarError without them; with them it stores a string id and edits the matching table.
FieldStatus write_record_field(Plugin& plugin,
                               const FormKey& record_key,
                               std::string_view path,
                               std::size_t index,
                               std::string_view text,
                               localization::LocalizationTables* tables = nullptr);

FieldStatus record_list_size(const Plugin& plugin,
                             const FormKey& record_key,
                             std::string_view path,
                             std::size_t& out_size);

// Constraint: list, read, insert and remove take a nested subrecord group as Parent[i]\Child (QUST\Stages[1]\LogEntries); its indexes are then relative to parent element i, and a missing parent element is index_out_of_range. A scoped path is not a schema row and the scalar rows stay record-wide.
struct ScopedGroupPath {
  schema::SubrecordGroupDescriptor group;
  std::optional<SubrecordGroupSpan> within;
};

FieldStatus resolve_group_path(const Plugin& plugin,
                               const FormKey& record_key,
                               std::string_view path,
                               ScopedGroupPath& out);

// Constraint: target is read only for FormLink lists (null inserts a null element); counted arrays and subrecord groups insert their schema default element.
FieldStatus insert_record_list_element(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index,
                                       const FormKey* target);

FieldStatus remove_record_list_element(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index);

}  // namespace fo4
