#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/formlink_lists.hpp"
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

RecordRenumberStatus renumber_local_record(Plugin& plugin,
                                           const FormKey& record_key,
                                           LocalFormId new_local);

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
                                       std::string_view value);

ScalarMutationStatus set_record_string(Plugin& plugin,
                                       const FormKey& record_key,
                                       const schema::ScalarFieldDescriptor& field,
                                       std::size_t index,
                                       std::string_view value);

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

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              const schema::FormLinkFieldDescriptor& field);

std::optional<std::size_t> record_field_count(const Plugin& plugin,
                                              const FormKey& record_key,
                                              std::string_view path);

CountedArrayMutationStatus insert_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index);

CountedArrayMutationStatus remove_record_counted_element(Plugin& plugin,
                                                         const FormKey& record_key,
                                                         const schema::CountedArrayDescriptor& array,
                                                         std::size_t index);

std::optional<std::vector<SubrecordGroupSpan>> subrecord_group_spans(
    const Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group);

SubrecordGroupMutationStatus insert_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index);

SubrecordGroupMutationStatus remove_record_subrecord_group(Plugin& plugin,
                                                           const FormKey& record_key,
                                                           const schema::SubrecordGroupDescriptor& group,
                                                           std::size_t index);

// Constraint: sets or clears one optional group member; a required member and a group with nested runs must be refused.
SubrecordGroupMutationStatus set_record_subrecord_group_member(
    Plugin& plugin,
    const FormKey& record_key,
    const schema::SubrecordGroupDescriptor& group,
    std::size_t index,
    Signature member,
    bool present);

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
    record.decompressed_payload.clear();
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
