#include <fo4/avm2/utf16.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fo4::avm2 {

std::u16string utf16_from_utf8(std::string_view text) {
  std::u16string units;
  units.reserve(text.size());
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 1;
    char32_t code = lead;
    if (lead >= 0xC2u && lead <= 0xDFu) {
      length = 2;
      code = lead & 0x1Fu;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
      length = 3;
      code = lead & 0x0Fu;
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
      length = 4;
      code = lead & 0x07u;
    }
    bool valid = index + length <= text.size();
    for (std::size_t offset = 1; valid && offset < length; ++offset) {
      const auto continuation = static_cast<unsigned char>(text[index + offset]);
      valid = (continuation & 0xC0u) == 0x80u;
      code = (code << 6) | (continuation & 0x3Fu);
    }
    if (!valid) {
      length = 1;
      code = lead;
    }
    if (code > 0xFFFFu) {
      code -= 0x10000u;
      units.push_back(static_cast<char16_t>(0xD800u + (code >> 10)));
      units.push_back(static_cast<char16_t>(0xDC00u + (code & 0x3FFu)));
    } else {
      units.push_back(static_cast<char16_t>(code));
    }
    index += length;
  }
  return units;
}

std::string utf8_from_utf16(std::u16string_view units) {
  std::string text;
  text.reserve(units.size());
  for (std::size_t index = 0; index < units.size(); ++index) {
    char32_t code = units[index];
    if (code >= 0xD800u && code <= 0xDBFFu && index + 1 < units.size() &&
        units[index + 1] >= 0xDC00u && units[index + 1] <= 0xDFFFu) {
      code = 0x10000u + ((code - 0xD800u) << 10) + (units[index + 1] - 0xDC00u);
      ++index;
    }
    if (code < 0x80u) {
      text.push_back(static_cast<char>(code));
    } else if (code < 0x800u) {
      text.push_back(static_cast<char>(0xC0u | (code >> 6)));
      text.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else if (code < 0x10000u) {
      text.push_back(static_cast<char>(0xE0u | (code >> 12)));
      text.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
      text.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    } else {
      text.push_back(static_cast<char>(0xF0u | (code >> 18)));
      text.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
      text.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
      text.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
    }
  }
  return text;
}

}  // namespace fo4::avm2
