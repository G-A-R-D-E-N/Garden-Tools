#pragma once

#include "fo4/hkx/animation.hpp"
#include "fo4/hkx/packfile.hpp"
#include "fo4/hkx/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::hkx {

// Invariant: an undriven flag is set only when the channel is not animated AND the bone's rest value is not the identity, within the thresholds in channel_audit.cpp.
struct HkxChannelTrack {
  std::size_t track{0};
  std::int32_t bone{-1};
  std::string bone_name;
  bool translation_undriven{false};
  bool rotation_undriven{false};
  bool scale_undriven{false};
  float rest_translation_length{0.0f};
  float rest_rotation_degrees{0.0f};
  float rest_scale_deviation{0.0f};
};

struct HkxChannelAudit {
  std::size_t tracks{0};
  std::size_t no_translation{0};
  std::size_t no_rotation{0};
  std::size_t no_scale{0};
  std::size_t offset_translation{0};
  std::size_t offset_rotation{0};
  std::size_t offset_scale{0};
  float max_translation_offset{0.0f};
  float max_rotation_degrees{0.0f};
  std::vector<HkxChannelTrack> offsets;
};

// Requires one in-range, unique bone per track like sample_pose: a bad map is BadSection and a View clip is UnsupportedLayout, never a guessed identity map.
HkxStatus audit_channels(const HkxSkeleton& skeleton, const HkxAnimation& animation, const HkxAnimationBinding& binding,
                         HkxChannelAudit& out);

}  // namespace fo4::hkx
