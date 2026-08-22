#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/scalar.hpp"

#include <string_view>

namespace fo4 {

enum class RecordCopyStatus {
  inserted,
  replaced,
  already_exists,
  source_missing,
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

RecordCopyStatus copy_record_as_override(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record_key,
                                         bool overwrite);

}  // namespace fo4
