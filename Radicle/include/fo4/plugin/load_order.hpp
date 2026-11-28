#pragma once

#include "fo4/formid.hpp"
#include "fo4/plugin/container.hpp"

#include <span>

namespace fo4 {

RuntimeFormIdContext build_runtime_form_id_context(
    std::span<const Plugin* const> load_order);

}  // namespace fo4
