#include <fo4ide/textures/DdsDecoder.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <fo4ide/textures/BcnDecoder.hpp>
#include <fo4ide/textures/DdsCodec.hpp>
#include <fo4ide/textures/PngWriter.hpp>

namespace fo4ide::textures {
namespace {

bool isUncompressed(std::uint8_t dxgi) {
    switch (dxgi) {
        case 28: case 29: case 87: case 88: case 91: case 93:
        case 49: case 61: case 65: case 85: case 86: case 115:
            return true;
        default:
            return false;
    }
}

void reconstructZ(std::span<std::uint8_t> rgba) {
    for (size_t i = 0; i < rgba.size(); i += 4) {
        const auto x = rgba[i] / 255.0 * 2.0 - 1.0;
        const auto y = rgba[i + 1] / 255.0 * 2.0 - 1.0;
        const auto z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
        rgba[i + 2] = static_cast<std::uint8_t>(
            std::clamp(std::lround((z + 1.0) * 0.5 * 255.0), 0L, 255L));
    }
}

void unpack(std::span<const std::uint8_t> surface,
            std::span<std::uint8_t> rgba,
            std::uint8_t dxgi,
            int pixels,
            int stride) {
    for (int i = 0; i < pixels; ++i) {
        const auto s = static_cast<size_t>(i) * stride;
        const auto d = static_cast<size_t>(i) * 4;
        switch (dxgi) {
            case 28: case 29:
                rgba[d] = surface[s]; rgba[d + 1] = surface[s + 1];
                rgba[d + 2] = surface[s + 2]; rgba[d + 3] = surface[s + 3];
                break;
            case 87: case 91:
                rgba[d] = surface[s + 2]; rgba[d + 1] = surface[s + 1];
                rgba[d + 2] = surface[s]; rgba[d + 3] = surface[s + 3];
                break;
            case 88: case 93:
                rgba[d] = surface[s + 2]; rgba[d + 1] = surface[s + 1];
                rgba[d + 2] = surface[s]; rgba[d + 3] = 255;
                break;
            case 49:
                rgba[d] = surface[s]; rgba[d + 1] = surface[s + 1];
                rgba[d + 2] = 0; rgba[d + 3] = 255;
                break;
            case 61:
                rgba[d] = rgba[d + 1] = rgba[d + 2] = surface[s]; rgba[d + 3] = 255;
                break;
            case 65:
                rgba[d] = rgba[d + 1] = rgba[d + 2] = 0; rgba[d + 3] = surface[s];
                break;
            case 85: {
                const auto v = static_cast<int>(surface[s]) | (static_cast<int>(surface[s + 1]) << 8);
                const auto r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
                rgba[d] = static_cast<std::uint8_t>((r5 * 527 + 23) >> 6);
                rgba[d + 1] = static_cast<std::uint8_t>((g6 * 259 + 33) >> 6);
                rgba[d + 2] = static_cast<std::uint8_t>((b5 * 527 + 23) >> 6);
                rgba[d + 3] = 255;
                break;
            }
            case 86: {
                const auto v = static_cast<int>(surface[s]) | (static_cast<int>(surface[s + 1]) << 8);
                const auto r5 = (v >> 10) & 0x1F, g5 = (v >> 5) & 0x1F, b5 = v & 0x1F;
                rgba[d] = static_cast<std::uint8_t>((r5 * 527 + 23) >> 6);
                rgba[d + 1] = static_cast<std::uint8_t>((g5 * 527 + 23) >> 6);
                rgba[d + 2] = static_cast<std::uint8_t>((b5 * 527 + 23) >> 6);
                rgba[d + 3] = static_cast<std::uint8_t>((v & 0x8000) != 0 ? 255 : 0);
                break;
            }
            case 115: {
                const auto v = static_cast<int>(surface[s]) | (static_cast<int>(surface[s + 1]) << 8);
                rgba[d] = static_cast<std::uint8_t>(((v >> 8) & 0xF) * 17);
                rgba[d + 1] = static_cast<std::uint8_t>(((v >> 4) & 0xF) * 17);
                rgba[d + 2] = static_cast<std::uint8_t>((v & 0xF) * 17);
                rgba[d + 3] = static_cast<std::uint8_t>(((v >> 12) & 0xF) * 17);
                break;
            }
            default:
                throw Error{"UnsupportedFormat",
                            "DXGI format " + std::to_string(dxgi) +
                                " is not an uncompressed layout this tool unpacks."};
        }
    }
}

}  // namespace

Result<std::vector<std::uint8_t>> decodeDds(std::span<const std::uint8_t> ddsBytes,
                                            int& width,
                                            int& height,
                                            bool reconstructZValue) {
    auto parsed = parseDds(ddsBytes);
    if (!parsed) return std::unexpected(parsed.error());
    const auto& info = *parsed;
    width = info.width;
    height = info.height;

    const auto surfaceLength = static_cast<int>(mipSize(info.format, info.width, info.height));
    if (ddsBytes.size() < static_cast<size_t>(info.dataOffset) + static_cast<size_t>(surfaceLength)) {
        return std::unexpected(Error{
            "InvalidDds",
            "Texture is " + std::to_string(ddsBytes.size()) + " bytes; its top mip alone needs " +
                std::to_string(static_cast<std::int64_t>(info.dataOffset) + surfaceLength) + "."});
    }
    const auto surface = ddsBytes.subspan(static_cast<size_t>(info.dataOffset),
                                          static_cast<size_t>(surfaceLength));

    const auto pixels = static_cast<std::int64_t>(width) * height;
    if (pixels * 4 > std::numeric_limits<int>::max()) {
        return std::unexpected(Error{
            "TooLarge",
            std::to_string(width) + "x" + std::to_string(height) +
                " is too large to decode into one buffer."});
    }
    std::vector<std::uint8_t> rgba(static_cast<size_t>(pixels) * 4);

    try {
        if (canDecodeBlockFormat(info.dxgiFormat())) {
            auto decoded = decodeBlockFormat(surface, info.dxgiFormat(), width, height);
            if (!decoded) return std::unexpected(decoded.error());
            rgba = std::move(*decoded);
        } else if (isUncompressed(info.dxgiFormat())) {
            unpack(surface, rgba, info.dxgiFormat(), static_cast<int>(pixels), info.format.pixelBytes);
        } else {
            return std::unexpected(Error{
                "UnsupportedFormat",
                std::string(info.format.name) + " cannot be decoded in process."});
        }
    } catch (const Error& error) {
        return std::unexpected(error);
    }

    if (reconstructZValue) reconstructZ(rgba);
    return rgba;
}

Result<std::vector<std::uint8_t>> ddsToPng(std::span<const std::uint8_t> ddsBytes,
                                           bool reconstructZValue) {
    int width{};
    int height{};
    auto rgba = decodeDds(ddsBytes, width, height, reconstructZValue);
    if (!rgba) return std::unexpected(rgba.error());
    return writePng(*rgba, width, height);
}

bool isBc5(std::span<const std::uint8_t> ddsBytes) noexcept {
    auto parsed = parseDds(ddsBytes);
    if (!parsed) return false;
    const auto dxgi = parsed->dxgiFormat();
    return dxgi == 82 || dxgi == 83 || dxgi == 84;
}

}  // namespace fo4ide::textures
