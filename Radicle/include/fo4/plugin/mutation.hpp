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
};

// Creates an empty local major record using the plugin's HEDR nextFormID,
// advances nextFormID monotonically, and creates/reuses the matching top-level
// GRUP. Light-plugin creation is intentionally refused until the engine's
// exact small-file FormID validity predicate is proven.
FormKey create_local_record(Plugin& plugin, Signature signature);

// Deletes a record owned by this plugin. nextFormID is never decremented or
// recycled; a later creation continues monotonically from the HEDR value.
RecordDeleteStatus delete_local_record(Plugin& plugin, const FormKey& record_key);

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
