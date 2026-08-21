#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <string_view>

namespace fo4 {

bool set_record_editor_id(Plugin& plugin,
                          const FormKey& record_key,
                          std::string_view editor_id);

}  // namespace fo4
