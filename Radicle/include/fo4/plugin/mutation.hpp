#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4 {

namespace localization {
struct LocalizationTables;
struct LocalizedStringTable;
}

enum class RecordCopyStatus {
  inserted,
  replaced,
  already_exists,
  source_missing,
  source_containment_unsupported,
};

enum class ScalarMutationStatus {
  updated,
  unchanged,
  record_missing,
  subrecord_missing,
  next_alias_id_too_low,
};

enum class CountedArrayMutationStatus {
  updated,
  record_missing,
  index_out_of_range,
  subrecord_missing,
  would_empty,
  unchanged,
};

enum class SubrecordGroupMutationStatus {
  updated,
  unchanged,
  record_missing,
  index_out_of_range,
  subrecord_missing,
};

struct SubrecordGroupSpan {
  std::size_t first{0};
  std::size_t end{0};
  // Invariant: the nested tuples of the group's counted sub-run, in record order; empty when the group has none.
  std::vector<SubrecordGroupSpan> runs{};
};

enum class ConditionMutationStatus {
  updated,
  unchanged,
  record_missing,
  multiple_lists,
  no_condition_list,
  alias_structure_invalid,
};

enum class FormLinkFieldMutationStatus {
  updated,
  unchanged,
  record_missing,
  record_mismatch,
  subrecord_missing,
};

enum class RecordDeleteStatus {
  deleted,
  record_missing,
  not_local,
  referenced,
};

enum class RecordRenumberStatus {
  renumbered,
  unchanged,
  record_missing,
  not_local,
  target_exists,
};

enum class LocalizedValueStatus {
  updated,
  unchanged,
  record_missing,
  subrecord_missing,
  field_not_localized,
  plugin_not_localized,
  payload_not_an_id,
  empty_text,
  table_kind_mismatch,
};

LocalizedValueStatus add_record_localized_string(Plugin& plugin,
                                                 const FormKey& record_key,
                                                 Signature subrecord_signature,
                                                 std::size_t index,
                                                 std::string_view text,
                                                 localization::LocalizedStringTable& table);

enum class PluginLocalizationStatus {
  converted,
  already_localized,
  not_localized,
  unconvertible_field,
  unresolved_string_id,
};

struct PluginLocalizationResult {
  PluginLocalizationStatus status{PluginLocalizationStatus::converted};
  std::size_t converted{0};
  std::size_t empty_text{0};
};

PluginLocalizationResult localize_plugin(Plugin& plugin,
                                         localization::LocalizationTables& tables);
PluginLocalizationResult delocalize_plugin(Plugin& plugin,
                                           localization::LocalizationTables& tables);

enum class ReferenceRepointStatus {
  repointed,
  no_references,
  mismatched_owners,
};

struct ReferenceRepointResult {
  ReferenceRepointStatus status{ReferenceRepointStatus::no_references};
  std::size_t references{0};
  std::vector<PluginKey> plugins;
};

// Light plugins cap local IDs at 0xFFF; see docs/code-notes/Radicle.md (local_form_id_range).
std::pair<std::uint32_t, std::uint32_t> local_form_id_range(const Plugin& plugin);
bool allows_hardcoded_range(const Plugin& plugin);

// Writes the HEDR next object ID verbatim, including lowering it; callers own the range policy.
void set_hedr_next_form_id(Plugin& plugin, std::uint32_t next_form_id);

std::optional<std::uint32_t> lowered_light_next_form_id(const Plugin& plugin);

// Advances HEDR nextFormID and returns the reserved local key; creates no record.
FormKey reserve_local_form_key(Plugin& plugin);

FormKey create_local_record(Plugin& plugin, Signature signature);

FormKey create_local_record(Plugin& plugin,
                            Signature signature,
                            std::vector<Subrecord> subrecords);

Group* find_or_create_top_group(Plugin& plugin, Signature signature);

// Deletes the record with its owned child GRUP. Refuses deletion when any
// schema-proven in-plugin FormLink still targets the record or a record inside
// that GRUP from outside it. Unsupported remap surfaces fail closed before
// mutation. next form ID is never decremented or recycled.
RecordDeleteStatus delete_local_record(Plugin& plugin, const FormKey& record_key);
std::vector<FormKey> records_removed_by_delete(const Plugin& plugin, const FormKey& record_key);

bool has_form_id_labelled_child_group(const Plugin& plugin, const FormKey& record_key);

// Constraint: repoint_links false renames the record and its child GRUP labels only; links to the old key are left as they were.
RecordRenumberStatus renumber_local_record(Plugin& plugin,
                                           const FormKey& record_key,
                                           LocalFormId new_local,
                                           bool repoint_links = true);

namespace detail {

// Constraint: mutates plugin directly and may throw midway, so call it only on a transactional copy.
RecordRenumberStatus renumber_local_record_in_place(Plugin& plugin,
                                                    const FormKey& record_key,
                                                    LocalFormId new_local,
                                                    bool repoint_links);

}  // namespace detail

RecordRenumberStatus inject_local_record(Plugin& plugin,
                                         const FormKey& record_key,
                                         const FormKey& new_key);

ReferenceRepointResult repoint_record_references(std::span<Plugin*> plugins,
                                                 const FormKey& from,
                                                 const FormKey& to);

std::string stored_editor_id(std::string_view editor_id);
std::string stored_inline_text(std::string_view text, std::string_view field);

bool set_record_editor_id(Plugin& plugin,
                          const FormKey& record_key,
                          std::string_view editor_id);

enum class RecordNameStatus {
  updated,
  unchanged,
  record_missing,
  not_named_type,
  not_insertable,
  form_mismatch,
  opaque_existing,
  invalid_name,
};

// Constraint: an override copied from a localized master into a plain plugin still holds the master's four-byte string ID, which is not readable text there; replace lets inline text overwrite exactly that shape.
enum class CarriedStringId { refuse, replace };

// Writes the record's own FULL, inserting it at its authoring position when the record has none.
RecordNameStatus set_record_full_name(Plugin& plugin,
                                      const FormKey& record_key,
                                      const schema::LocalizedString& name,
                                      CarriedStringId carried = CarriedStringId::refuse);

enum class RecordNameState {
  not_named_type,
  absent,
  absent_not_insertable,
  present,
  opaque,
};

struct RecordFullName {
  RecordNameState state{RecordNameState::not_named_type};
  // Set only when state is present: a string ID in a localized plugin, inline text otherwise.
  schema::LocalizedString value{};
};

// Reads the FULL that set_record_full_name would write, and says whether an absent one can be inserted.
RecordFullName record_full_name(const Plugin& plugin, const Record& record);

ScalarMutationStatus set_record_scalar(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::ScalarFieldDescriptor& field,
    std::string_view value);

ScalarMutationStatus set_record_scalar(Plugin& plugin,
                                       const FormKey& record_key,
                                       Signature subrecord_signature,
                                       std::string_view value);

FormLinkFieldMutationStatus set_record_formlink_field(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::FormLinkFieldDescriptor& field,
    const FormKey& target);

ScalarMutationStatus set_record_scalar(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value,
                                       bool create_missing = false);

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value,
                                       bool create_missing = false);

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       std::string_view path,
                                       std::size_t index,
                                       std::string_view value);

FormLinkFieldMutationStatus set_record_formlink_field(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::FormLinkFieldDescriptor& field,
    std::size_t index,
    const FormKey& target);

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::ScalarFieldDescriptor& field);

// Constraint: nullopt when the occurrence is absent; text is the form set_record_scalar accepts for a numeric or bytes field, and a string field is UTF-8 for the string setter.
std::optional<std::string> record_scalar_text(const Plugin& plugin,
                                              const Record& record,
                                              const schema::ScalarFieldDescriptor& field,
                                              std::size_t index);

// Constraint: counts the occurrences on the given record, so a caller holding the record never repeats the plugin lookup.
std::size_t record_field_count(const Record& record, const schema::ScalarFieldDescriptor& field);

// Constraint: the stored bytes of one occurrence, undecoded; nullopt when the occurrence is absent.
std::optional<std::vector<std::byte>> record_scalar_bytes(const Record& record,
                                                          const schema::ScalarFieldDescriptor& field,
                                                          std::size_t index);

// Constraint: true only when the plugin is localized and the field is stored as a string-table id.
bool record_scalar_is_localized_string(const Plugin& plugin,
                                       const schema::ScalarFieldDescriptor& field);

// Constraint: nullopt when the occurrence is absent. Throws ScalarError unless the field is a localized string whose slot is 4 bytes.
std::optional<std::uint32_t> record_localized_string_id(const Plugin& plugin,
                                                       const Record& record,
                                                       const schema::ScalarFieldDescriptor& field,
                                                       std::size_t index);

// Constraint: stored_text is Windows-1252; an empty text stores id 0 and adds no table entry. Throws ScalarError unless the field is a localized string in a localized plugin.
ScalarMutationStatus set_record_localized_string(Plugin& plugin,
                                                 const FormKey& record_key,
                                                 const schema::ScalarFieldDescriptor& field,
                                                 std::size_t index,
                                                 std::string_view stored_text,
                                                 localization::LocalizationTables& tables);

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::FormLinkFieldDescriptor& field);

// Constraint: nullopt when the occurrence is absent; a null FormLink reads as raw 0, and the raw value is relative to the record's own plugin.
std::optional<RawFormId> record_formlink_field_raw(const Record& record,
                                                   const schema::FormLinkFieldDescriptor& field,
                                                   std::size_t index);

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              std::string_view path);

// Constraint: nullopt only when the record is missing; an absent array counts as zero.
std::optional<std::size_t> record_counted_array_size(const Plugin& plugin,
                                                     const FormKey& record_key,
                                                     const schema::CountedArrayDescriptor& array);

// Constraint: nullopt when the element is absent; a header-segment array reads at its segment base, and a payload outside the modelled layout reads as absent.
std::optional<std::string> record_counted_array_element_hex(const Record& record,
                                                            const schema::CountedArrayDescriptor& array,
                                                            std::size_t index);

std::optional<std::string> record_subrecord_group_element_hex(const Record& record,
                                                              const schema::SubrecordGroupDescriptor& group,
                                                              std::size_t index,
                                                              const SubrecordGroupSpan* within = nullptr);

CountedArrayMutationStatus insert_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index);

CountedArrayMutationStatus remove_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index);

// Constraint: hex is exactly 2*stride digits; an index equal to the count appends a default element first (not for a header-counted segment array), and an element that does not fit leaves the record unchanged.
CountedArrayMutationStatus set_record_counted_element_hex(Plugin& plugin,
                                                          const FormKey& record_key,
                                                          const schema::CountedArrayDescriptor& array,
                                                          std::size_t index,
                                                          std::string_view hex);

// Constraint: within is one element of the group's parent group; the group's elements and every index are then relative to that element, and an empty group takes new elements at the end of it.
std::optional<std::vector<SubrecordGroupSpan>> subrecord_group_spans(
    const Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group,
    const SubrecordGroupSpan* within = nullptr);

std::vector<SubrecordGroupSpan> subrecord_group_spans(const Record& record,
                                                      const schema::SubrecordGroupDescriptor& group,
                                                      const SubrecordGroupSpan* within = nullptr);

SubrecordGroupMutationStatus insert_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index,
                                                           const SubrecordGroupSpan* within = nullptr);

SubrecordGroupMutationStatus remove_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index,
                                                           const SubrecordGroupSpan* within = nullptr);

// Constraint: hex is the element's payloads in subrecord order, split by the sizes its subrecords already have (at most one variable-length subrecord absorbs the difference); a result the group walk reads differently is rolled back. adopt_members first adds the one set of absent fixed-size optional members whose sizes make the element exactly as long as hex, and throws when several sets fit; with adopt_members the record is restored if the call throws or refuses, unless self_restoring is false because the caller already holds a snapshot it restores from.
SubrecordGroupMutationStatus set_record_subrecord_group_element_hex(Plugin& plugin,
                                                                    const FormKey& record_key,
                                                                    const schema::SubrecordGroupDescriptor& group,
                                                                    std::size_t index,
                                                                    std::string_view hex,
                                                                    const SubrecordGroupSpan* within = nullptr,
                                                                    bool adopt_members = false,
                                                                    bool self_restoring = true);

// Constraint: sets or clears one optional group member; a required member and a group with nested runs must be refused.
SubrecordGroupMutationStatus set_record_subrecord_group_member(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group,
    std::size_t index,
    Signature member,
    bool present,
    const SubrecordGroupSpan* within = nullptr);

SubrecordGroupMutationStatus insert_record_subrecord_group_run(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               const schema::SubrecordGroupDescriptor& group,
                                                               std::size_t run,
                                                               std::size_t index);

SubrecordGroupMutationStatus remove_record_subrecord_group_run(Plugin& plugin,
                                                               const FormKey& record_key,
                                                               const schema::SubrecordGroupDescriptor& group,
                                                               std::size_t run,
                                                               std::size_t index);

inline ConditionMutationStatus set_record_conditions(Plugin& plugin,
                                  const FormKey& record_key,
                                  std::span<const CtdaCondition> conditions) {
  assign_record_form_keys(plugin);
  auto status = ConditionMutationStatus::record_missing;

  visit_records(plugin, [&](Record& record) {
    if (status != ConditionMutationStatus::record_missing ||
        record.header.signature == sig_tes4 || record.form_key != record_key) {
      return;
    }
    require_decodable(record);
    status = ConditionMutationStatus::unchanged;
    const auto window = schema::root_condition_window(record);
    if (window.status == schema::RootConditionStatus::multiple_lists) {
      status = ConditionMutationStatus::multiple_lists;
      return;
    }
    if (window.status == schema::RootConditionStatus::alias_structure_invalid) {
      status = ConditionMutationStatus::alias_structure_invalid;
      return;
    }
    const auto result = write_ctda_conditions_in(record, window.begin, window.end,
                                                 conditions, window.insert_anchor);
    if (result == CtdaScopeWrite::multiple_lists) {
      status = ConditionMutationStatus::multiple_lists;
      return;
    }
    if (result == CtdaScopeWrite::no_list) {
      status = ConditionMutationStatus::no_condition_list;
      return;
    }
    if (result != CtdaScopeWrite::replaced && result != CtdaScopeWrite::inserted) return;
    record.original_range.clear();
    record.header.data_size = 0;
    record.uncompressed_size = 0;
    status = ConditionMutationStatus::updated;
  });

  if (status == ConditionMutationStatus::updated) plugin.original_range.clear();
  return status;
}

RecordCopyStatus copy_record_as_override(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record_key,
                                         bool overwrite,
                                         FormLinkTypeLookup type_lookup = {});

struct RecordDeepCopyResult {
  RecordCopyStatus status{RecordCopyStatus::source_missing};
  FormKey key{};
  std::vector<FormKey> children;
};

RecordDeepCopyResult copy_record_deep(Plugin& destination,
                                      const Plugin& source,
                                      const FormKey& record,
                                      bool overwrite);

}  // namespace fo4
