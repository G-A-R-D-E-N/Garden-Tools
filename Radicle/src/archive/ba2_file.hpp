#pragma once

#include "fo4/archive/ba2.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fo4::archive::detail {

inline std::vector<std::byte> read_file_bytes(const std::filesystem::path& path) {
  const auto text = path.u8string();
  const std::string name(text.begin(), text.end());
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw Ba2Error("could not open BA2: " + name);
  const auto end = stream.tellg();
  if (end < 0) throw Ba2Error("could not determine BA2 size: " + name);
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  stream.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw Ba2Error("could not read BA2: " + name);
  }
  return bytes;
}

}  // namespace fo4::archive::detail
