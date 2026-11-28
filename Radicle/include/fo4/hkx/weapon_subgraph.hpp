#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/behavior_graph.hpp"
#include "fo4/plugin_core/container.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

struct RaceAnimBlock {
  std::uint32_t set_id{0};
  std::string behavior;
  std::vector<std::string> prefixes;
  std::uint32_t flags{0};
};

// Constraint: a block opens at SAKD and closes at the next SAKD or any other subrecord; a block with no id, no behaviour or no prefix is dropped, as BGS EsPlugin.ReadAnimationSets does.
std::vector<RaceAnimBlock> race_animation_blocks(const Record& race);

struct WeaponTypeSet {
  std::string type;
  std::vector<std::string> prefixes;
};

// Constraint: only blocks whose behaviour file name contains "weapon" count; types sort by uppercase name, as BGS OrdinalIgnoreCase does, and prefixes keep first-seen order without case duplicates.
std::vector<WeaponTypeSet> weapon_type_sets(std::span<const RaceAnimBlock> blocks);

struct WeaponSets {
  std::vector<WeaponTypeSet> sets;
  std::size_t races{0};
  std::size_t blocks{0};
  // Invariant: empty when the master was read; otherwise the named reason the sets are empty.
  std::string reason;
};

// Invariant: filled at most once per cache, so a master is parsed once per call however many behaviours are checked.
struct WeaponSetCache {
  std::optional<WeaponSets> sets;
  std::map<std::string, std::vector<std::string>> folders;
  // Invariant: true once a weapon folder listing or a coverage probe could not be read, so an empty folder list or an absent copy is not evidence of a clean result.
  bool probe_failed{false};
  // Invariant: empty while probe_failed is false; otherwise the named reason the first failure gave.
  std::string probe_reason;
  // Invariant: counts every failed listing or probe, so a caller can tell whether one check raised it.
  std::size_t probe_failures{0};
  // Invariant: the most recent failure reason, which a per-file report names instead of the first one.
  std::string last_probe_reason;
};

// Requires a resolver whose roots are highest priority first; the highest-priority loose Fallout4.esm is read.
const WeaponSets& weapon_sets(const archive::AssetResolver& assets, WeaponSetCache& cache);

inline constexpr std::size_t k_max_weapon_findings = 6;

struct WeaponGap {
  std::string leaf;
  std::string message;
  std::size_t node{0};
  // Invariant: true when no generic Animations\<leaf> copy exists either, so the engine has nothing to fall back to.
  bool crash{false};
};

struct WeaponCheck {
  // Invariant: true when any clip animation names Animations\Weapon\, whether or not the coverage test ran.
  bool weapon_subgraph{false};
  std::vector<WeaponGap> gaps;
  // Invariant: "race_sets", "folders" or empty when the coverage test did not run.
  std::string basis;
};

// Requires project_root to be the meshes-relative project folder with backslashes, for example actors\character.
// Invariant: a probe that cannot be read claims no gap for that clip and sets cache.probe_failed, so an unreadable archive is never reported as a crash.
WeaponCheck check_weapon_subgraph(const archive::AssetResolver& assets, WeaponSetCache& cache, std::string_view project_root,
                                  std::span<const HkxGraphClip> clips);

std::string weapon_gaps_more_message(std::size_t extra);

// Constraint: for an Animations\<leaf> clip, tries the race weapon prefixes first and then every weapon folder of the project, as BGS ModlistScan.Resolve does; returns the meshes-relative path of the first copy found.
// Invariant: `failed` is set when a probe could not be read, so a null result is then not evidence that the clip is absent.
std::optional<std::string> resolve_weapon_clip(const archive::AssetResolver& assets, WeaponSetCache& cache, std::string_view project_root,
                                               std::string_view leaf, bool* failed = nullptr);

}  // namespace fo4::hkx
