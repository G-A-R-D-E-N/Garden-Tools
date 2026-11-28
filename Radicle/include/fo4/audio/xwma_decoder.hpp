#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::audio {

struct XwmaPcm {
  std::uint32_t sample_rate{0};
  std::uint16_t channels{0};
  std::vector<std::int16_t> samples;
  std::size_t bad_packets{0};
};

XwmaPcm decode_xwma(std::span<const std::byte> bytes);

}  // namespace fo4::audio
