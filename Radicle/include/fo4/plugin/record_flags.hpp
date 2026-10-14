#pragma once

#include "fo4/flags.hpp"
#include "fo4/plugin/container.hpp"

namespace fo4 {

enum class RecordFlagStatus {
  changed,
  unchanged,
  record_missing,
  header_record,
  deleted_bit,
  exterior_relocation_unsupported,
};

enum class MarkDeletedStatus {
  marked,
  already_deleted,
  record_missing,
  local_record,
  header_record,
};

RecordFlagStatus set_record_flags(Plugin& plugin, const FormKey& record_key, RecordFlags::value_type flags);

MarkDeletedStatus mark_override_deleted(Plugin& plugin, const FormKey& record_key);

}  // namespace fo4
