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

// Section 1.3 findings that the report can prove per cell; tokens must stay stable for consumers.
enum class PrevisIssue : std::uint8_t {
  rvis_without_visi = 0,
  tome_missing = 1,
  tome_invalid = 2,
  cdx_row_missing = 3,
  oc_nif_missing = 4,
  xcri_malformed = 5,
  pcmb_stale = 6,
  visi_stale = 7,
  rvis_malformed = 8,
  pcmb_ref_newer = 9,
  pcmb_record_untimed = 10,
  previs_disabled_by_override = 11,
  pcmb_dropped_by_override = 12,
  tome_unreadable = 13,
  csg_rows_missing = 14,
};

std::string_view to_string(PrevisIssue issue) noexcept;

// Constraint: codes 0 to 13 must keep their values and tokens; a new issue takes the next free code.
// The C ABI must reject a code at or above this; a cast would otherwise wrap to a real token.
inline constexpr std::int32_t k_previs_issue_count = 15;

struct CellIssue {
  PrevisIssue code{PrevisIssue::rvis_without_visi};
  std::string detail;
};

struct CellPrevisInput {
  bool has_pcmb{false};
  bool has_visi{false};
  bool has_rvis{false};
  bool rvis_malformed{false};
  bool has_xcri{false};
  bool has_xpri{false};
  // A false presence flag requires a checked index; unchecked answers must skip.
  bool cdx_checked{false};
  bool has_cdx_row{false};
  bool uvd_checked{false};
  bool has_uvd{false};
  bool uvd_valid{false};
  // A tome that exists but could not be read must not be reported as invalid.
  bool uvd_unreadable{false};
  bool oc_nif_checked{false};
  std::uint32_t missing_oc_nifs{0};
  // Invariant: csg_checked stays false unless the container and the cell's meshes were read.
  bool csg_checked{false};
  bool csg_broken{false};
  // Invariant: csg_not_run requires an applicable check that could not run.
  bool csg_not_run{false};
  std::string csg_detail;
  bool pcmb_stale{false};
  bool visi_stale{false};
  // Cross-plugin override findings are outdated-class only: the pinned broken set must not move.
  std::uint32_t override_refs_newer{0};
  std::uint32_t override_records_untimed{0};
  bool previs_disabled_by_override{false};
  bool pcmb_zeroed_by_override{false};
  bool xcri_removed_by_override{false};
  // XCRI bytes present below the measured form version must report a skip.
  bool xcri_unmeasured_version{false};
  // Suppressed XCRI findings must stay out of the report; the caller adds its own skip.
  bool xcri_index_checks_suppressed{false};
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
  // A read failure says nothing about the tome's content, so it must stay apart from issues.
  bool read_failed{false};
  std::vector<std::string> issues;
};

// Constraint: not_run requires an applicable check that could not run; an empty answer is silence.
struct CellCsgAnswer {
  bool checked{false};
  bool broken{false};
  bool not_run{false};
  std::vector<std::string> issues;
};

// A null callback requires a skip: the corresponding index was not supplied.
struct LoadOrderIndexes {
  // Answers for the winning record's plugin; an unreadable CDX requires nullopt.
  std::function<std::optional<bool>(std::size_t plugin, const CdxCellTarget& target)> cdx_row;
  // Missing OC NIF count over the XCRI mesh ids; unindexed NIFs require nullopt.
  std::function<std::optional<std::uint32_t>(const FormKey& cell,
                                             std::span<const std::uint32_t> mesh_ids)>
      missing_oc_nifs;
  // Constraint: a missing container must come back as not_run with its reason, never as a clean answer.
  std::function<std::optional<CellCsgAnswer>(const FormKey& cell,
                                             std::span<const std::uint32_t> mesh_ids)>
      csg_rows;
  // A tome lookup must use the RVIS root cell, or the cell itself when RVIS is absent.
  std::function<TomeLookup(const FormKey& tome_cell)> uvd;
  // A plugin without either container leaves its overrides unmeasured and must skip them.
  std::function<bool(std::size_t plugin)> plugin_ships_previs;
};

// Every winning CELL requires a report on decode error, never an abort; unmeasured layouts skip.
std::vector<CellDiagnosis> diagnose_load_order(const env::Environment& env,
                                               const LoadOrderIndexes& indexes);

}  // namespace fo4::previs
