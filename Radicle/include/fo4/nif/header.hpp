#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::nif {

struct NifHeaderInfo {
  std::uint32_t file_version{0};
  std::uint32_t user_version{0};
  std::uint32_t block_count{0};
  bool readable{false};
  std::vector<std::string> block_types;
};

// Invariant: never throws; an unreadable header sets readable false and keeps every type name read before the failure.
// Constraint: reads any NetImmerse or Gamebryo version from 5.0.0.1 up, not only 20.2.0.7, so it is cheaper and wider than read_nif.
NifHeaderInfo read_nif_header(std::span<const std::byte> bytes) noexcept;

enum class NifSkinScan : std::int32_t { no_skin = 0, skin = 1, unknown = 2 };

// Invariant: a skin instance type name seen before a failure answers skin; otherwise a readable header answers no_skin and an unreadable one unknown.
NifSkinScan scan_nif_skin(std::span<const std::byte> bytes) noexcept;

}  // namespace fo4::nif
