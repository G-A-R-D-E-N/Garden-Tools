#include "fo4/audio/lip_file.hpp"

#include "fo4/error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace fo4::audio {
namespace {

constexpr std::size_t k_fuz_header_size = 12;
constexpr std::size_t k_fixed_header_size = 14;
constexpr std::uint32_t k_lip_version = 1;
constexpr std::uint32_t k_tom3_version = 0x334D4F54u;
constexpr std::uint32_t k_known_flags = 15;
constexpr std::uint32_t k_max_phoneme_count = 128;
constexpr std::uint16_t k_start_token_bias = 3;

std::uint8_t byte_at(std::span<const std::byte> bytes, std::size_t offset) {
  return std::to_integer<std::uint8_t>(bytes[offset]);
}

std::uint16_t u16_at(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(byte_at(bytes, offset) | (byte_at(bytes, offset + 1) << 8u));
}

std::uint32_t u32_at(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(u16_at(bytes, offset)) | (static_cast<std::uint32_t>(u16_at(bytes, offset + 2)) << 16u);
}

bool is_fuz(std::span<const std::byte> bytes) {
  return bytes.size() >= 4 && byte_at(bytes, 0) == 'F' && byte_at(bytes, 1) == 'U' && byte_at(bytes, 2) == 'Z' &&
         byte_at(bytes, 3) == 'E';
}

struct HeaderLayout {
  std::size_t preroll_offset{0};
  bool has_preroll{false};
  std::size_t phoneme_offset{0};
};

void require(std::span<const std::byte> bytes, std::size_t size) {
  if (bytes.size() < size) throw parse_error(bytes.size(), "lip block ends inside its header");
}

HeaderLayout header_layout(std::span<const std::byte> bytes) {
  require(bytes, 16);
  const auto word14 = u16_at(bytes, 14);
  if (word14 == 3) return {16, true, 20};
  if (word14 == 7) return {0, false, 16};
  if (byte_at(bytes, 14) == 0) {
    require(bytes, 17);
    const auto word15 = u16_at(bytes, 15);
    if (word15 == 2) return {17, true, 21};
    if (word15 == 6) return {0, false, 17};
    if (word15 == 1) {
      require(bytes, 19);
      const auto word17 = u16_at(bytes, 17);
      if (word17 == 2) return {19, true, 23};
      if (word17 == 6) return {0, false, 19};
    }
  }
  throw parse_error(14, "lip header variant is not recognised");
}

std::size_t decoded_size(std::span<const std::byte> payload, std::size_t leading_bytes, std::size_t limit) {
  std::size_t produced = leading_bytes;
  std::size_t at = 0;
  while (at < payload.size() && produced <= limit) {
    if (payload[at] != std::byte{0}) {
      ++produced;
      ++at;
      continue;
    }
    if (payload.size() - at < 3) throw parse_error(at, "lip payload ends inside a zero run");
    produced += u16_at(payload, at + 1);
    at += 3;
  }
  return produced;
}

}  // namespace

std::span<const std::byte> fuz_lip_block(std::span<const std::byte> fuz) {
  if (!is_fuz(fuz) || fuz.size() < k_fuz_header_size) {
    throw parse_error(0, "sound payload is not a FUZE container");
  }
  if (u32_at(fuz, 4) != 1) {
    throw parse_error(4, "FUZE version is not 1");
  }
  const std::size_t lip_size = u32_at(fuz, 8);
  if (lip_size > fuz.size() - k_fuz_header_size) {
    throw parse_error(8, "FUZE lip size exceeds the remaining data");
  }
  return fuz.subspan(k_fuz_header_size, lip_size);
}

LipAnimation parse_lip(std::span<const std::byte> bytes) {
  if (is_fuz(bytes)) bytes = fuz_lip_block(bytes);

  if (bytes.empty()) {
    throw parse_error(0, "lip block is empty");
  }
  require(bytes, k_fixed_header_size);
  const auto version = u32_at(bytes, 0);
  if (version == k_tom3_version) {
    throw parse_error(0, "TOM3 lip files are not supported");
  }
  if (version != k_lip_version) {
    throw parse_error(0, "lip version is not 1");
  }
  LipAnimation lip;
  lip.flags = u32_at(bytes, 8);
  if ((lip.flags & ~k_known_flags) != 0) {
    throw parse_error(8, "lip flags contain unknown bits");
  }
  if ((lip.flags & k_lip_flag_big_endian) != 0) {
    throw parse_error(8, "big-endian lip files are not supported");
  }
  if ((lip.flags & k_lip_flag_compressed) == 0) {
    throw parse_error(8, "uncompressed lip files are not supported");
  }

  const auto layout = header_layout(bytes);
  const std::size_t header_size = layout.phoneme_offset + 4;
  require(bytes, header_size);

  lip.frame_count = u16_at(bytes, 12);
  lip.preroll_frames = layout.has_preroll ? static_cast<std::int32_t>(u32_at(bytes, layout.preroll_offset)) : 0;
  lip.phoneme_count = u16_at(bytes, layout.phoneme_offset);
  lip.modifier_count = (lip.flags & k_lip_flag_has_modifiers) != 0 ? k_lip_modifier_count : 0;
  if (lip.frame_count == 0) {
    throw parse_error(12, "lip has no frames");
  }
  if (lip.phoneme_count == 0 || lip.phoneme_count > k_max_phoneme_count) {
    throw parse_error(layout.phoneme_offset, "lip phoneme count is out of range");
  }

  const std::size_t grid_bytes = static_cast<std::size_t>(lip.frame_count) * lip.slot_count() * sizeof(float);
  const std::uint16_t start_token = u16_at(bytes, layout.phoneme_offset + 2);
  if (start_token < k_start_token_bias) {
    throw parse_error(layout.phoneme_offset + 2, "lip start token is out of range");
  }
  const std::size_t leading_bytes = start_token - k_start_token_bias;
  const auto payload = bytes.subspan(header_size);
  const std::size_t produced = decoded_size(payload, leading_bytes, grid_bytes);
  if (produced > grid_bytes) {
    throw parse_error(header_size, "lip payload is longer than its declared frame grid");
  }
  if (produced < grid_bytes) {
    throw parse_error(header_size, "lip payload is shorter than its declared frame grid");
  }

  std::vector<std::byte> grid(grid_bytes, std::byte{0});
  std::size_t out = std::min(leading_bytes, grid_bytes);
  std::size_t at = 0;
  while (at < payload.size() && out < grid_bytes) {
    if (payload[at] != std::byte{0}) {
      grid[out++] = payload[at++];
      continue;
    }
    if (payload.size() - at < 3) break;
    out += std::min<std::size_t>(u16_at(payload, at + 1), grid_bytes - out);
    at += 3;
  }

  lip.weights.resize(grid_bytes / sizeof(float));
  std::memcpy(lip.weights.data(), grid.data(), grid_bytes);
  for (auto& weight : lip.weights) {
    if (!std::isfinite(weight) || weight < -1.0f || weight > 1.0f) {
      ++lip.clamped_cells;
      lip.max_clamped_magnitude = std::isfinite(weight) ? std::max(lip.max_clamped_magnitude, std::fabs(weight))
                                                        : std::numeric_limits<float>::infinity();
      weight = std::isfinite(weight) ? std::clamp(weight, -1.0f, 1.0f) : 0.0f;
    }
  }
  return lip;
}

}  // namespace fo4::audio
