#include "fo4/previs/uvd_tome.hpp"

#include "fo4/error.hpp"
#include "fo4/previs/uvd.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace fo4::previs {
namespace {

// Constraint: 0x50 is both the page header size and the word retail stores at page + 0x38 on all 9,313 pages.
constexpr std::size_t k_page_header = 0x50;
constexpr std::size_t k_leaf_row = 20;
constexpr std::size_t k_child_record = 16;
// Constraint: retail leaf rows open with 33, so the row tag is fixed and never a page header word.
constexpr std::uint32_t k_leaf_tag = 33;

std::size_t align16(std::size_t value) { return (value + 15u) & ~std::size_t{15}; }

void put_u32(std::vector<std::byte>& out, std::size_t at, std::uint32_t value) {
  std::memcpy(out.data() + at, &value, 4);
}

void put_f32(std::vector<std::byte>& out, std::size_t at, float value) {
  std::memcpy(out.data() + at, &value, 4);
}

void put_u16(std::vector<std::byte>& out, std::size_t at, std::uint16_t value) {
  std::memcpy(out.data() + at, &value, 2);
}

std::uint16_t quantise(float value, float min, float max) {
  const float extent = max - min;
  if (!(extent > 0.0f)) return 0;
  const float normalised = ((value - min) / extent) * 2.0f - 1.0f;
  const float scaled = normalised * static_cast<float>(k_uvd_quantisation);
  const auto clamped = std::clamp(scaled, -static_cast<float>(k_uvd_quantisation),
                                  static_cast<float>(k_uvd_quantisation));
  return static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clamped)));
}

std::size_t page_payload_size(const UvdPage& page) {
  return align16(k_page_header + page.positions.size() / 3 * 6 + page.indices.size() * 2 + 2);
}

// Constraint: the writer and the patcher emit a page through one function, so a patched page is the same record a written one is and no second copy of the header layout can drift.
std::vector<std::byte> encode_page_payload(const UvdPage& page) {
  std::vector<std::byte> out(page_payload_size(page), std::byte{0});
  for (int axis = 0; axis < 3; ++axis) {
    put_f32(out, static_cast<std::size_t>(axis) * 4, page.min[axis]);
    put_f32(out, 0x0C + static_cast<std::size_t>(axis) * 4, page.max[axis]);
  }
  const float extent = std::max({page.max[0] - page.min[0], page.max[1] - page.min[1],
                                 page.max[2] - page.min[2], 1.0f});
  const auto vertex_count = static_cast<std::uint32_t>(page.positions.size() / 3);
  // Constraint: the measured page length is the payload size in Q8 at +0x2C, and bit0 marks a page with geometry.
  put_u32(out, 0x2C, (static_cast<std::uint32_t>(out.size()) << 8u) | (vertex_count > 0 ? 1u : 0u));
  put_f32(out, 0x30, std::max(1.0f, std::round(extent / 64.0f)));
  put_u32(out, 0x34, vertex_count);
  put_u32(out, 0x38, static_cast<std::uint32_t>(k_page_header));
  put_u32(out, 0x3C, static_cast<std::uint32_t>(page.indices.size()));
  std::size_t vertex_at = k_page_header;
  for (std::uint32_t vertex = 0; vertex < vertex_count; ++vertex) {
    const float* position = page.positions.data() + static_cast<std::size_t>(vertex) * 3;
    for (int axis = 0; axis < 3; ++axis) {
      put_u16(out, vertex_at + static_cast<std::size_t>(axis) * 2, quantise(position[axis], page.min[axis], page.max[axis]));
    }
    vertex_at += 6;
  }
  for (const auto index : page.indices) {
    put_u16(out, vertex_at, index);
    vertex_at += 2;
  }
  put_u16(out, vertex_at, k_uvd_index_end);
  return out;
}

std::uint32_t get_u32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  std::memcpy(&value, bytes.data() + at, 4);
  return value;
}

float get_f32(std::span<const std::byte> bytes, std::size_t at) {
  float value = 0.0f;
  std::memcpy(&value, bytes.data() + at, 4);
  return value;
}

}  // namespace

std::vector<std::byte> write_uvd_tome(const UvdTomeRequest& request) {
  const std::size_t page_count = request.pages.size();
  std::size_t leaf_total = 0;
  for (const auto& page : request.pages) leaf_total += page.leaves.size();

  const std::size_t descriptor_start = k_uvd_first_section;
  const std::size_t section_start = descriptor_start + std::max<std::size_t>(page_count, 1) * k_uvd_page_record;
  const std::size_t page_entry_start = section_start;
  // Constraint: retail publishes page offsets as a u32 array, so each table slot is four bytes.
  std::size_t cursor = align16(page_entry_start + page_count * 4);
  std::vector<std::size_t> payload_at;
  payload_at.reserve(page_count);
  for (const auto& page : request.pages) {
    cursor = align16(cursor);
    payload_at.push_back(cursor);
    cursor += page_payload_size(page);
  }
  const std::size_t leaf_table_start = align16(cursor);
  const std::size_t child_start = align16(leaf_table_start + 4 + leaf_total * k_leaf_row);
  std::size_t size = align16(child_start + leaf_total * k_child_record * 2);
  if (size < k_uvd_header_min) size = k_uvd_header_min;

  std::vector<std::byte> out(size, std::byte{0});
  put_u32(out, 0, k_uvd_magic);
  put_u32(out, 8, static_cast<std::uint32_t>(size));
  put_f32(out, 0x0C, 512.0f);
  // Constraint: retail stores three world minimums at 0x14 and three maximums at 0x20, never interleaved pairs.
  for (int axis = 0; axis < 3; ++axis) {
    put_f32(out, 0x14 + static_cast<std::size_t>(axis) * 4, request.world_min[axis]);
    put_f32(out, 0x20 + static_cast<std::size_t>(axis) * 4, request.world_max[axis]);
  }
  put_u32(out, 0x2C, static_cast<std::uint32_t>(page_count * 0x20));
  put_u32(out, 0x30, static_cast<std::uint32_t>(descriptor_start));
  put_u32(out, 0x38, static_cast<std::uint32_t>(page_count));
  put_u32(out, 0x3C, static_cast<std::uint32_t>(descriptor_start + 0x10));
  put_u32(out, 0x7C, static_cast<std::uint32_t>(page_count));
  put_u32(out, 0x80, static_cast<std::uint32_t>(page_entry_start));
  put_u32(out, 0x88, static_cast<std::uint32_t>(descriptor_start + 0x20));
  put_u32(out, 0x8C, static_cast<std::uint32_t>(leaf_total));
  put_u32(out, 0x90, static_cast<std::uint32_t>(leaf_total));
  put_u32(out, 0x94, static_cast<std::uint32_t>(page_count));
  put_u32(out, 0x98, static_cast<std::uint32_t>(descriptor_start + 0x30));
  put_u32(out, 0x9C, static_cast<std::uint32_t>(descriptor_start + 0x40));
  put_u32(out, 0xA0, static_cast<std::uint32_t>(page_entry_start));
  put_u32(out, 0xA4, static_cast<std::uint32_t>(leaf_table_start));
  put_u32(out, 0xA8, static_cast<std::uint32_t>(child_start));
  put_u32(out, 0xAC, static_cast<std::uint32_t>(leaf_total));
  const auto banner = request.banner.empty() ? std::string_view{"T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0"}
                                             : std::string_view{request.banner};
  const std::size_t banner_bytes = std::min(banner.size(), size - k_uvd_banner_offset - 1);
  std::memcpy(out.data() + k_uvd_banner_offset, banner.data(), banner_bytes);

  for (std::size_t page = 0; page < page_count; ++page) {
    const std::size_t at = descriptor_start + page * k_uvd_page_record;
    put_u32(out, at, 3);
    put_u32(out, at + 0x10, 0x7F7FFFFFu);
    put_u32(out, at + 0x20, 1);
    put_f32(out, at + 0x30, 1.0f);
    put_u32(out, at + 0x40, static_cast<std::uint32_t>(page));
  }

  for (std::size_t page = 0; page < page_count; ++page) {
    put_u32(out, page_entry_start + page * 4, static_cast<std::uint32_t>(payload_at[page]));
  }

  std::size_t leaf_index = 0;
  std::size_t child_cursor = child_start;
  for (std::size_t page_index = 0; page_index < page_count; ++page_index) {
    const auto& page = request.pages[page_index];
    const std::size_t at = payload_at[page_index];
    const auto payload = encode_page_payload(page);
    std::memcpy(out.data() + at, payload.data(), payload.size());

    for (const auto& leaf : page.leaves) {
      const std::size_t row = leaf_table_start + 4 + leaf_index * k_leaf_row;
      put_u32(out, row, k_leaf_tag);
      put_u32(out, row + 4, static_cast<std::uint32_t>(child_cursor));
      put_u32(out, row + 8, static_cast<std::uint32_t>(child_cursor + k_child_record));
      put_u32(out, child_cursor, leaf.count);
      put_u32(out, child_cursor + 4, leaf.kind);
      put_u32(out, child_cursor + k_child_record, 0);
      child_cursor += k_child_record * 2;
      ++leaf_index;
    }
  }

  const auto crc = crc32c(std::span<const std::byte>{out}.subspan(8));
  put_u32(out, 4, crc);
  put_u32(out, 8, static_cast<std::uint32_t>(size));
  return out;
}

std::optional<UvdPage> read_uvd_page(std::span<const std::byte> tome, std::size_t page_index) {
  if (tome.size() < k_uvd_first_section + k_page_header) return std::nullopt;
  const auto count = get_u32(tome, 0x38);
  const auto table = get_u32(tome, 0xa0);
  if (page_index >= count || table + 4u * (page_index + 1u) > tome.size()) return std::nullopt;
  const std::size_t at = get_u32(tome, table + static_cast<std::uint32_t>(page_index) * 4u);
  if (at + k_page_header > tome.size()) return std::nullopt;
  UvdPage page;
  for (int axis = 0; axis < 3; ++axis) {
    page.min[axis] = get_f32(tome, at + static_cast<std::size_t>(axis) * 4u);
    page.max[axis] = get_f32(tome, at + 0x0C + static_cast<std::size_t>(axis) * 4u);
  }
  const auto vertices = get_u32(tome, at + 0x34);
  const auto indices = get_u32(tome, at + 0x3C);
  const auto end = at + k_page_header + static_cast<std::size_t>(vertices) * 6u +
                   static_cast<std::size_t>(indices) * 2u + 2u;
  if (end > tome.size()) return std::nullopt;
  page.positions.resize(static_cast<std::size_t>(vertices) * 3u);
  for (std::uint32_t vertex = 0; vertex < vertices; ++vertex) {
    for (int axis = 0; axis < 3; ++axis) {
      std::int16_t code = 0;
      std::memcpy(&code, tome.data() + at + k_page_header + static_cast<std::size_t>(vertex) * 6u +
                         static_cast<std::size_t>(axis) * 2u, 2);
      const float extent = page.max[axis] - page.min[axis];
      page.positions[static_cast<std::size_t>(vertex) * 3u + static_cast<std::size_t>(axis)] =
          page.min[axis] + (static_cast<float>(code) / static_cast<float>(k_uvd_quantisation) * 0.5f + 0.5f) * extent;
    }
  }
  page.indices.resize(indices);
  for (std::uint32_t index = 0; index < indices; ++index) {
    std::uint16_t value = 0;
    std::memcpy(&value, tome.data() + at + k_page_header + static_cast<std::size_t>(vertices) * 6u +
                          static_cast<std::size_t>(index) * 2u, 2);
    page.indices[index] = value;
  }
  return page;
}

std::vector<std::byte> patch_uvd_page(std::span<const std::byte> tome, std::size_t page_index, const UvdPage& page) {
  if (page.positions.size() % 3 != 0) throw parse_error(0, "a patched page needs xyz triples");
  const auto vertex_total = static_cast<std::uint32_t>(page.positions.size() / 3);
  for (const auto index : page.indices) {
    if (index >= vertex_total) throw parse_error(0, "a patched page index exceeds its vertex count");
  }
  if (tome.size() < k_uvd_first_section + k_page_header) throw parse_error(0, "tome is shorter than its first section");
  const std::size_t count = get_u32(tome, 0x38);
  const std::size_t table = get_u32(tome, 0xa0);
  const std::size_t leaf_table = get_u32(tome, 0xa4);
  const std::size_t child_block = get_u32(tome, 0xa8);
  if (page_index >= count) throw parse_error(0, "page index is outside the tome");
  if (table + 4u * count > leaf_table || leaf_table + 4u > child_block || child_block > tome.size()) {
    throw parse_error(0, "tome page table, leaf table and child block are not in order");
  }
  const std::size_t table_end = table + 4u * count;

  std::vector<std::vector<std::byte>> payloads(count);
  std::vector<std::size_t> old_offsets(count);
  for (std::size_t index = 0; index < count; ++index) {
    old_offsets[index] = get_u32(tome, table + index * 4u);
    if (old_offsets[index] < table_end || old_offsets[index] + k_page_header > leaf_table) {
      throw parse_error(0, "a page offset is outside the page area");
    }
    const auto length = std::size_t{get_u32(tome, old_offsets[index] + 0x2C) >> 8u};
    if (length < k_page_header || old_offsets[index] + length > leaf_table) {
      throw parse_error(0, "a page payload does not fit before the leaf table");
    }
    payloads[index].assign(tome.begin() + static_cast<std::ptrdiff_t>(old_offsets[index]),
                           tome.begin() + static_cast<std::ptrdiff_t>(old_offsets[index] + length));
  }
  payloads[page_index] = encode_page_payload(page);

  std::size_t cursor = align16(table + 4u * count);
  std::vector<std::size_t> new_offsets(count);
  for (std::size_t index = 0; index < count; ++index) {
    new_offsets[index] = cursor;
    cursor += payloads[index].size();
  }
  const std::size_t new_leaf = align16(cursor);
  const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(new_leaf) - static_cast<std::ptrdiff_t>(leaf_table);
  const std::size_t new_child = static_cast<std::size_t>(child_block) + static_cast<std::size_t>(delta);
  const std::size_t size = align16(tome.size() + static_cast<std::size_t>(delta));
  if (size > std::numeric_limits<std::uint32_t>::max()) throw parse_error(0, "the patched tome would not fit a 32-bit size");

  std::vector<std::byte> out(size, std::byte{0});
  const std::size_t page_table_end = table + 4u * count;
  std::memcpy(out.data(), tome.data(), std::min(page_table_end, tome.size()));
  for (std::size_t index = 0; index < count; ++index) {
    put_u32(out, table + index * 4u, static_cast<std::uint32_t>(new_offsets[index]));
    std::memcpy(out.data() + new_offsets[index], payloads[index].data(), payloads[index].size());
  }
  // Constraint: leaf rows address the child block, so their two offset words move with it.
  const std::size_t leaf_rows = (child_block - leaf_table - 4u) / k_leaf_row;
  std::memcpy(out.data() + new_leaf, tome.data() + leaf_table, 4u + leaf_rows * k_leaf_row);
  for (std::size_t row = 0; row < leaf_rows; ++row) {
    const std::size_t at = new_leaf + 4u + row * k_leaf_row;
    put_u32(out, at + 4u, get_u32(out, at + 4u) + static_cast<std::uint32_t>(delta));
    put_u32(out, at + 8u, get_u32(out, at + 8u) + static_cast<std::uint32_t>(delta));
  }
  std::memcpy(out.data() + new_child, tome.data() + child_block, tome.size() - child_block);
  put_u32(out, 0xA4, static_cast<std::uint32_t>(new_leaf));
  put_u32(out, 0xA8, static_cast<std::uint32_t>(new_child));
  put_u32(out, 8, static_cast<std::uint32_t>(size));
  const auto crc = crc32c(std::span<const std::byte>{out}.subspan(8));
  put_u32(out, 4, crc);
  return out;
}

}  // namespace fo4::previs

