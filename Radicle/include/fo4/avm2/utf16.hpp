#pragma once

#include <string>
#include <string_view>

namespace fo4::avm2 {

/* Requires UTF-8 input. */
std::u16string utf16_from_utf8(std::string_view text);
/* Requires UTF-16 input. */
std::string utf8_from_utf16(std::u16string_view units);

}  // namespace fo4::avm2
