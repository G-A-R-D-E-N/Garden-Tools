#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <string_view>

namespace fo4 {

enum class RecordCopyStatus {
  inserted,
  replaced,
  already_exists,
  source_missing,
};

bool set_record_editor_id(Plugin& plugin,
                          const FormKey& record_key,
                          std::string_view editor_id);

RecordCopyStatus copy_record_as_override(Plugin& destination,
                                         const Plugin& source,
                                         const FormKey& record_key,
                                         bool overwrite);

}  // namespace fo4
