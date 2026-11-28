#include "fo4/texture/dds.hpp"
#include "dds_internal.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace fo4::texture {
namespace {

bool is_uncompressed(std::uint8_t dxgi) {
  switch (dxgi) {
    case 28:
    case 29:
    case 87:
    case 88:
    case 91:
    case 93:
    case 49:
    case 61:
    case 65:
    case 85:
    case 86:
    case 115:
    case k_dxgi_bgr8:
    case k_dxgi_rgb8:
      return true;
    default:
      return false;
  }
}

bool is_unsigned_bc5(std::uint8_t dxgi) {
  return dxgi == 82 || dxgi == 83;
}

// Invariant: only signed BC4 and BC5 (dxgi 81 and 84) change; every other format stays as stored.
void unsign_rgtc(std::vector<std::uint8_t>& blocks, const DdsInfo& info) {
  const auto dxgi = info.dxgi_format();
  if (dxgi != 81 && dxgi != 84) return;
  const auto stride = static_cast<std::size_t>(info.format.block_bytes);
  const auto flip = [](std::uint8_t& value) {
    int signed_value = static_cast<std::int8_t>(value);
    if (signed_value == -128) signed_value = -127;
    value = static_cast<std::uint8_t>(signed_value + 128);
  };
  for (std::size_t at = 0; at + stride <= blocks.size(); at += stride) {
    flip(blocks[at]);
    flip(blocks[at + 1]);
    if (stride == 16) {
      flip(blocks[at + 8]);
      flip(blocks[at + 9]);
    }
  }
}

void reconstruct_z(std::span<std::uint8_t> rgba) {
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    const auto x = rgba[i] / 255.0 * 2.0 - 1.0;
    const auto y = rgba[i + 1] / 255.0 * 2.0 - 1.0;
    const auto z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
    rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(std::lround((z + 1.0) * 0.5 * 255.0), 0L, 255L));
  }
}

void unpack(std::span<const std::uint8_t> surface,
            std::span<std::uint8_t> rgba,
            std::uint8_t dxgi,
            int width,
            int height,
            int stride,
            int row_pitch) {
  const auto pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
  for (std::int64_t i = 0; i < pixels; ++i) {
    const auto row = static_cast<std::size_t>(i / width);
    const auto s = row * row_pitch + static_cast<std::size_t>(i % width) * stride;
    const auto d = static_cast<std::size_t>(i) * 4;
    switch (dxgi) {
      case 28:
      case 29:
        rgba[d] = surface[s];
        rgba[d + 1] = surface[s + 1];
        rgba[d + 2] = surface[s + 2];
        rgba[d + 3] = surface[s + 3];
        break;
      case 87:
      case 91:
        rgba[d] = surface[s + 2];
        rgba[d + 1] = surface[s + 1];
        rgba[d + 2] = surface[s];
        rgba[d + 3] = surface[s + 3];
        break;
      case 88:
      case 93:
        rgba[d] = surface[s + 2];
        rgba[d + 1] = surface[s + 1];
        rgba[d + 2] = surface[s];
        rgba[d + 3] = 255;
        break;
      case 49:
        rgba[d] = surface[s];
        rgba[d + 1] = surface[s + 1];
        rgba[d + 2] = 0;
        rgba[d + 3] = 255;
        break;
      case k_dxgi_bgr8:
        rgba[d] = surface[s + 2];
        rgba[d + 1] = surface[s + 1];
        rgba[d + 2] = surface[s];
        rgba[d + 3] = 255;
        break;
      case k_dxgi_rgb8:
        rgba[d] = surface[s];
        rgba[d + 1] = surface[s + 1];
        rgba[d + 2] = surface[s + 2];
        rgba[d + 3] = 255;
        break;
      case 61:
        rgba[d] = rgba[d + 1] = rgba[d + 2] = surface[s];
        rgba[d + 3] = 255;
        break;
      case 65:
        rgba[d] = rgba[d + 1] = rgba[d + 2] = 0;
        rgba[d + 3] = surface[s];
        break;
      case 85: {
        const auto value = static_cast<int>(surface[s]) | (static_cast<int>(surface[s + 1]) << 8);
        const auto r5 = (value >> 11) & 0x1F;
        const auto g6 = (value >> 5) & 0x3F;
        const auto b5 = value & 0x1F;
        rgba[d] = static_cast<std::uint8_t>((r5 * 527 + 23) >> 6);
        rgba[d + 1] = static_cast<std::uint8_t>((g6 * 259 + 33) >> 6);
        rgba[d + 2] = static_cast<std::uint8_t>((b5 * 527 + 23) >> 6);
        rgba[d + 3] = 255;
        break;
      }
      case 86: {
        const auto value = static_cast<int>(surface[s]) | (static_cast<int>(surface[s + 1]) << 8);
        const auto r5 = (value >> 10) & 0x1F;
        const auto g5 = (value >> 5) & 0x1F;
        const auto b5 = value & 0x1F;
        rgba[d] = static_cast<std::uint8_t>((r5 * 527 + 23) >> 6);
        rgba[d + 1] = static_cast<std::uint8_t>((g5 * 527 + 23) >> 6);
        rgba[d + 2] = static_cast<std::uint8_t>((b5 * 527 + 23) >> 6);
        rgba[d + 3] = static_cast<std::uint8_t>((value & 0x8000) != 0 ? 255 : 0);
        break;
      }
      case 115: {
        const auto value = static_cast<int>(surface[s]) | (static_cast<int>(surface[s + 1]) << 8);
        rgba[d] = static_cast<std::uint8_t>(((value >> 8) & 0xF) * 17);
        rgba[d + 1] = static_cast<std::uint8_t>(((value >> 4) & 0xF) * 17);
        rgba[d + 2] = static_cast<std::uint8_t>((value & 0xF) * 17);
        rgba[d + 3] = static_cast<std::uint8_t>(((value >> 12) & 0xF) * 17);
        break;
      }
      default:
        throw dds_error{"UnsupportedFormat",
                        "DXGI format " + std::to_string(dxgi) +
                            " is not an uncompressed layout this decoder unpacks."};
    }
  }
}

}

bool is_bc5(std::span<const std::uint8_t> bytes) noexcept {
  try {
    const auto dxgi = read_dds(bytes).dxgi_format();
    return is_unsigned_bc5(dxgi);
  } catch (...) {
    return false;
  }
}

std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t mip,
                                          bool reconstruct_z_value,
                                          std::uint32_t& width,
                                          std::uint32_t& height) {
  return decode_dds_rgba(bytes, 0, mip, reconstruct_z_value, width, height);
}

std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t face,
                                          std::uint32_t mip,
                                          bool reconstruct_z_value,
                                          std::uint32_t& width,
                                          std::uint32_t& height) {
  const auto info = read_dds(bytes);
  if (reconstruct_z_value && !is_unsigned_bc5(info.dxgi_format())) {
    throw std::invalid_argument("DDS: reconstruct_z needs an unsigned BC5 normal map, not " +
                                std::string(info.format.name) + ".");
  }
  if (face >= static_cast<std::uint32_t>(info.face_count())) {
    throw dds_error{"InvalidDds",
                    "DDS: face " + std::to_string(face) + " was requested but the file holds " +
                        std::to_string(info.face_count()) + "."};
  }
  if (mip >= static_cast<std::uint32_t>(info.mip_count)) {
    throw dds_error{"InvalidDds",
                    "DDS: mip " + std::to_string(mip) + " was requested but the file holds " +
                        std::to_string(info.mip_count) + "."};
  }
  const auto shift = std::min<std::uint32_t>(mip, 31U);
  width = static_cast<std::uint32_t>(std::max(1, info.width >> shift));
  height = static_cast<std::uint32_t>(std::max(1, info.height >> shift));

  const auto surface_length = info.mip_size(mip);
  const auto face_start = info.face_offset(face);
  const auto mip_start = info.mip_offset(mip);
  constexpr auto k_int64_max = std::numeric_limits<std::int64_t>::max();
  // Invariant: data_offset + face_start + mip_start + surface_length must stay below 2^63.
  if (face_start > k_int64_max - info.data_offset || mip_start > k_int64_max - info.data_offset - face_start ||
      surface_length > k_int64_max - info.data_offset - face_start - mip_start) {
    throw dds_error{"InvalidDds", "DDS: face " + std::to_string(face) + " mip " + std::to_string(mip) +
                                      " starts past 2^63 bytes."};
  }
  const auto surface_start = static_cast<std::int64_t>(info.data_offset) + face_start + mip_start;
  if (surface_start < 0 ||
      bytes.size() < static_cast<std::size_t>(surface_start) + static_cast<std::size_t>(surface_length)) {
    throw dds_error{"InvalidDds",
                    "DDS: file is " + std::to_string(bytes.size()) + " bytes; mip " + std::to_string(mip) +
                        " alone ends at " + std::to_string(surface_start + surface_length) + "."};
  }
  const auto surface = bytes.subspan(static_cast<std::size_t>(surface_start),
                                     static_cast<std::size_t>(surface_length));

  const auto pixels = static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height);
  if (pixels * 4 > std::numeric_limits<int>::max()) {
    throw dds_error{"TooLarge",
                    "DDS: " + std::to_string(width) + "x" + std::to_string(height) +
                        " is too large to decode into one buffer."};
  }
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(pixels) * 4);

  if (can_decode_block_format(info.dxgi_format())) {
    rgba = decode_block_format(surface, info.dxgi_format(), static_cast<int>(width), static_cast<int>(height));
  } else if (is_uncompressed(info.dxgi_format())) {
    unpack(surface, rgba, info.dxgi_format(), static_cast<int>(width), static_cast<int>(height),
           info.format.pixel_bytes, info.mip_row_pitch(mip));
  } else {
    throw dds_error{"UnsupportedFormat",
                    "DDS: " + std::string(info.format.name) + " cannot be decoded in process."};
  }

  if (reconstruct_z_value) reconstruct_z(rgba);
  return rgba;
}

std::vector<std::uint8_t> read_dds_block_chain(std::span<const std::uint8_t> bytes,
                                               std::uint32_t face,
                                               std::uint32_t first_mip,
                                               std::uint32_t mip_count,
                                               bool unsigned_rgtc) {
  const auto info = read_dds(bytes);
  if (!info.format.is_block_compressed()) {
    throw dds_error{"UnsupportedFormat", "DDS: " + std::string(info.format.name) + " is not block compressed."};
  }
  if (face >= static_cast<std::uint32_t>(info.face_count())) {
    throw dds_error{"InvalidDds", "DDS: face " + std::to_string(face) + " was requested but the file holds " +
                                      std::to_string(info.face_count()) + "."};
  }
  const auto stored = static_cast<std::uint32_t>(std::max(0, info.mip_count));
  if (mip_count == 0 || first_mip >= stored || mip_count > stored - first_mip) {
    throw dds_error{"InvalidDds", "DDS: mips " + std::to_string(first_mip) + " for " + std::to_string(mip_count) +
                                      " were requested but the file holds " + std::to_string(stored) + "."};
  }
  const auto chain_start = info.mip_offset(first_mip);
  const auto chain_length = info.mip_offset(first_mip + mip_count) - chain_start;
  const auto face_start = info.face_offset(face);
  constexpr auto k_int64_max = std::numeric_limits<std::int64_t>::max();
  // Invariant: data_offset + face_start + chain_start + chain_length must stay below 2^63.
  if (face_start > k_int64_max - info.data_offset || chain_start > k_int64_max - info.data_offset - face_start ||
      chain_length > k_int64_max - info.data_offset - face_start - chain_start) {
    throw dds_error{"InvalidDds", "DDS: face " + std::to_string(face) + " starts past 2^63 bytes."};
  }
  const auto start = static_cast<std::int64_t>(info.data_offset) + face_start + chain_start;
  if (start < 0 || chain_length < 0 ||
      bytes.size() < static_cast<std::size_t>(start) + static_cast<std::size_t>(chain_length)) {
    throw dds_error{"InvalidDds", "DDS: file is " + std::to_string(bytes.size()) + " bytes; mips " +
                                      std::to_string(first_mip) + " for " + std::to_string(mip_count) +
                                      " end at " + std::to_string(start + chain_length) + "."};
  }
  const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(start);
  std::vector<std::uint8_t> blocks(begin, begin + static_cast<std::ptrdiff_t>(chain_length));

  if (unsigned_rgtc) unsign_rgtc(blocks, info);
  return blocks;
}

std::vector<std::uint8_t> read_dds_face_blocks(std::span<const std::uint8_t> bytes,
                                               std::uint32_t face,
                                               std::uint32_t mips_per_face,
                                               std::uint32_t mip_count,
                                               bool unsigned_rgtc) {
  const auto info = read_dds(bytes);
  if (!info.format.is_block_compressed()) {
    throw dds_error{"UnsupportedFormat", "DDS: " + std::string(info.format.name) + " is not block compressed."};
  }
  const auto full_chain =
      static_cast<std::uint32_t>(std::bit_width(static_cast<unsigned>(std::max(info.width, info.height))));
  if (mips_per_face == 0 || mips_per_face > full_chain || mip_count == 0 || mip_count > mips_per_face) {
    throw dds_error{"InvalidDds", "DDS: " + std::to_string(mip_count) + " mips of " + std::to_string(mips_per_face) +
                                      " per face were requested but a " + std::to_string(info.width) + "x" +
                                      std::to_string(info.height) + " chain has " + std::to_string(full_chain) + "."};
  }
  const auto stride = info.mip_offset(mips_per_face);
  const auto length = info.mip_offset(mip_count);
  if (stride > 0 && face > std::numeric_limits<std::int64_t>::max() / stride) {
    throw dds_error{"InvalidDds", "DDS: face " + std::to_string(face) + " starts past 2^63 bytes."};
  }
  const auto face_start = static_cast<std::int64_t>(face) * stride;
  const auto available = static_cast<std::int64_t>(bytes.size()) - info.data_offset;
  if (available < 0 || face_start > available || length > available - face_start) {
    throw dds_error{"InvalidDds", "DDS: file is " + std::to_string(bytes.size()) + " bytes; " +
                                      std::to_string(mip_count) + " mips of face " + std::to_string(face) +
                                      " end at " + std::to_string(info.data_offset + face_start + length) + "."};
  }
  const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(info.data_offset + face_start);
  std::vector<std::uint8_t> blocks(begin, begin + static_cast<std::ptrdiff_t>(length));
  if (unsigned_rgtc) unsign_rgtc(blocks, info);
  return blocks;
}

}
