#pragma once

#include <filesystem>
#include <string>

namespace fo4::io {

inline std::string utf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

}  // namespace fo4::io
