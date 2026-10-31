#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace fo4::previs {

// Wire invariant: 16-byte rows ascending by (form, key).
struct CdxRow {
  // word0 invariant: exterior packs signed 12-bit x/y plus kind; interior holds only kind.
  std::uint32_t key{0};
  // word1 must be the full Form-ID of a WRLD (exterior) or interior CELL.
  std::uint32_t form{0};
  std::uint32_t value{0};  // word2 opaque
  std::uint32_t extra{0};  // word3 opaque
};

struct CdxFile {
  std::vector<CdxRow> rows;
};

CdxFile parse_cdx(std::span<const std::byte> data);
std::vector<std::byte> write_cdx(const CdxFile& file);

// Structure check invariant: magic, count/size, ascending rows, interior grid zero.
std::vector<std::string> validate_cdx(std::span<const std::byte> data);

// Kind invariant: interior kinds are 0x80 and up, exterior below.
inline constexpr std::uint8_t k_cdx_interior_kind_min = 0x80;

std::uint8_t cdx_kind(std::uint32_t key) noexcept;
bool cdx_kind_is_interior(std::uint32_t key) noexcept;

struct CdxGrid {
  std::int32_t x{0};
  std::int32_t y{0};
  friend bool operator==(const CdxGrid&, const CdxGrid&) = default;
};

// Exterior coordinates are signed 12-bit fields; encode and decode must round-trip.
CdxGrid decode_cdx_grid(std::uint32_t key) noexcept;
std::uint32_t encode_cdx_grid(const CdxGrid& grid) noexcept;

// A row's cell key: form plus packed grid; interior rows must use grid 0.
struct CdxCellKey {
  std::uint32_t form{0};
  std::uint32_t grid{0};
  friend bool operator==(const CdxCellKey&, const CdxCellKey&) = default;
};

struct CdxCellKeyHash {
  std::size_t operator()(const CdxCellKey& key) const noexcept;
};

// Lookups must be per cell and never scan the 1.46M rows of Fallout4.cdx.
class CdxIndex {
 public:
  explicit CdxIndex(const CdxFile& file);

  bool contains(const CdxCellKey& key) const noexcept;
  bool has_exterior_row(std::uint32_t world_form, const CdxGrid& grid) const noexcept;
  bool has_interior_row(std::uint32_t cell_form) const noexcept;

 private:
  std::unordered_set<std::uint64_t> keys_;
};

}  // namespace fo4::previs
