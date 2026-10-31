#pragma once

#include "fo4/environment/world.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::previs {

// Section 1.3 findings; each token must stay stable for report consumers.
enum class PrevisIssue : std::uint8_t {
  rvis_without_visi = 0,
  tome_missing = 1,
  tome_invalid = 2,
  cdx_row_missing = 3,
  csg_rows_missing = 4,
  oc_nif_missing = 5,
  xcri_malformed = 6,
  pcmb_stale = 7,
  visi_stale = 8,
  cell_decode_failed = 9,
};

std::string_view to_string(PrevisIssue issue) noexcept;

struct CellIssue {
  PrevisIssue code{PrevisIssue::rvis_without_visi};
  std::string detail;
};

struct CellPrevisInput {
  bool has_pcmb{false};
  bool has_visi{false};
  bool has_rvis{false};
  bool has_xcri{false};
  bool has_xpri{false};
  // A false presence flag requires a checked index; unchecked answers must skip.
  bool cdx_checked{false};
  bool has_cdx_row{false};
  bool csg_checked{false};
  bool has_csg_rows{false};
  bool uvd_checked{false};
  bool has_uvd{false};
  bool uvd_valid{false};
  bool oc_nif_checked{false};
  std::uint32_t missing_oc_nifs{0};
  bool pcmb_stale{false};
  bool visi_stale{false};
  // XCRI bytes present below the measured form version must report a skip.
  bool xcri_unmeasured_version{false};
};

struct CellPrevisStatus {
  bool broken{false};
  bool outdated{false};
  bool skip_unsupported{false};
  std::vector<CellIssue> issues;
  std::vector<CellIssue> outdated_issues;
  std::vector<std::string> skips;
};

CellPrevisStatus diagnose_cell(const CellPrevisInput& input);

struct CellDiagnosis {
  FormKey cell;
  std::size_t plugin{0};
  CellPrevisInput input;
  CellPrevisStatus status;
};

// A cell's CDX location must be WRLD plus XCLC grid, or the cell itself for interior.
struct CdxCellTarget {
  std::optional<FormKey> world;
  std::optional<env::CellGrid> grid;
  FormKey cell;
};

struct TomeLookup {
  bool checked{false};
  bool found{false};
  std::vector<std::string> issues;
};

// A null callback requires a skip: the corresponding index was not supplied.
struct LoadOrderIndexes {
  // Answers for the winning record's plugin; an unreadable CDX requires nullopt.
  std::function<std::optional<bool>(std::size_t plugin, const CdxCellTarget& target)> cdx_row;
  // CSG rows for the winning plugin and cell; unverifiable CSG requires nullopt.
  std::function<std::optional<bool>(std::size_t plugin, const FormKey& cell)> csg_rows;
  // Missing OC NIF count over the XCRI mesh ids; unindexed NIFs require nullopt.
  std::function<std::optional<std::uint32_t>(const FormKey& cell,
                                             std::span<const std::uint32_t> mesh_ids)>
      missing_oc_nifs;
  // A tome lookup must use the RVIS root cell, or the cell itself when RVIS is absent.
  std::function<TomeLookup(const FormKey& tome_cell)> uvd;
};

// Every winning CELL requires a broken report on decode error, never an abort.
std::vector<CellDiagnosis> diagnose_load_order(const env::Environment& env,
                                               const LoadOrderIndexes& indexes);

}  // namespace fo4::previs
