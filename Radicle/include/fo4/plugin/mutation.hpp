#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/schema/ctda.hpp"
#include "fo4/schema/scalar.hpp"

#include <span>
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

// Advances HEDR nextFormID and returns the reserved local key; creates no record.
FormKey reserve_local_form_key(Plugin& plugin);

FormKey create_local_record(Plugin& plugin, Signature signature);

// Refuses deletion when any schema-proven in-plugin FormLink still targets the
// record. Unsupported remap surfaces fail closed before mutation. nextFormID
// is never decremented or recycled.
RecordDeleteStatus delete_local_record(Plugin& plugin, const FormKey& record_key);

RecordRenumberStatus renumber_local_record(Plugin& plugin,
                                           const FormKey& record_key,
                                           LocalFormId new_local);

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

// Replaces the record's CTDA condition list with freshly serialized
// conditions (see write_ctda_conditions for the subrecord-level contract).
// Returns false when the record is missing. When the record exists, an
// unchanged/empty CTDA list remains a successful no-op. Any mutation clears
// the current Radicle original-range caches so the writer serializes the new
// subrecords instead of reusing stale bytes.
inline bool set_record_conditions(Plugin& plugin,
                                  const FormKey& record_key,
                                  std::span<const CtdaCondition> conditions) {
  assign_record_form_keys(plugin);
  bool found = false;
  bool changed = false;

  visit_records(plugin, [&](Record& record) {
    if (found || record.header.signature == sig_tes4 || record.form_key != record_key) {
      return;
    }
    found = true;
    if (!write_ctda_conditions(record, conditions)) return;
    record.original_range.clear();
    record.decompressed_payload.clear();
    record.header.data_size = 0;
    record.uncompressed_size = 0;
    changed = true;
  });

  if (!found) return false;
  if (changed) plugin.original_range.clear();
  return true;
}

RecordCopyStatus copy_record_as_override(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record_key,
                                         bool overwrite);

}  // namespace fo4
