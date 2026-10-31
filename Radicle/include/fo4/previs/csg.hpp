#pragma once

// Read-side invariant: chunk entries are (compressed size, absolute offset) and
// chunks must be contiguous from the table end to EOF.
// A `.psg`-shaped file must fail closed: its layout is unmeasured.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::previs {

inline constexpr std::size_t k_csg_row_bytes = 20;
inline constexpr std::size_t k_csg_chunk_entry_bytes = 8;
// Measured invariant: every chunk inflates to 65536 bytes except a smaller final chunk.
inline constexpr std::size_t k_csg_inflated_chunk_max = 65536;

struct CsgRow {
  std::array<std::byte, k_csg_row_bytes> bytes{};
};

struct CsgFile {
  std::vector<CsgRow> rows;
  std::vector<std::vector<std::byte>> chunks;
};

CsgFile parse_csg(std::span<const std::byte> data);
std::vector<std::byte> write_csg(const CsgFile& file);

// Structure check: magic, counts, table bounds, contiguous chunk offset ending at EOF, zlib headers.
std::vector<std::string> validate_csg(std::span<const std::byte> data);

// Decompresses one stored chunk; output must not exceed k_csg_inflated_chunk_max.
std::vector<std::byte> decompress_csg_chunk(std::span<const std::byte> chunk);

}  // namespace fo4::previs
