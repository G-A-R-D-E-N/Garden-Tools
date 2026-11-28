#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/hkx/animation_sources.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class ModlistScanMode : std::uint8_t { characters, clips };

struct ModlistScanLimits {
  std::size_t max_files{5000};
  std::size_t max_findings{200};
  std::size_t max_details{40};
  std::size_t max_animations{2000};
};

struct ModlistFinding {
  // Invariant: one of unreadable-character, unreadable-behaviour, missing-animations, missing-clip-animations, roundtrip-loss.
  std::string kind;
  std::string path;
  std::string source;
  // Invariant: the first max_details lines; detail_total holds the exact count.
  std::vector<std::string> details;
  std::size_t detail_total{0};
};

struct ModlistScan {
  ModlistScanMode mode{ModlistScanMode::characters};
  std::size_t mod_roots{0};
  std::size_t candidates{0};
  std::size_t scanned{0};
  std::size_t checked{0};
  std::size_t broken{0};
  std::size_t missing{0};
  std::size_t unreadable{0};
  std::size_t round_trip_losses{0};
  std::size_t findings_total{0};
  std::vector<ModlistFinding> findings;
  bool truncated_files{false};
  bool truncated_findings{false};
  bool truncated_animations{false};
  bool incomplete{false};
  std::vector<std::string> notes;

  // Invariant: ok means no missing reference, unreadable file or round-trip loss; complete means no read was cut or missed (a findings list cut at its cap is not one, the totals are exact), so ok without complete is not a clean verdict.
  bool ok() const { return missing == 0 && unreadable == 0 && round_trip_losses == 0; }
  bool complete() const { return !truncated_files && !truncated_animations && !incomplete; }
};

// Requires data_dir to be the game's Data folder, so files it provides are not counted as a mod's; the winning copy of each mod-owned file is judged from the full view.
ModlistScan scan_modlist(const archive::AssetResolver& assets, const std::filesystem::path& data_dir, ModlistScanMode mode,
                         const ModlistScanLimits& limits);

// Requires an MO2 instance, because a modlist scan is about the mods; throws AnimationSourceError with that reason when none is given.
ModlistScan run_modlist_scan(const AnimationSourceRequest& request, ModlistScanMode mode, const ModlistScanLimits& limits);

}  // namespace fo4::hkx
