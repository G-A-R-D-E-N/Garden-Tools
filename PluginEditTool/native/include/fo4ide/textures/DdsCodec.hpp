#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::textures {

struct DdsFormatInfo {
    std::uint8_t dxgi{};
    std::string_view name;
    int blockBytes{0};  // > 0 when block-compressed
    int pixelBytes{0};  // uncompressed bytes per pixel

    [[nodiscard]] bool isBlockCompressed() const noexcept { return blockBytes > 0; }
};

struct DdsInfo {
    int width{};
    int height{};
    int mipCount{};
    bool isCubeMap{};
    int arraySize{};
    int depth{};
    int dataOffset{};
    DdsFormatInfo format{};

    [[nodiscard]] std::uint8_t dxgiFormat() const noexcept { return format.dxgi; }

    // Sum of every mip's size times array/depth slices.
    [[nodiscard]] std::int64_t payloadSize() const;
};

// Format table lookup by DXGI format code (nullopt when not recognized).
std::optional<DdsFormatInfo> lookupFormat(std::uint8_t dxgi) noexcept;

// Size in bytes of one mip level (width/height floored to 1).
std::int64_t mipSize(const DdsFormatInfo& format, int width, int height) noexcept;

// Parse a DDS container (legacy or DX10). Returns the layout plus the byte
// offset of the first surface. Errors: "InvalidDds", "UnsupportedFormat".
Result<DdsInfo> parseDds(std::span<const std::uint8_t> bytes,
                         std::string_view what = "texture");

// Build a canonical DX10 DDS_HEADER for a given layout (used by the pack path
// and tests). Errors: "UnsupportedFormat".
Result<std::vector<std::uint8_t>> buildDdsHeader(const DdsInfo& info);

}  // namespace fo4ide::textures
