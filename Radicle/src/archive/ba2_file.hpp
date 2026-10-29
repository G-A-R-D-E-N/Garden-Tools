#pragma once

#include "fo4/archive/ba2.hpp"
#include "fo4/binary/zlib_bound.hpp"
#include "fo4/io/path_text.hpp"
#include "fo4/text/encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fo4::archive::detail {

inline void require_zlib_expansion_bound(std::size_t packed_size, std::uint32_t unpacked_size) {
  if (unpacked_size > static_cast<std::uint64_t>(packed_size) * binary::k_zlib_max_expansion +
                          binary::k_zlib_slack_bytes) {
    throw Ba2Error("BA2 compressed chunk declares " + std::to_string(unpacked_size) +
                   " bytes from " + std::to_string(packed_size) +
                   " packed bytes; exceeds zlib expansion bound");
  }
}

inline std::vector<std::byte> read_file_bytes(const std::filesystem::path& path) {
  const auto name = io::utf8(path);
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

template <class Entry>
std::vector<std::string> stored_ba2_paths(const std::vector<Entry>& entries, bool has_string_table) {
  std::vector<std::string> stored;
  if (!has_string_table) return stored;
  stored.reserve(entries.size());
  for (const auto& entry : entries) {
    auto bytes = text::utf8_to_windows1252(entry.path);
    if (!bytes) throw Ba2Error("BA2 entry path is not UTF-8 text representable in Windows-1252: " + entry.path);
    stored.push_back(std::move(*bytes));
  }
  return stored;
}

}  // namespace fo4::archive::detail
