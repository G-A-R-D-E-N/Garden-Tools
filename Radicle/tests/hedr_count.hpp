#pragma once

#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace fo4::test {

inline constexpr std::size_t k_hedr_count_offset = 34;

inline std::uint32_t stored_hedr_count(std::span<const std::byte> bytes) {
  std::uint32_t count = 0;
  std::memcpy(&count, bytes.data() + k_hedr_count_offset, sizeof(count));
  return count;
}

inline std::uint32_t rebuilt_hedr_count(const fo4::Plugin& plugin) {
  return static_cast<std::uint32_t>(fo4::collect_records(plugin).size() - 1 +
                                    fo4::collect_groups(plugin).size());
}

inline bool hedr_count_only_difference(std::span<const std::byte> written,
                                       std::span<const std::byte> original,
                                       const fo4::Plugin& plugin) {
  if (written.size() != original.size()) return false;
  for (std::size_t i = 0; i < written.size(); ++i) {
    if (written[i] == original[i]) continue;
    if (i < k_hedr_count_offset || i >= k_hedr_count_offset + sizeof(std::uint32_t)) return false;
  }
  return stored_hedr_count(written) == rebuilt_hedr_count(plugin);
}

}  // namespace fo4::test
