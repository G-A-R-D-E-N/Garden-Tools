#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fo4::test {

inline std::vector<std::byte> zstr(std::string_view text) {
  std::vector<std::byte> out;
  out.reserve(text.size() + 1);
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

inline std::vector<std::byte> u32_bytes(std::uint32_t value) {
  return {std::byte{static_cast<std::uint8_t>(value)}, std::byte{static_cast<std::uint8_t>(value >> 8)},
          std::byte{static_cast<std::uint8_t>(value >> 16)}, std::byte{static_cast<std::uint8_t>(value >> 24)}};
}

}  // namespace fo4::test
