#include <fo4ide/textures/DdsCodec.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace fo4ide::textures {
namespace {

constexpr std::uint32_t kDdsMagic = 0x20534444;      // "DDS "
constexpr std::uint32_t kDx10FourCc = 0x30315844;    // "DX10"
constexpr std::uint32_t kDdpfFourCc = 0x4;
constexpr std::uint32_t kDdpfRgb = 0x40;
constexpr std::uint32_t kDdpfLuminance = 0x20000;
constexpr std::uint32_t kDdpfAlphaOnly = 0x2;
constexpr std::uint32_t kCaps2CubeMap = 0x200;
constexpr std::uint32_t kCaps2Volume = 0x200000;

constexpr int kLegacyHeaderSize = 128;
constexpr int kDx10HeaderSize = 148;

constexpr std::array<DdsFormatInfo, 40> kFormats{{
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

std::uint32_t readU32(std::span<const std::uint8_t> bytes, size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

void writeU32(std::vector<std::uint8_t>& bytes, size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

Result<std::uint8_t> legacyToDxgi(std::uint32_t pfFlags,
                                  std::uint32_t fourCc,
                                  std::uint32_t rgbBits,
                                  std::uint32_t rMask,
                                  std::uint32_t gMask,
                                  std::uint32_t bMask,
                                  std::uint32_t aMask,
                                  std::string_view what) {
    if ((pfFlags & kDdpfFourCc) != 0) {
        switch (fourCc) {
            case 0x31545844: return std::uint8_t{71};  // DXT1
            case 0x32545844: return std::uint8_t{74};  // DXT2
            case 0x33545844: return std::uint8_t{74};  // DXT3
            case 0x34545844: return std::uint8_t{77};  // DXT4
            case 0x35545844: return std::uint8_t{77};  // DXT5
            case 0x31495441: return std::uint8_t{80};  // ATI1
            case 0x55344342: return std::uint8_t{80};  // BC4U
            case 0x53344342: return std::uint8_t{81};  // BC4S
            case 0x32495441: return std::uint8_t{83};  // ATI2
            case 0x55354342: return std::uint8_t{83};  // BC5U
            case 0x53354342: return std::uint8_t{84};  // BC5S
            case 0x00000024: return std::uint8_t{10};  // A16B16G16R16F
            case 0x00000074: return std::uint8_t{2};   // A32B32G32R32F
            default:
                return std::unexpected(Error{
                    "UnsupportedFormat",
                    std::string(what) + ": unsupported DDS FourCC 0x" +
                        [fourCc] {
                            char buf[9]{};
                            snprintf(buf, sizeof buf, "%08X", fourCc);
                            return std::string(buf);
                        }()});
        }
    }

    if ((pfFlags & kDdpfRgb) != 0) {
        switch (rgbBits) {
            case 32:
                if (rMask == 0x00FF0000 && gMask == 0x0000FF00 && bMask == 0x000000FF) {
                    return aMask == 0xFF000000 ? std::uint8_t{87} : std::uint8_t{88};
                }
                if (rMask == 0x000000FF && gMask == 0x0000FF00 && bMask == 0x00FF0000) {
                    return std::uint8_t{28};
                }
                if (rMask == 0x3FF00000 && gMask == 0x000FFC00 && bMask == 0x000003FF) {
                    return std::uint8_t{24};
                }
                break;
            case 16:
                if (rMask == 0xF800 && gMask == 0x07E0 && bMask == 0x001F) return std::uint8_t{85};
                if (rMask == 0x7C00 && gMask == 0x03E0 && bMask == 0x001F) return std::uint8_t{86};
                if (rMask == 0x0F00 && gMask == 0x00F0 && bMask == 0x000F) return std::uint8_t{115};
                break;
            default:
                break;
        }
        char buf[9]{};
        snprintf(buf, sizeof buf, "%08X", rMask);
        std::string masks(buf);
        snprintf(buf, sizeof buf, "%08X", gMask);
        masks += "/";
        masks += buf;
        snprintf(buf, sizeof buf, "%08X", bMask);
        masks += "/";
        masks += buf;
        snprintf(buf, sizeof buf, "%08X", aMask);
        masks += "/";
        masks += buf;
        return std::unexpected(Error{
            "UnsupportedFormat",
            std::string(what) + ": unsupported uncompressed DDS layout (" +
                std::to_string(rgbBits) + "bpp, masks " + masks + ")."});
    }

    if ((pfFlags & kDdpfLuminance) != 0 && rgbBits == 8) return std::uint8_t{61};
    if ((pfFlags & kDdpfAlphaOnly) != 0 && rgbBits == 8) return std::uint8_t{65};

    char buf[9]{};
    snprintf(buf, sizeof buf, "%08X", pfFlags);
    return std::unexpected(Error{
        "UnsupportedFormat",
        std::string(what) + ": DDS pixel format flags 0x" + buf +
            " carry no format this tool recognises."});
}

}  // namespace

std::optional<DdsFormatInfo> lookupFormat(std::uint8_t dxgi) noexcept {
    for (const auto& format : kFormats) {
        if (format.dxgi == dxgi) return format;
    }
    return std::nullopt;
}

std::int64_t mipSize(const DdsFormatInfo& format, int width, int height) noexcept {
    const auto w = std::max(1, width);
    const auto h = std::max(1, height);
    if (!format.isBlockCompressed()) {
        return static_cast<std::int64_t>(w) * h * format.pixelBytes;
    }
    return static_cast<std::int64_t>((w + 3) / 4) * ((h + 3) / 4) * format.blockBytes;
}

std::int64_t DdsInfo::payloadSize() const {
    std::int64_t one = 0;
    for (int m = 0; m < mipCount; ++m) {
        one += mipSize(format, width >> m, height >> m);
    }
    return one * arraySize * std::max(1, depth);
}

Result<DdsInfo> parseDds(std::span<const std::uint8_t> bytes, std::string_view what) {
    if (bytes.size() < static_cast<size_t>(kLegacyHeaderSize)) {
        return std::unexpected(Error{
            "InvalidDds",
            std::string(what) + ": " + std::to_string(bytes.size()) +
                " bytes is too short to be a DDS."});
    }
    if (readU32(bytes, 0) != kDdsMagic) {
        return std::unexpected(Error{
            "InvalidDds",
            std::string(what) + ": not a DDS (no 'DDS ' magic)."});
    }
    if (readU32(bytes, 4) != 124) {
        return std::unexpected(Error{
            "InvalidDds",
            std::string(what) + ": DDS_HEADER size is " + std::to_string(readU32(bytes, 4)) +
                ", expected 124."});
    }

    const auto height = static_cast<int>(readU32(bytes, 12));
    const auto width = static_cast<int>(readU32(bytes, 16));
    const auto depth = static_cast<int>(readU32(bytes, 24));
    auto mipCount = static_cast<int>(readU32(bytes, 28));
    const auto caps2 = readU32(bytes, 112);

    const auto pfFlags = readU32(bytes, 80);
    const auto fourCc = readU32(bytes, 84);
    const auto rgbBits = readU32(bytes, 88);
    const auto rMask = readU32(bytes, 92);
    const auto gMask = readU32(bytes, 96);
    const auto bMask = readU32(bytes, 100);
    const auto aMask = readU32(bytes, 104);

    auto isCube = (caps2 & kCaps2CubeMap) != 0;
    const auto isVolume = (caps2 & kCaps2Volume) != 0;

    int dataOffset{};
    std::uint8_t dxgi{};
    int arraySize = 1;

    if ((pfFlags & kDdpfFourCc) != 0 && fourCc == kDx10FourCc) {
        if (bytes.size() < static_cast<size_t>(kDx10HeaderSize)) {
            return std::unexpected(Error{
                "InvalidDds",
                std::string(what) + ": header claims a DX10 extension but the file ends before it."});
        }
        const auto raw = readU32(bytes, 128);
        if (raw > 0xFFU) {
            return std::unexpected(Error{
                "InvalidDds",
                std::string(what) + ": DXGI format " + std::to_string(raw) +
                    " does not fit the one byte a BA2 entry stores."});
        }
        dxgi = static_cast<std::uint8_t>(raw);
        const auto miscFlag = readU32(bytes, 136);
        arraySize = static_cast<int>(std::max<std::uint32_t>(1, readU32(bytes, 140)));
        if ((miscFlag & 0x4U) != 0) isCube = true;
        dataOffset = kDx10HeaderSize;
    } else {
        auto legacy = legacyToDxgi(pfFlags, fourCc, rgbBits, rMask, gMask, bMask, aMask, what);
        if (!legacy) return std::unexpected(legacy.error());
        dxgi = *legacy;
        dataOffset = kLegacyHeaderSize;
    }

    if (isCube) arraySize *= 6;

    auto format = lookupFormat(dxgi);
    if (!format) {
        return std::unexpected(Error{
            "UnsupportedFormat",
            std::string(what) + ": DXGI format " + std::to_string(dxgi) +
                " is not one this tool can size, so it cannot be packed into a texture archive."});
    }

    if (width <= 0 || height <= 0) {
        return std::unexpected(Error{
            "InvalidDds",
            std::string(what) + ": header says " + std::to_string(width) + "x" +
                std::to_string(height) + "."});
    }
    if (width > 0xFFFF || height > 0xFFFF) {
        return std::unexpected(Error{
            "InvalidDds",
            std::string(what) + ": " + std::to_string(width) + "x" + std::to_string(height) +
                " exceeds the 16-bit width/height a BA2 entry stores."});
    }
    if (mipCount <= 0) mipCount = 1;
    if (mipCount > 0xFF) {
        return std::unexpected(Error{
            "InvalidDds",
            std::string(what) + ": " + std::to_string(mipCount) +
                " mips exceeds the one byte a BA2 entry stores."});
    }

    return DdsInfo{
        width,
        height,
        mipCount,
        isCube,
        arraySize,
        isVolume ? std::max(1, depth) : 1,
        dataOffset,
        *format,
    };
}

Result<std::vector<std::uint8_t>> buildDdsHeader(const DdsInfo& info) {
    std::vector<std::uint8_t> bytes(static_cast<size_t>(kDx10HeaderSize), 0);
    const auto linearSize = static_cast<std::uint32_t>(mipSize(info.format, info.width, info.height));

    writeU32(bytes, 0, kDdsMagic);
    writeU32(bytes, 4, 124);
    writeU32(bytes, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
    writeU32(bytes, 12, static_cast<std::uint32_t>(info.height));
    writeU32(bytes, 16, static_cast<std::uint32_t>(info.width));
    writeU32(bytes, 20, linearSize);
    writeU32(bytes, 24, static_cast<std::uint32_t>(std::max(1, info.depth)));
    writeU32(bytes, 28, static_cast<std::uint32_t>(info.mipCount));
    writeU32(bytes, 76, 32);
    writeU32(bytes, 80, kDdpfFourCc);
    writeU32(bytes, 84, kDx10FourCc);
    writeU32(bytes, 108, 0x1000 | (info.mipCount > 1 ? 0x400008U : 0U));
    if (info.isCubeMap) writeU32(bytes, 112, kCaps2CubeMap | 0xFC00U);
    writeU32(bytes, 128, info.dxgiFormat());
    writeU32(bytes, 132, 3);
    if (info.isCubeMap) writeU32(bytes, 136, 0x4U);
    writeU32(bytes, 140,
             static_cast<std::uint32_t>(std::max(1, info.isCubeMap ? info.arraySize / 6 : info.arraySize)));
    return bytes;
}

}  // namespace fo4ide::textures
