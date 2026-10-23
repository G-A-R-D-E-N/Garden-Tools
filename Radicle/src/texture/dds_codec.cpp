#include "fo4/texture/dds.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>

namespace fo4::texture {
namespace {

constexpr std::uint32_t k_dds_magic = 0x20534444;
constexpr std::uint32_t k_dx10_four_cc = 0x30315844;
constexpr std::uint32_t k_ddpf_four_cc = 0x4;
constexpr std::uint32_t k_ddpf_rgb = 0x40;
constexpr std::uint32_t k_ddpf_luminance = 0x20000;
constexpr std::uint32_t k_ddpf_alpha_only = 0x2;
constexpr std::uint32_t k_caps2_cube_map = 0x200;
constexpr std::uint32_t k_caps2_volume = 0x200000;
constexpr std::uint32_t k_max_slices = 0xFFFFU;

constexpr int k_legacy_header_size = 128;
constexpr int k_dx10_header_size = 148;

constexpr std::array<DdsFormatInfo, 40> k_formats{{
    {70, "BC1_TYPELESS", 8, 0},
    {71, "BC1_UNORM", 8, 0},
    {72, "BC1_UNORM_SRGB", 8, 0},
    {73, "BC2_TYPELESS", 16, 0},
    {74, "BC2_UNORM", 16, 0},
    {75, "BC2_UNORM_SRGB", 16, 0},
    {76, "BC3_TYPELESS", 16, 0},
    {77, "BC3_UNORM", 16, 0},
    {78, "BC3_UNORM_SRGB", 16, 0},
    {79, "BC4_TYPELESS", 8, 0},
    {80, "BC4_UNORM", 8, 0},
    {81, "BC4_SNORM", 8, 0},
    {82, "BC5_TYPELESS", 16, 0},
    {83, "BC5_UNORM", 16, 0},
    {84, "BC5_SNORM", 16, 0},
    {94, "BC6H_TYPELESS", 16, 0},
    {95, "BC6H_UF16", 16, 0},
    {96, "BC6H_SF16", 16, 0},
    {97, "BC7_TYPELESS", 16, 0},
    {98, "BC7_UNORM", 16, 0},
    {99, "BC7_UNORM_SRGB", 16, 0},
    {2, "R32G32B32A32_FLOAT", 0, 16},
    {10, "R16G16B16A16_FLOAT", 0, 8},
    {24, "R10G10B10A2_UNORM", 0, 4},
    {28, "R8G8B8A8_UNORM", 0, 4},
    {29, "R8G8B8A8_UNORM_SRGB", 0, 4},
    {34, "R16G16_UNORM", 0, 4},
    {41, "R32_FLOAT", 0, 4},
    {49, "R8G8_UNORM", 0, 2},
    {54, "R16_FLOAT", 0, 2},
    {56, "R16_UNORM", 0, 2},
    {61, "R8_UNORM", 0, 1},
    {65, "A8_UNORM", 0, 1},
    {85, "B5G6R5_UNORM", 0, 2},
    {86, "B5G5R5A1_UNORM", 0, 2},
    {87, "B8G8R8A8_UNORM", 0, 4},
    {88, "B8G8R8X8_UNORM", 0, 4},
    {91, "B8G8R8A8_UNORM_SRGB", 0, 4},
    {93, "B8G8R8X8_UNORM_SRGB", 0, 4},
    {115, "B4G4R4A4_UNORM", 0, 2},
}};

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::string hex32(std::uint32_t value) {
  char buffer[9]{};
  std::snprintf(buffer, sizeof buffer, "%08X", value);
  return std::string(buffer);
}

std::int64_t checked_payload_bytes(std::uint64_t total, std::uint64_t slices) {
  constexpr auto k_max_int64 = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (total != 0 && slices > std::numeric_limits<std::uint64_t>::max() / total) {
    throw dds_error{"InvalidDds",
                    "DDS: the mip total times the array and depth slices overflows a 64-bit byte count."};
  }
  const auto bytes = total * slices;
  if (bytes > k_max_int64) {
    throw dds_error{"InvalidDds", "DDS: the payload exceeds 2^63 bytes."};
  }
  return static_cast<std::int64_t>(bytes);
}

std::uint8_t legacy_to_dxgi(std::uint32_t pf_flags,
                            std::uint32_t four_cc,
                            std::uint32_t rgb_bits,
                            std::uint32_t r_mask,
                            std::uint32_t g_mask,
                            std::uint32_t b_mask,
                            std::uint32_t a_mask) {
  if ((pf_flags & k_ddpf_four_cc) != 0) {
    switch (four_cc) {
      case 0x31545844: return 71;
      case 0x32545844: return 74;
      case 0x33545844: return 74;
      case 0x34545844: return 77;
      case 0x35545844: return 77;
      case 0x31495441: return 80;
      case 0x55344342: return 80;
      case 0x53344342: return 81;
      case 0x32495441: return 83;
      case 0x55354342: return 83;
      case 0x53354342: return 84;
      case 0x00000024: return 10;
      case 0x00000074: return 2;
      default:
        throw dds_error{"UnsupportedFormat", "DDS: unsupported DDS FourCC 0x" + hex32(four_cc) + "."};
    }
  }

  if ((pf_flags & k_ddpf_rgb) != 0) {
    switch (rgb_bits) {
      case 32:
        if (r_mask == 0x00FF0000 && g_mask == 0x0000FF00 && b_mask == 0x000000FF) {
          return a_mask == 0xFF000000 ? 87 : 88;
        }
        if (r_mask == 0x000000FF && g_mask == 0x0000FF00 && b_mask == 0x00FF0000) return 28;
        if (r_mask == 0x3FF00000 && g_mask == 0x000FFC00 && b_mask == 0x000003FF) return 24;
        break;
      case 16:
        if (r_mask == 0xF800 && g_mask == 0x07E0 && b_mask == 0x001F) return 85;
        if (r_mask == 0x7C00 && g_mask == 0x03E0 && b_mask == 0x001F) return 86;
        if (r_mask == 0x0F00 && g_mask == 0x00F0 && b_mask == 0x000F) return 115;
        break;
      default:
        break;
    }
    throw dds_error{"UnsupportedFormat",
                    "DDS: unsupported uncompressed DDS layout (" + std::to_string(rgb_bits) + "bpp, masks " +
                        hex32(r_mask) + "/" + hex32(g_mask) + "/" + hex32(b_mask) + "/" + hex32(a_mask) + ")."};
  }

  if ((pf_flags & k_ddpf_luminance) != 0 && rgb_bits == 8) return 61;
  if ((pf_flags & k_ddpf_alpha_only) != 0 && rgb_bits == 8) return 65;

  throw dds_error{"UnsupportedFormat",
                  "DDS: pixel format flags 0x" + hex32(pf_flags) +
                      " carry no format this decoder recognises."};
}

}

std::optional<DdsFormatInfo> lookup_format(std::uint8_t dxgi) noexcept {
  for (const auto& format : k_formats) {
    if (format.dxgi == dxgi) return format;
  }
  return std::nullopt;
}

std::int64_t mip_size(const DdsFormatInfo& format, int width, int height) noexcept {
  const auto w = std::max(1, width);
  const auto h = std::max(1, height);
  if (!format.is_block_compressed()) {
    return static_cast<std::int64_t>(w) * h * format.pixel_bytes;
  }
  return static_cast<std::int64_t>((w + 3) / 4) * ((h + 3) / 4) * format.block_bytes;
}

std::int64_t DdsInfo::payload_size() const {
  std::uint64_t total = 0;
  for (int mip = 0; mip < mip_count; ++mip) {
    const auto shift = std::min(mip, 31);
    const auto level = fo4::texture::mip_size(format, width >> shift, height >> shift);
    if (level < 0) {
      throw dds_error{"InvalidDds", "DDS: a mip level has a negative size."};
    }
    total += static_cast<std::uint64_t>(level);
  }
  const auto arrays = static_cast<std::uint64_t>(std::max(1, array_size));
  const auto slices = static_cast<std::uint64_t>(std::max(1, depth));
  return checked_payload_bytes(total, arrays * slices);
}

std::int64_t DdsInfo::mip_size(std::uint32_t mip) const noexcept {
  const auto shift = std::min<std::uint32_t>(mip, 31U);
  return fo4::texture::mip_size(format, width >> shift, height >> shift);
}

std::int64_t DdsInfo::mip_offset(std::uint32_t mip) const noexcept {
  std::int64_t offset = 0;
  for (std::uint32_t level = 0; level < mip; ++level) offset += this->mip_size(level);
  return offset;
}

DdsInfo read_dds(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < static_cast<std::size_t>(k_legacy_header_size)) {
    throw dds_error{"InvalidDds",
                    "DDS: " + std::to_string(bytes.size()) + " bytes is too short to be a DDS."};
  }
  if (read_u32(bytes, 0) != k_dds_magic) {
    throw dds_error{"InvalidDds", "DDS: not a DDS (no 'DDS ' magic)."};
  }
  if (read_u32(bytes, 4) != 124) {
    throw dds_error{"InvalidDds",
                    "DDS: DDS_HEADER size is " + std::to_string(read_u32(bytes, 4)) + ", expected 124."};
  }

  const auto height = static_cast<int>(read_u32(bytes, 12));
  const auto width = static_cast<int>(read_u32(bytes, 16));
  const auto raw_depth = read_u32(bytes, 24);
  const auto depth = static_cast<int>(raw_depth);
  auto mip_count = static_cast<int>(read_u32(bytes, 28));
  const auto caps2 = read_u32(bytes, 112);

  const auto pf_flags = read_u32(bytes, 80);
  const auto four_cc = read_u32(bytes, 84);
  const auto rgb_bits = read_u32(bytes, 88);
  const auto r_mask = read_u32(bytes, 92);
  const auto g_mask = read_u32(bytes, 96);
  const auto b_mask = read_u32(bytes, 100);
  const auto a_mask = read_u32(bytes, 104);

  auto is_cube = (caps2 & k_caps2_cube_map) != 0;
  const auto is_volume = (caps2 & k_caps2_volume) != 0;

  int data_offset = 0;
  std::uint8_t dxgi = 0;
  int array_size = 1;

  if ((pf_flags & k_ddpf_four_cc) != 0 && four_cc == k_dx10_four_cc) {
    if (bytes.size() < static_cast<std::size_t>(k_dx10_header_size)) {
      throw dds_error{"InvalidDds", "DDS: header claims a DX10 extension but the file ends before it."};
    }
    const auto raw = read_u32(bytes, 128);
    if (raw > 0xFFU) {
      throw dds_error{"InvalidDds",
                      "DDS: DXGI format " + std::to_string(raw) +
                          " does not fit the one byte a BA2 entry stores."};
    }
    dxgi = static_cast<std::uint8_t>(raw);
    const auto misc_flag = read_u32(bytes, 136);
    const auto raw_array_size = read_u32(bytes, 140);
    if (raw_array_size > k_max_slices) {
      throw dds_error{"InvalidDds",
                      "DDS: array size " + std::to_string(raw_array_size) +
                          " exceeds the 16-bit count a BA2 entry stores."};
    }
    array_size = static_cast<int>(std::max<std::uint32_t>(1, raw_array_size));
    if ((misc_flag & 0x4U) != 0) is_cube = true;
    data_offset = k_dx10_header_size;
  } else {
    dxgi = legacy_to_dxgi(pf_flags, four_cc, rgb_bits, r_mask, g_mask, b_mask, a_mask);
    data_offset = k_legacy_header_size;
  }

  if (is_cube) array_size *= 6;

  const auto format = lookup_format(dxgi);
  if (!format) {
    throw dds_error{"UnsupportedFormat",
                    "DDS: DXGI format " + std::to_string(dxgi) +
                        " is not one this decoder can size, so it is not readable."};
  }

  if (width <= 0 || height <= 0) {
    throw dds_error{"InvalidDds",
                    "DDS: header says " + std::to_string(width) + "x" + std::to_string(height) + "."};
  }
  if (width > 0xFFFF || height > 0xFFFF) {
    throw dds_error{"InvalidDds",
                    "DDS: " + std::to_string(width) + "x" + std::to_string(height) +
                        " exceeds the 16-bit width and height a BA2 entry stores."};
  }
  if (is_volume && raw_depth > k_max_slices) {
    throw dds_error{"InvalidDds",
                    "DDS: depth " + std::to_string(raw_depth) +
                        " exceeds the 16-bit count a BA2 entry stores."};
  }
  if (mip_count <= 0) mip_count = 1;
  if (mip_count > 0xFF) {
    throw dds_error{"InvalidDds",
                    "DDS: " + std::to_string(mip_count) + " mips exceeds the one byte a BA2 entry stores."};
  }

  return DdsInfo{width,      height,   mip_count,   is_cube,
                 array_size, is_volume ? std::max(1, depth) : 1, data_offset, *format};
}

std::vector<std::uint8_t> build_dds_header(const DdsInfo& info) {
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(k_dx10_header_size), 0);
  const auto linear_size = static_cast<std::uint32_t>(mip_size(info.format, info.width, info.height));

  write_u32(bytes, 0, k_dds_magic);
  write_u32(bytes, 4, 124);
  write_u32(bytes, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
  write_u32(bytes, 12, static_cast<std::uint32_t>(info.height));
  write_u32(bytes, 16, static_cast<std::uint32_t>(info.width));
  write_u32(bytes, 20, linear_size);
  write_u32(bytes, 24, static_cast<std::uint32_t>(std::max(1, info.depth)));
  write_u32(bytes, 28, static_cast<std::uint32_t>(info.mip_count));
  write_u32(bytes, 76, 32);
  write_u32(bytes, 80, k_ddpf_four_cc);
  write_u32(bytes, 84, k_dx10_four_cc);
  write_u32(bytes, 108, 0x1000 | (info.mip_count > 1 ? 0x400008U : 0U));
  if (info.is_cube_map) write_u32(bytes, 112, k_caps2_cube_map | 0xFC00U);
  write_u32(bytes, 128, info.dxgi_format());
  write_u32(bytes, 132, 3);
  if (info.is_cube_map) write_u32(bytes, 136, 0x4U);
  write_u32(bytes, 140,
            static_cast<std::uint32_t>(std::max(1, info.is_cube_map ? info.array_size / 6 : info.array_size)));
  return bytes;
}

}
