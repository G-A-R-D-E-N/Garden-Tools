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
    append_le(out, row.kind);
    append_le(out, row.cell);
    append_le(out, row.value);
    append_le(out, row.extra);
  }
  return out;
}

std::vector<std::string> validate_cdx(std::span<const std::byte> data) {
  try {
    auto file = parse_cdx(data);
    std::vector<std::string> issues;
    for (std::size_t i = 1; i < file.rows.size(); ++i) {
      const auto& a = file.rows[i - 1];
      const auto& b = file.rows[i];
      const auto ta = std::tie(a.cell, a.kind);
      const auto tb = std::tie(b.cell, b.kind);
      if (tb < ta) {
        issues.push_back("CDX rows are not grouped by cell and kind");
        break;
      }
    }
    return issues;
  } catch (const std::exception& e) {
    return {e.what()};
  }
}

bool cdx_has_data_for_cell(const CdxFile& file, std::uint32_t cell_local_id) noexcept {
  const auto want = cell_local_id & 0xFFFFFFu;
  for (const auto& row : file.rows) {
    if ((row.cell & 0xFFFFFFu) == want) return true;
  }
  return false;
}

}  // namespace fo4::previs
