#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::audio {

inline constexpr std::uint32_t k_lip_modifier_count = 17;
inline constexpr float k_lip_frames_per_second = 30.0f;

inline constexpr std::uint32_t k_lip_flag_compressed = 1;
inline constexpr std::uint32_t k_lip_flag_big_endian = 2;
inline constexpr std::uint32_t k_lip_flag_has_modifiers = 4;
inline constexpr std::uint32_t k_lip_flag_variable_targets = 8;

struct LipAnimation {
  std::uint32_t frame_count{0};
  std::int32_t preroll_frames{0};
  std::uint32_t phoneme_count{0};
  std::uint32_t modifier_count{0};
  std::uint32_t flags{0};
  std::uint32_t clamped_cells{0};
  float max_clamped_magnitude{0.0f};
  std::vector<float> weights;

  std::uint32_t slot_count() const { return phoneme_count + modifier_count; }
  float weight(std::uint32_t frame, std::uint32_t slot) const {
    return weights[static_cast<std::size_t>(frame) * slot_count() + slot];
  }
};

std::span<const std::byte> fuz_lip_block(std::span<const std::byte> fuz);

LipAnimation parse_lip(std::span<const std::byte> bytes);

}  // namespace fo4::audio
