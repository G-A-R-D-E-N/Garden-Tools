#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation_sources.hpp"
#include "fo4/hkx/weapon_subgraph.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::hkx {

struct CrashSubgraphId {
  std::uint64_t id{0};
  // Invariant: "animation_offsets_path" when the id came from an AnimationOffsets\<id>.txt path, "bare_number" when it is only the first run of 9 to 20 digits.
  std::string matched;
};

// Constraint: a bare-number match is a guess and must never be shown as a confirmed id.
std::optional<CrashSubgraphId> find_crash_subgraph_id(std::string_view text);

struct SubgraphEntry {
  std::uint64_t id{0};
  std::string manifest;
  std::string source;
  std::vector<std::string> behavior_paths;
  std::vector<std::string> animation_paths;
};

struct SubgraphManifestIndex {
  // Invariant: sorted by id, one entry per id.
  std::vector<SubgraphEntry> entries;
  std::size_t manifest_files{0};
  std::size_t duplicate_ids{0};
  std::size_t unparsed{0};
  std::size_t unreadable{0};
  bool truncated{false};
  bool incomplete{false};
  std::vector<std::string> notes;

  const SubgraphEntry* find(std::uint64_t id) const;
};

// Constraint: the manifest folder is listed once; two files carrying one id keep the first path in sorted order and count the other as a duplicate.
SubgraphManifestIndex discover_subgraphs(const archive::AssetResolver& assets, std::size_t max_manifests);

struct OffsetData {
  std::string source;
  std::size_t bytes{0};
  std::optional<std::string> hint;
};

std::optional<OffsetData> read_offset_data(const archive::AssetResolver& assets, std::uint64_t id);

struct SweepLimits {
  std::size_t max_manifests{20000};
  std::size_t max_behaviors{3000};
  std::size_t max_failures{200};
};

struct SweepFailure {
  std::uint64_t id{0};
  std::string behavior;
  std::size_t gap_total{0};
  std::size_t crash_gaps{0};
  // Invariant: the first k_max_weapon_findings gaps; gap_total holds the exact count.
  std::vector<WeaponGap> gaps;
};

struct SubgraphSweep {
  std::size_t manifests{0};
  std::size_t duplicate_ids{0};
  std::size_t archives{0};
  std::size_t roots{0};
  std::size_t weapon_checked{0};
  std::size_t behaviors_read{0};
  std::size_t behaviors_missing{0};
  std::size_t behaviors_unreadable{0};
  // Invariant: behaviour paths that are not under a Behaviors folder, so they were never opened.
  std::size_t behaviors_skipped{0};
  std::size_t manifests_unread{0};
  std::size_t unchecked{0};
  std::size_t failures_total{0};
  std::vector<SweepFailure> failures;
  bool truncated_manifests{false};
  bool truncated_behaviors{false};
  bool truncated_failures{false};
  bool incomplete{false};
  std::size_t weapon_sets{0};
  std::string weapon_sets_reason;
  std::vector<std::string> notes;

  // Invariant: ok means no subgraph has a gap; complete means no read was cut, missed, missing, skipped or unreadable (a display list cut at its cap is not one, the totals are exact), so ok without complete is not a clean verdict.
  bool ok() const { return failures_total == 0; }
  bool complete() const {
    return !truncated_manifests && !truncated_behaviors && !incomplete && behaviors_missing == 0 &&
           behaviors_unreadable == 0 && behaviors_skipped == 0 && manifests_unread == 0 && unchecked == 0;
  }
};

SubgraphSweep sweep_subgraphs(const archive::AssetResolver& assets, const SweepLimits& limits);

struct DiffLimits {
  std::size_t max_manifests{20000};
  std::size_t max_listed{200};
  std::size_t max_weapon_reads{500};
};

struct SubgraphDiff {
  std::size_t vanilla_manifests{0};
  std::size_t modded_manifests{0};
  std::size_t new_ids_total{0};
  std::size_t gone_ids_total{0};
  std::size_t new_behaviors_total{0};
  std::size_t clusters_total{0};
  std::size_t new_weapon_total{0};
  std::size_t weapon_unread{0};
  std::size_t weapon_missing{0};
  std::size_t weapon_unreadable{0};
  std::size_t weapon_skipped{0};
  std::size_t manifests_unread{0};
  std::vector<std::uint64_t> new_ids;
  std::vector<std::uint64_t> gone_ids;
  std::vector<std::string> new_behaviors;
  // Invariant: sorted by count descending, then behaviour path.
  std::vector<std::pair<std::string, std::size_t>> clusters;
  std::vector<std::string> new_weapon_behaviors;
  bool truncated_manifests{false};
  // Invariant: a display cap only; the totals are exact, so it does not make the diff incomplete.
  bool truncated_lists{false};
  bool truncated_weapon_reads{false};
  bool incomplete{false};
  std::vector<std::string> notes;

  bool complete() const {
    return !truncated_manifests && !truncated_weapon_reads && !incomplete && weapon_missing == 0 &&
           weapon_unreadable == 0 && weapon_skipped == 0 && manifests_unread == 0;
  }
};

SubgraphDiff diff_subgraph_coverage(const archive::AssetResolver& vanilla, const archive::AssetResolver& modded,
                                    const DiffLimits& limits);

// Requires a game Data folder or an MO2 instance; throws AnimationSourceError otherwise.
SubgraphSweep run_subgraph_sweep(const AnimationSourceRequest& request, const SweepLimits& limits);

// Requires an MO2 instance, because the vanilla side is the Data folder alone; throws AnimationSourceError with that reason when none is given.
SubgraphDiff run_subgraph_diff(const AnimationSourceRequest& request, const DiffLimits& limits);

struct CrashInput {
  std::filesystem::path log;
  std::string text;
};

struct CrashLimits {
  std::size_t max_log_bytes{16u * 1024u * 1024u};
  std::size_t max_missing_listed{20};
};

struct CrashSubgraph {
  bool id_found{false};
  std::uint64_t id{0};
  std::string matched;
  bool confirmed{false};
  bool manifest_found{false};
  std::string manifest;
  std::string source;
  std::vector<std::string> behavior_paths;
  std::size_t animation_total{0};
  std::size_t animation_present{0};
  std::size_t missing_total{0};
  std::vector<std::string> missing;
  bool offsets_found{false};
  std::string offsets_source;
  std::size_t offsets_bytes{0};
  std::string offsets_hint;
  std::size_t behaviors_checked{0};
  // Invariant: one "<path>: missing|unreadable|not under a Behaviors folder" line per named behaviour that was not read.
  std::vector<std::string> unchecked_behaviors;
  bool weapon_subgraph{false};
  std::size_t gap_total{0};
  std::vector<WeaponGap> gaps;
  // Invariant: 1 when a manifest carrying the id was found, by its <id>.txt path or by listing the folder; otherwise 0.
  std::size_t manifests{0};
  // Invariant: true when no manifest carries the id, no behaviour was named or read, an archive or mod folder was skipped, or the manifest search was cut, so an empty gap list is not a clean verdict.
  bool incomplete{false};
  // Invariant: non-empty when the input could not be used (no input, both a log and text, log too large or unreadable); the other fields are then unset.
  std::string refusal;
  std::vector<std::string> notes;
};

// Requires a log file or text, not both; a log over max_log_bytes is refused by name and never partly read.
CrashSubgraph analyze_crash_subgraph(const AnimationSourceRequest& request, const CrashInput& input, const CrashLimits& limits);

}  // namespace fo4::hkx
