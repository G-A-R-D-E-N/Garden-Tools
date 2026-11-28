#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fo4::archive {

struct Ba2Hashes {
  std::uint32_t name{0};
  std::uint32_t extension{0};
  std::uint32_t directory{0};
};

// Constraint: writers must refuse paths at or past this length; they hash to zero.
inline constexpr std::size_t k_max_ba2_path_length = 260;

// BA2 name-hash CRC-32: table-based, init 0, no final XOR.
std::uint32_t ba2_crc32(std::span<const std::byte> data);

// Normalize an archive path the way the authoring tools do: '/' -> '\',
// lowercase, trim leading/trailing '\'. Returns "." for empty paths and for
// paths of 260+ characters (the length a BA2 string table entry must fit).
std::string normalize_ba2_path(std::string_view path);
std::string normalize_ba2_name(std::string_view path);

// Compute the three hashes for an archive path: CRC-32 of the file stem,
// the extension as a little-endian word (first 4 bytes), and CRC-32 of the
// parent directory.
Ba2Hashes compute_ba2_hashes(std::string_view path);

}  // namespace fo4::archive
