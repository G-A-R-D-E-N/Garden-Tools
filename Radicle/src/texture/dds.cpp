#include "fo4/texture/dds.hpp"

#include <algorithm>
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
      return true;
    default:
      return false;
  }
}

bool is_unsigned_bc5(std::uint8_t dxgi) {
  return dxgi == 82 || dxgi == 83;
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
            int pixels,
            int stride) {
  for (int i = 0; i < pixels; ++i) {
    const auto s = static_cast<std::size_t>(i) * stride;
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
    return dxgi == 82 || dxgi == 83 || dxgi == 84;
  } catch (...) {
    return false;
  }
}

std::vector<std::uint8_t> decode_dds_rgba(std::span<const std::uint8_t> bytes,
                                          std::uint32_t mip,
                                          bool reconstruct_z_value,
                                          std::uint32_t& width,
                                          std::uint32_t& height) {
  const auto info = read_dds(bytes);
  if (reconstruct_z_value && !is_unsigned_bc5(info.dxgi_format())) {
    throw std::invalid_argument("DDS: reconstruct_z needs an unsigned BC5 normal map, not " +
                                std::string(info.format.name) + ".");
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
  const auto surface_start = static_cast<std::int64_t>(info.data_offset) + info.mip_offset(mip);
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
    unpack(surface, rgba, info.dxgi_format(), static_cast<int>(pixels), info.format.pixel_bytes);
  } else {
    throw dds_error{"UnsupportedFormat",
                    "DDS: " + std::string(info.format.name) + " cannot be decoded in process."};
  }

  if (reconstruct_z_value) reconstruct_z(rgba);
  return rgba;
}

}
