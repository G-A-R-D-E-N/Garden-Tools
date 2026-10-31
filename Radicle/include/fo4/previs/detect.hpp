#pragma once

#include "fo4/environment/world.hpp"
#include "fo4/previs/cdx.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fo4::previs {

struct CellPrevisInput {
  bool has_pcmb{false};
  bool has_visi{false};
  bool has_rvis{false};
  bool has_xcri{false};
  bool has_xpri{false};
  // A false presence flag requires verified absent: each index answer carries
  // a checked flag, and a needed-but-unchecked index must report a skip.
  bool cdx_checked{false};
  bool has_cdx_row{false};
  bool csg_checked{false};
  bool has_csg_rows{false};
  bool uvd_checked{false};
  bool has_uvd{false};
  bool uvd_valid{false};
  bool pcmb_stale{false};
  bool visi_stale{false};
  // XCRI bytes present below the measured form version must report a skip, never parse.
  bool xcri_unmeasured_version{false};
};

struct CellPrevisStatus {
  bool broken{false};
  // Cells the generator cannot verify must be reported, never approximated.
  bool skip_unsupported{false};
  std::vector<std::string> issues;
  std::vector<std::string> skips;
};

// VISI requires RVIS and a valid tome; precombined cells require XCRI plus
// CDX and CSG rows; stale stamps require restamping.
CellPrevisStatus diagnose_cell(const CellPrevisInput& input);

struct LoadOrderIndexes {
  const CdxFile* cdx{nullptr};
  // Per-plugin CDX; null requires a skip, never a broken verdict.
  std::function<const CdxFile*(std::size_t plugin)> cdx_for_plugin;
  // Cells known to own CSG rows; null requires a skip.
  const std::unordered_set<std::uint32_t>* csg_cells{nullptr};
  // Nullopt when no .uvd exists, else validator issues. Null callback requires a skip.
  std::function<std::optional<std::vector<std::string>>(std::uint32_t cell_local)> uvd_issues;
};

struct CellDiagnosis {
  FormKey cell;
  CellPrevisInput input;
  CellPrevisStatus status;
};

// Every winning CELL requires a broken report on decode error, never an abort.
std::vector<CellDiagnosis> diagnose_load_order(const env::Environment& env,
                                               const LoadOrderIndexes& indexes);

}  // namespace fo4::previs
