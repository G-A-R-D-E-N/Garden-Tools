#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::previs {

struct CdxRow {
  std::uint32_t kind{0};
  std::uint32_t cell{0};
  std::uint32_t value{0};
  std::uint32_t extra{0};
};

struct CdxFile {
  std::vector<CdxRow> rows;
};

CdxFile parse_cdx(std::span<const std::byte> data);
std::vector<std::byte> write_cdx(const CdxFile& file);

// Structure check invariant: magic, count/size agreement, rows grouped by (form, grid).
std::vector<std::string> validate_cdx(std::span<const std::byte> data);

// True when any row names this CELL local id (low 24 bits); requires a row scan.
bool cdx_has_data_for_cell(const CdxFile& file, std::uint32_t cell_local_id) noexcept;

}  // namespace fo4::previs
