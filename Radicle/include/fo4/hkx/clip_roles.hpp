#pragma once

#include "fo4/hkx/packfile.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

struct HkxBehaviorClip {
  std::string generator;
  std::string animation;
  std::int8_t mode{0};
  float playback_speed{1.0f};
};

// Requires an x64 packfile; a file with no hkbClipGenerator object comes back as MissingObject.
HkxStatus read_behavior_clips(const HkxPackfile& packfile, std::vector<HkxBehaviorClip>& out);

struct HkxClipRole {
  std::string role;
  std::string entry;
  bool loop{false};
  float playback_speed{1.0f};
};

// Constraint: a role comes from the animation file name only, never from the generator name.
std::string_view clip_role_of(std::string_view animation);

// Constraint: the first clip in packfile order wins each role; entries are lowercase `/` archive paths ending .hkx.
std::vector<HkxClipRole> resolve_clip_roles(std::span<const HkxBehaviorClip> clips, std::string_view animation_root,
                                            std::string_view directory);

}  // namespace fo4::hkx
