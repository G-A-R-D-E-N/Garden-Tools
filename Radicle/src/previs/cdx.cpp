#include "fo4/previs/cdx.hpp"

#include "fo4/error.hpp"

#include <cstring>
#include <string_view>
#include <tuple>

namespace fo4::previs {
namespace {

constexpr std::string_view k_magic = "bcdx";
constexpr std::size_t k_header_bytes = 8;
constexpr std::size_t k_row_bytes = 16;

std::uint32_t u32le(std::span<const std::byte> data, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, data.data() + at, 4);
  return v;
}

void append_le(std::vector<std::byte>& out, std::uint32_t v) {
  out.push_back(static_cast<std::byte>(v & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
}

std::uint64_t pack_key(const CdxCellKey& key) noexcept {
  return static_cast<std::uint64_t>(key.form) << 32u | key.grid;
}

}  // namespace

CdxFile parse_cdx(std::span<const std::byte> data) {
  if (data.size() < k_header_bytes) throw parse_error(0, "CDX is shorter than its header");
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<char>(data[i]) != k_magic[i]) throw parse_error(0, "CDX has a bad magic");
  }
  const auto count = u32le(data, 4);
  const std::uint64_t want = k_header_bytes + std::uint64_t{k_row_bytes} * count;
  if (want != data.size()) throw parse_error(4, "CDX count does not match its size");
  CdxFile file;
  file.rows.reserve(count);
  for (std::uint64_t i = 0; i < count; ++i) {
    const std::size_t at = k_header_bytes + static_cast<std::size_t>(i) * k_row_bytes;
    file.rows.push_back(CdxRow{u32le(data, at), u32le(data, at + 4), u32le(data, at + 8),
                               u32le(data, at + 12)});
  }
  return file;
}

std::vector<std::byte> write_cdx(const CdxFile& file) {
  if (file.rows.size() > 0xFFFFFFFFu) throw parse_error(0, "CDX row count exceeds u32");
  std::vector<std::byte> out;
  out.reserve(k_header_bytes + file.rows.size() * k_row_bytes);
  for (const char c : k_magic) out.push_back(static_cast<std::byte>(c));
  append_le(out, static_cast<std::uint32_t>(file.rows.size()));
  for (const auto& row : file.rows) {
    append_le(out, row.key);
    append_le(out, row.form);
    append_le(out, row.value);
    append_le(out, row.extra);
  }
  return out;
}

std::uint8_t cdx_kind(std::uint32_t key) noexcept {
  return static_cast<std::uint8_t>(key & 0xFFu);
}

bool cdx_kind_is_interior(std::uint32_t key) noexcept {
  return cdx_kind(key) >= k_cdx_interior_kind_min;
}

CdxGrid decode_cdx_grid(std::uint32_t key) noexcept {
  const auto raw_x = static_cast<std::int32_t>((key >> 12u) & 0xFFFu);
  const auto raw_y = static_cast<std::int32_t>(key & 0xFFFu);
  return CdxGrid{raw_x >= 0x800 ? raw_x - 0x1000 : raw_x, raw_y >= 0x800 ? raw_y - 0x1000 : raw_y};
}

std::uint32_t encode_cdx_grid(const CdxGrid& grid) noexcept {
  return (static_cast<std::uint32_t>(grid.x) & 0xFFFu) << 12u |
         (static_cast<std::uint32_t>(grid.y) & 0xFFFu);
}

CdxIndex::CdxIndex(const CdxFile& file) {
  keys_.reserve(file.rows.size() / 4 + 1);
  for (const auto& row : file.rows) {
    keys_.insert(pack_key(CdxCellKey{row.form, row.key >> 8u}));
  }
}

bool CdxIndex::contains(const CdxCellKey& key) const noexcept {
  return keys_.contains(pack_key(key));
}

bool CdxIndex::has_exterior_row(std::uint32_t world_form, const CdxGrid& grid) const noexcept {
  return contains(CdxCellKey{world_form, encode_cdx_grid(grid)});
}

bool CdxIndex::has_interior_row(std::uint32_t cell_form) const noexcept {
  return contains(CdxCellKey{cell_form, 0});
}

std::vector<std::string> validate_cdx(std::span<const std::byte> data) {
  CdxFile file;
  try {
    file = parse_cdx(data);
  } catch (const std::exception& e) {
    return {e.what()};
  }
  std::vector<std::string> issues;
  for (std::size_t i = 1; i < file.rows.size(); ++i) {
    const auto& a = file.rows[i - 1];
    const auto& b = file.rows[i];
    if (std::tie(b.form, b.key) < std::tie(a.form, a.key)) {
      issues.push_back("CDX rows are not ascending by (form, key)");
      break;
    }
  }
  for (const auto& row : file.rows) {
    if (!cdx_kind_is_interior(row.key)) continue;
    if ((row.key >> 8u) != 0) {
      issues.push_back("CDX interior row carries a nonzero grid");
      break;
    }
  }
  return issues;
}

}  // namespace fo4::previs
