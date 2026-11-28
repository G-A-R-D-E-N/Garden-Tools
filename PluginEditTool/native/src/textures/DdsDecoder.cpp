#include <fo4ide/textures/DdsDecoder.hpp>

#include <algorithm>
#include <cmath>
#include <exception>

#include <fo4/texture/dds.hpp>
#include <fo4/texture/png.hpp>

namespace fo4ide::textures {
namespace {

void rebuildBlue(std::vector<std::uint8_t>& rgba) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const auto x = rgba[i] / 255.0 * 2.0 - 1.0;
        const auto y = rgba[i + 1] / 255.0 * 2.0 - 1.0;
        const auto z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
        rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(std::lround((z + 1.0) * 0.5 * 255.0), 0L, 255L));
    }
}

}  // namespace

Result<std::vector<std::uint8_t>> decodeDds(std::span<const std::uint8_t> ddsBytes,
                                            int& width,
                                            int& height,
                                            bool reconstructZ) {
    try {
        std::uint32_t w{};
        std::uint32_t h{};
        auto rgba = fo4::texture::decode_dds_rgba(ddsBytes, 0U, false, w, h);
        if (reconstructZ) rebuildBlue(rgba);
        width = static_cast<int>(w);
        height = static_cast<int>(h);
        return rgba;
    } catch (const fo4::texture::dds_error& error) {
        return std::unexpected(Error{error.type(), error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidDds", error.what()});
    }
}

Result<std::vector<std::uint8_t>> ddsToPng(std::span<const std::uint8_t> ddsBytes, bool reconstructZ) {
    int width{};
    int height{};
    auto rgba = decodeDds(ddsBytes, width, height, reconstructZ);
    if (!rgba) return std::unexpected(rgba.error());
    try {
        return fo4::texture::encode_png_rgba(*rgba, static_cast<std::uint32_t>(width),
                                             static_cast<std::uint32_t>(height));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidDds", error.what()});
    }
}

bool isBc5(std::span<const std::uint8_t> ddsBytes) noexcept { return fo4::texture::is_bc5(ddsBytes); }

}  // namespace fo4ide::textures
