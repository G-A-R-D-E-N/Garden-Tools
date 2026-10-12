#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace fo4 {

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

// Inclusive [min, max] local FormID range this plugin may allocate for its own
// records, selected by the TES4 SmallFile flag.
//
// Light plugins are 0x001..0xFFF. TESFile::AdjustFormIDFileIndex (Fallout4.exe
// 1.10.163, RVA 0x131f90 -- the symbol dumps say 0x131fa0 and are off by 0x10)
// rebuilds a light record's runtime FormID as
//
//     *id &= 0xFFF;  *id |= 0xFE000000;  *id |= u16 [this+0x372] << 12;
//
// so bits 12..23 of the on-disk local are discarded before the slot is OR'd
// in. A local above 0xFFF does not fail to load -- it silently aliases onto
// another record, which is why the ceiling has to be enforced here: the engine
// will never report the violation. See docs/ESL_FORMIDS.md and issue #26.
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
                                         bool overwrite);

}  // namespace fo4
