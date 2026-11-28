#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::audio {

inline constexpr std::uint16_t k_wma_v2_format_tag = 0x0161u;

struct XwmaFormat {
  std::uint16_t format_tag{0};
  std::uint16_t channels{0};
  std::uint32_t sample_rate{0};
  std::uint32_t average_bytes_per_second{0};
  std::uint16_t block_align{0};
  std::uint16_t bits_per_sample{0};

  friend bool operator==(const XwmaFormat&, const XwmaFormat&) = default;
};

struct SoundContainer {
  bool fuz{false};
  std::size_t lip_offset{0};
  std::size_t lip_size{0};
  std::size_t riff_offset{0};
  std::size_t riff_size{0};
  XwmaFormat format;
  std::size_t data_offset{0};
  std::size_t data_size{0};
  std::size_t packet_count{0};

  friend bool operator==(const SoundContainer&, const SoundContainer&) = default;
};

SoundContainer parse_sound_container(std::span<const std::byte> bytes);

}  // namespace fo4::audio
