#pragma once

#include "fo4/hkx/packfile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::hkx {

enum class HkxAnimationKind : std::uint8_t {
  Interleaved = 0,
  Spline = 1,
  View = 2,
};

struct HkxAnimationTrack {
  std::vector<std::array<float, 3>> translations{};
  std::vector<std::array<float, 4>> rotations{};  // x, y, z, w
  std::vector<std::array<float, 3>> scales{};
  std::array<bool, 3> translation_animated{};
  bool rotation_animated{false};
  std::array<bool, 3> scale_animated{};
};

struct HkxAnimationAnnotation {
  float time{0.0f};
  std::string text;
};

struct HkxAnnotationTrack {
  std::string name;
  std::vector<HkxAnimationAnnotation> annotations;
};

struct HkxAnimationBlocks {
  std::size_t count{0};
  std::size_t max_frames_per_block{0};
  float block_duration{0.0f};
};

struct HkxAnimation {
  HkxAnimationKind kind{HkxAnimationKind::View};
  std::string class_name;
  std::int32_t type{0};
  float duration{0.0f};
  std::size_t frame_count{0};
  float frame_duration{0.0f};
  std::int32_t float_track_count{0};
  HkxAnimationBlocks blocks{};
  std::vector<HkxAnimationTrack> tracks;
  std::vector<HkxAnnotationTrack> annotation_tracks;
  std::vector<float> float_tracks{};
};

// Requires a decodable animation class in the packfile; an undecoded class comes back as View.
HkxStatus read_animation(const HkxPackfile& packfile, HkxAnimation& out);

struct HkxAnimationBinding {
  std::string original_skeleton_name;
  std::optional<std::size_t> animation_offset{};
  std::vector<std::int16_t> transform_track_to_bone_indices;
  std::vector<std::int16_t> float_track_to_float_slot_indices;
  std::vector<std::int16_t> partition_indices;
  std::int8_t blend_hint{0};
};

HkxStatus read_binding(const HkxPackfile& packfile, HkxAnimationBinding& out);

}  // namespace fo4::hkx
