#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/flags.hpp"
#include "fo4/plugin/container.hpp"

namespace fo4 {

enum class RecordFlagStatus {
  changed,
  unchanged,
  record_missing,
  header_record,
  deleted_bit,
  target_cell_missing,
};

enum class MarkDeletedStatus {
  marked,
  already_deleted,
  record_missing,
  local_record,
  header_record,
};

enum class UndeleteStatus {
  undeleted,
  not_deleted,
  record_missing,
  header_record,
  not_a_reference,
  incomplete_reference,
  injected_record,
};

RecordFlagStatus set_record_flags(Plugin& plugin, const FormKey& record_key, RecordFlags::value_type flags);

MarkDeletedStatus mark_override_deleted(Plugin& plugin, const FormKey& record_key);

// load_order must hold the record's owner plugin, or the record reads as injected_record.
UndeleteStatus undelete_and_disable_reference(Plugin& plugin,
                                             const env::Environment& load_order,
                                             const FormKey& record_key);

}  // namespace fo4
