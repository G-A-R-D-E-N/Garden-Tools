#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class HkxTrackNameSource : std::uint8_t {
  None = 0,
  Explicit,
  Sibling,
  Embedded,
};

const char* hkx_track_name_source_name(HkxTrackNameSource source);

using HkxReadFilter = std::function<bool(const std::filesystem::path&)>;

struct HkxTrackNames {
  std::vector<std::string> names;
  HkxTrackNameSource source{HkxTrackNameSource::None};
  std::filesystem::path skeleton;
};

// Constraint: a track with no map entry, a bone outside the skeleton or no skeleton is named "track N".
std::string track_name(const HkxAnimationBinding& binding, const HkxSkeleton* skeleton, std::size_t track);

// Constraint: looks only in the CharacterAssets folder beside the Animations ancestor, then in the animation's own folder; nullopt when no file there reads as a skeleton. A candidate the allowed filter refuses is never opened, and one with fewer bones than the binding's track map reaches is skipped.
std::optional<std::filesystem::path> find_sibling_skeleton(const std::filesystem::path& animation_path,
                                                           const HkxAnimationBinding* binding = nullptr,
                                                           const HkxReadFilter& allowed = {});

// Constraint: skeleton order is the explicit path, the sibling skeleton, then the one in the animation packfile; one that cannot be read is skipped, never an error.
HkxTrackNames name_tracks(const std::filesystem::path& animation_path, const HkxPackfile& animation, const HkxAnimationBinding& binding,
                          std::size_t track_count, const std::filesystem::path& explicit_skeleton = {},
                          const HkxReadFilter& allowed = {});

}  // namespace fo4::hkx
