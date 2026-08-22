#include "f4lod/dds.hpp"

#include <algorithm>
#include <limits>

namespace f4lod {
namespace {

constexpr std::uint32_t kDdsMagic = 0x20534444U;
constexpr std::uint32_t kFourCcDx10 = 0x30315844U;
constexpr std::uint32_t kFourCcDxt1 = 0x31545844U;
constexpr std::uint32_t kFourCcDxt3 = 0x33545844U;
constexpr std::uint32_t kFourCcDxt5 = 0x35545844U;
constexpr std::uint32_t kFourCcAti1 = 0x31495441U;
constexpr std::uint32_t kFourCcAti2 = 0x32495441U;
constexpr std::uint32_t kDdpfFourCc = 0x00000004U;
constexpr std::uint32_t kDdpfRgb = 0x00000040U;
constexpr std::uint32_t kCaps2CubeMap = 0x00000200U;
constexpr std::uint32_t kDx10Texture2D = 3U;
constexpr std::uint32_t kDx10MiscTextureCube = 0x00000004U;

std::uint32_t read_u32(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

DdsCompression compression_from_dxgi(std::uint32_t format) noexcept {
    if (format >= 70U && format <= 72U) return DdsCompression::bc1;
    if (format >= 73U && format <= 75U) return DdsCompression::bc2;
    if (format >= 76U && format <= 78U) return DdsCompression::bc3;
    if (format >= 79U && format <= 81U) return DdsCompression::bc4;
    if (format >= 82U && format <= 84U) return DdsCompression::bc5;
    if (format >= 94U && format <= 96U) return DdsCompression::bc6h;
    if (format >= 97U && format <= 99U) return DdsCompression::bc7;
    if ((format >= 27U && format <= 32U) || (format >= 87U && format <= 93U)) {
        return DdsCompression::rgba8;
    }
    return DdsCompression::unknown;
}

DdsCompression compression_from_legacy(
    std::uint32_t pixel_flags,
    std::uint32_t four_cc,
    std::uint32_t rgb_bits) noexcept {
    if ((pixel_flags & kDdpfFourCc) != 0U) {
        if (four_cc == kFourCcDxt1) return DdsCompression::bc1;
        if (four_cc == kFourCcDxt3) return DdsCompression::bc2;
        if (four_cc == kFourCcDxt5) return DdsCompression::bc3;
        if (four_cc == kFourCcAti1) return DdsCompression::bc4;
        if (four_cc == kFourCcAti2) return DdsCompression::bc5;
    }
    if ((pixel_flags & kDdpfRgb) != 0U && rgb_bits == 32U) return DdsCompression::rgba8;
    return DdsCompression::unknown;
}

std::uint32_t block_bytes(DdsCompression compression) noexcept {
    switch (compression) {
    case DdsCompression::bc1:
    case DdsCompression::bc4:
        return 8U;
    case DdsCompression::bc2:
    case DdsCompression::bc3:
    case DdsCompression::bc5:
    case DdsCompression::bc6h:
    case DdsCompression::bc7:
        return 16U;
    default:
        return 0U;
    }
}

bool checked_add(std::uint64_t& total, std::uint64_t value) noexcept {
    if (value > std::numeric_limits<std::uint64_t>::max() - total) return false;
    total += value;
    return true;
}

bool expected_payload_size(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t mip_count,
    std::uint32_t surfaces,
    DdsCompression compression,
    std::uint64_t& result) noexcept {
    result = 0U;
    for (std::uint32_t mip = 0; mip < mip_count; ++mip) {
        const auto w = std::max<std::uint32_t>(1U, width >> mip);
        const auto h = std::max<std::uint32_t>(1U, height >> mip);
        std::uint64_t bytes = 0U;
        const auto block = block_bytes(compression);
        if (block != 0U) {
            bytes = static_cast<std::uint64_t>(std::max<std::uint32_t>(1U, (w + 3U) / 4U)) *
                    static_cast<std::uint64_t>(std::max<std::uint32_t>(1U, (h + 3U) / 4U)) *
                    block;
        } else if (compression == DdsCompression::rgba8) {
            bytes = static_cast<std::uint64_t>(w) * h * 4ULL;
        } else {
            return false;
        }
        if (surfaces != 0U && bytes > std::numeric_limits<std::uint64_t>::max() / surfaces) return false;
        if (!checked_add(result, bytes * surfaces)) return false;
    }
    return true;
}

} // namespace

DdsInspection inspect_dds(std::span<const unsigned char> data) {
    DdsInspection result;
    if (data.size() < 128U) {
        result.error = "DDS is smaller than the legacy 128-byte header.";
        return result;
    }
    if (read_u32(data.data()) != kDdsMagic) {
        result.error = "DDS magic is missing.";
        return result;
    }
    if (read_u32(data.data() + 4U) != 124U) {
        result.error = "DDS_HEADER size is not 124 bytes.";
        return result;
    }
    if (read_u32(data.data() + 76U) != 32U) {
        result.error = "DDS_PIXELFORMAT size is not 32 bytes.";
        return result;
    }

    result.height = read_u32(data.data() + 12U);
    result.width = read_u32(data.data() + 16U);
    result.mip_count = read_u32(data.data() + 28U);
    if (result.mip_count == 0U) result.mip_count = 1U;
    if (result.width == 0U || result.height == 0U) {
        result.error = "DDS has zero width or height.";
        return result;
    }
    if (result.mip_count > 32U) {
        result.error = "DDS mip count is outside the supported range.";
        return result;
    }

    const auto pixel_flags = read_u32(data.data() + 80U);
    const auto four_cc = read_u32(data.data() + 84U);
    const auto rgb_bits = read_u32(data.data() + 88U);
    result.cube = (read_u32(data.data() + 112U) & kCaps2CubeMap) != 0U;
    result.header_size = 128U;

    if ((pixel_flags & kDdpfFourCc) != 0U && four_cc == kFourCcDx10) {
        if (data.size() < 148U) {
            result.error = "DX10 DDS is missing its 20-byte extended header.";
            return result;
        }
        result.dx10 = true;
        result.header_size = 148U;
        result.dxgi_format = read_u32(data.data() + 128U);
        const auto resource_dimension = read_u32(data.data() + 132U);
        const auto misc_flag = read_u32(data.data() + 136U);
        result.array_size = read_u32(data.data() + 140U);
        if (resource_dimension != kDx10Texture2D) {
            result.error = "Only 2D DDS/DX10 textures are supported by the terrain texture pipeline.";
            return result;
        }
        if (result.array_size == 0U) {
            result.error = "DDS/DX10 array size is zero.";
            return result;
        }
        result.cube = (misc_flag & kDx10MiscTextureCube) != 0U;
        result.compression = compression_from_dxgi(result.dxgi_format);
    } else {
        result.compression = compression_from_legacy(pixel_flags, four_cc, rgb_bits);
    }

    if (result.compression == DdsCompression::unknown) {
        result.error = result.dx10
            ? "DDS/DX10 uses an unsupported DXGI texture format."
            : "DDS uses an unsupported legacy pixel format.";
        return result;
    }

    const std::uint64_t surfaces = static_cast<std::uint64_t>(result.array_size) * (result.cube ? 6ULL : 1ULL);
    if (surfaces > std::numeric_limits<std::uint32_t>::max()) {
        result.error = "DDS surface count exceeds the supported range.";
        return result;
    }
    if (!expected_payload_size(
            result.width,
            result.height,
            result.mip_count,
            static_cast<std::uint32_t>(surfaces),
            result.compression,
            result.minimum_payload_size)) {
        result.error = "Could not calculate DDS payload size for its texture format.";
        return result;
    }

    result.available_payload_size = data.size() - result.header_size;
    if (result.available_payload_size < result.minimum_payload_size) {
        result.error = "DDS pixel payload is truncated for its dimensions, mips, and format.";
        return result;
    }

    result.valid = true;
    return result;
}

const char* dds_compression_name(DdsCompression compression) noexcept {
    switch (compression) {
    case DdsCompression::unknown: return "unknown";
    case DdsCompression::bc1: return "BC1";
    case DdsCompression::bc2: return "BC2";
    case DdsCompression::bc3: return "BC3";
    case DdsCompression::bc4: return "BC4";
    case DdsCompression::bc5: return "BC5";
    case DdsCompression::bc6h: return "BC6H";
    case DdsCompression::bc7: return "BC7";
    case DdsCompression::rgba8: return "RGBA8";
    }
    return "unknown";
}

} // namespace f4lod
