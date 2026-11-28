#pragma once

// Constraint: a generated tome comes only from the cell's own geometry, never from a vanilla file.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fo4::previs {

// Constraint: the vertex-run length equation at +0x50 scores 0/40 on retail tomes, so this int16 xyz packing is the writer's own layout.
inline constexpr int k_uvd_quantisation = 16384;
inline constexpr std::uint16_t k_uvd_index_end = 0x7FFFu;
inline constexpr std::size_t k_uvd_first_section = 0x150;
inline constexpr std::size_t k_uvd_page_record = 0x70;

struct UvdLeaf {
  // Constraint: leaf count/kind stay a fallback: byte range childAt..childAt+8, scored 2/40 at best by tools/previs/uvd_search.mjs.
  std::uint32_t count{0};
  std::uint32_t kind{0};
};

struct UvdPage {
  float min[3]{};
  float max[3]{};
  // Constraint: positions are xyz triples in cell units and indices stay u16, so one page is one mesh.
  std::vector<float> positions;
  std::vector<std::uint16_t> indices;
  std::vector<UvdLeaf> leaves;
};

struct UvdTomeRequest {
  float world_min[3]{};
  float world_max[3]{};
  std::vector<UvdPage> pages;
  std::string banner;
};

// Constraint: every count and offset in the result is computed from the bytes written, so no vanilla constant leaks in.
std::vector<std::byte> write_uvd_tome(const UvdTomeRequest& request);

// Constraint: patching one page must not disturb another page's bytes, so the reader and writer share one page record.
std::optional<UvdPage> read_uvd_page(std::span<const std::byte> tome, std::size_t page_index);

// Constraint: the tome is rebuilt from its own page table, so a patched page keeps every other payload byte and the table, leaf rows and total size follow the new payload length; the cell body CRC is recomputed over [8, size).
std::vector<std::byte> patch_uvd_page(std::span<const std::byte> tome, std::size_t page_index, const UvdPage& page);

}  // namespace fo4::previs

