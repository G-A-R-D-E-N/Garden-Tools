#pragma once

// Read-side invariant: `<plugin> - Geometry.csg` holds bcsg magic and zlib chunks.
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

struct CsgRow {
  std::array<std::byte, k_csg_row_bytes> bytes{};
};

struct CsgFile {
  std::vector<CsgRow> rows;
  std::vector<std::vector<std::byte>> chunks;
};

CsgFile parse_csg(std::span<const std::byte> data);
std::vector<std::byte> write_csg(const CsgFile& file);

// Structure check: magic, counts, table bounds, contiguous chunks whose offset ends at EOF.
std::vector<std::string> validate_csg(std::span<const std::byte> data);

// Decompresses one stored chunk; a bad stream must throw.
std::vector<std::byte> decompress_csg_chunk(std::span<const std::byte> chunk);

}  // namespace fo4::previs
