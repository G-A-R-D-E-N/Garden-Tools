#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>

namespace fo4::nif {

// Invariant: the attribute ids are the VertexDesc attribute index, so the flag bit is 1 << id.
enum class VertexAttribute : std::uint32_t {
  Position = 0,
  UV0 = 1,
  UV1 = 2,
  Normal = 3,
  Binormal = 4,
  Color = 5,
  Skinning = 6,
  LandData = 7,
  EyeData = 8,
};

constexpr std::size_t k_vertex_attribute_count = 9;
// Invariant: bit 0x400 of the flag word selects 16-byte positions instead of 8-byte ones.
constexpr std::uint32_t k_vertex_full_precision = 0x400;

// Invariant: the low nibble is the vertex size in four-byte units, each attribute offset is a 4-bit field from bit 2, and the flags start at bit 44.
struct VertexDesc {
  std::uint64_t value{0};

  std::uint32_t size_bytes() const noexcept { return static_cast<std::uint32_t>(value & 0xFU) * 4U; }
  std::uint32_t flags() const noexcept { return static_cast<std::uint32_t>(value >> 44U); }
  // Invariant: attribute N's field sits at bit 4N+4 and holds the byte offset over four, so position's field is the dynamic vertex size instead.
  std::uint32_t attribute_offset(VertexAttribute attribute) const noexcept {
    return static_cast<std::uint32_t>((value >> (4U * static_cast<std::uint32_t>(attribute) + 2U)) & 0x3CU);
  }
  bool has(VertexAttribute attribute) const noexcept {
    return (flags() >> static_cast<std::uint32_t>(attribute) & 1U) != 0;
  }
  bool full_precision() const noexcept { return (flags() & k_vertex_full_precision) != 0; }
  // Invariant: an attribute's width is four bytes, except positions (8 or 16), the 12-byte skin slot and land data, which reserves none.
  std::uint32_t attribute_bytes(VertexAttribute attribute) const noexcept;
};

const char* vertex_attribute_name(VertexAttribute attribute) noexcept;
// Invariant: a vertex is its position slot plus the sum of its other flagged attribute widths, so this is the only place the stride is computed.
std::size_t vertex_bytes_for(std::uint32_t flags) noexcept;
// Requires a tri-shape block; the descriptor sits after the NiAVObject prefix, the bounding sphere and the three references.
VertexDesc read_vertex_desc(const NifFile& nif, std::uint32_t block);

}  // namespace fo4::nif
