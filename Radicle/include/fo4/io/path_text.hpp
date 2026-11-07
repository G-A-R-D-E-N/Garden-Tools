#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace fo4::io {

inline std::string utf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

inline std::filesystem::path from_utf8(std::string_view text) {
  std::u8string bytes;
  bytes.reserve(text.size());
  for (const unsigned char ch : text) bytes.push_back(static_cast<char8_t>(ch));
  return std::filesystem::path(bytes);
}

}  // namespace fo4::io
