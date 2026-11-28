#pragma once

#include "fo4/archive/assets.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::audio {

enum class SoundContainerKind : std::int32_t { wav = 0, xwma = 1, fuz = 2 };

// Invariant: bits_per_sample is 8 (unsigned samples, as stored) or 16 (little-endian signed); pcm holds whole frames.
struct SoundPcm {
  SoundContainerKind container{SoundContainerKind::wav};
  std::uint32_t sample_rate{0};
  std::uint16_t channels{0};
  std::uint16_t bits_per_sample{0};
  std::size_t bad_packets{0};
  std::vector<std::byte> pcm;
};

struct SoundAsset {
  std::string path;
  SoundPcm pcm;
};

SoundPcm decode_wav(std::span<const std::byte> bytes);
SoundPcm decode_sound(std::span<const std::byte> bytes);
std::vector<std::string> sound_path_candidates(std::string_view name);
std::optional<SoundAsset> read_sound(const archive::AssetResolver& assets, std::string_view name);

}  // namespace fo4::audio
