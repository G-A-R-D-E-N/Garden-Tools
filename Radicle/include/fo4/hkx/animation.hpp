#pragma once

#include "fo4/hkx/packfile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::hkx {

enum class HkxAnimationKind : std::uint8_t {
  Interleaved = 0,
  Spline = 1,
  View = 2,
  // hkaLosslessCompressedAnimation: samples addressed by channel offset, no spline payload.
  Lossless = 3,
  Predictive = 4,
  Quantized = 5,
};

// Invariant: this is the one spelling of a kind, so the summary text and a consumer's report cannot disagree.
const char* hkx_animation_kind_name(HkxAnimationKind kind);

// Invariant: every class read_animation treats as a clip ends with "Animation"; a skeleton or a container does not.
bool is_animation_class(std::string_view class_name) noexcept;

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
// Interleaved, spline, lossless, predictive and quantized decode; other classes must stay View.
HkxStatus read_animation(const HkxPackfile& packfile, HkxAnimation& out);

enum class HkxMotionKind : std::uint8_t {
  None = 0,
  Default = 1,
  Unsupported = 2,
};

// Constraint: samples are (x, y, z, w); w is yaw in radians about up; units and axes as stored in the file.
struct HkxExtractedMotion {
  HkxMotionKind kind{HkxMotionKind::None};
  std::string class_name;
  std::array<float, 3> up{0.0f, 0.0f, 1.0f};
  std::array<float, 3> forward{0.0f, 1.0f, 0.0f};
  float duration{0.0f};
  std::vector<std::array<float, 4>> samples;
};

struct HkxMotionSummary {
  std::array<float, 3> net_translation{};
  float net_turn_radians{0.0f};
  float mean_planar_speed{0.0f};
  float mean_turn_rate{0.0f};
};

// Requires a decodable animation class; a clip with no extracted motion comes back as kind None.
HkxStatus read_extracted_motion(const HkxPackfile& packfile, HkxExtractedMotion& out);

// Constraint: the planar projection uses the motion's up vector.
HkxMotionSummary summarize_motion(const HkxExtractedMotion& motion);

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
