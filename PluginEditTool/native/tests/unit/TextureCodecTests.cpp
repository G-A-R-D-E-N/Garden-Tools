#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include <fo4/texture/dds.hpp>

#include <fo4ide/textures/DdsDecoder.hpp>
#include <fo4ide/textures/PngWriter.hpp>

namespace {

using namespace fo4ide::textures;

std::vector<std::uint8_t> dx10Dds(std::uint8_t dxgi, std::span<const std::uint8_t> surface) {
    const auto format = fo4::texture::lookup_format(dxgi);
    REQUIRE(format);
    const fo4::texture::DdsInfo info{4, 4, 1, false, 1, 1, 148, *format};
    auto header = fo4::texture::build_dds_header(info);
    header.insert(header.end(), surface.begin(), surface.end());
    return header;
}

}  // namespace

TEST_CASE("DdsDecoder decodes an uncompressed RGBA8 surface") {
    std::vector<std::uint8_t> surface;
    for (int i = 0; i < 16; ++i) {
        surface.push_back(static_cast<std::uint8_t>(i));
        surface.push_back(static_cast<std::uint8_t>(i + 16));
        surface.push_back(static_cast<std::uint8_t>(i + 32));
        surface.push_back(static_cast<std::uint8_t>(i + 48));
    }
    const auto dds = dx10Dds(28, surface);

    int width{};
    int height{};
    auto decoded = decodeDds(dds, width, height);
    REQUIRE(decoded);
    CHECK(width == 4);
    CHECK(height == 4);
    CHECK((*decoded)[0] == 0);
    CHECK((*decoded)[1] == 16);
    CHECK((*decoded)[2] == 32);
    CHECK((*decoded)[3] == 48);
}

TEST_CASE("DdsDecoder decodes a BC1 dds end to end") {
    const std::array<std::uint8_t, 8> block{0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto dds = dx10Dds(71, block);

    int width{};
    int height{};
    auto decoded = decodeDds(dds, width, height);
    REQUIRE(decoded);
    CHECK(width == 4);
    CHECK(height == 4);
    CHECK((*decoded)[0] == 255);
    CHECK((*decoded)[3] == 255);
}

TEST_CASE("DdsDecoder reconstructs Z for normal maps") {
    std::vector<std::uint8_t> surface;
    for (int i = 0; i < 16; ++i) {
        surface.push_back(255);  // x = 1.0
        surface.push_back(0);    // y = -1.0
        surface.push_back(0);
        surface.push_back(255);
    }
    const auto dds = dx10Dds(28, surface);

    int width{};
    int height{};
    auto decoded = decodeDds(dds, width, height, /*reconstructZ=*/true);
    REQUIRE(decoded);
    // x^2 + y^2 > 1 -> z = 0 -> (0 + 1) * 0.5 * 255 = 127.5 -> 128
    CHECK((*decoded)[2] == 128);
}

TEST_CASE("PngWriter produces a valid PNG that round-trips") {
    std::vector<std::uint8_t> rgba;
    for (int i = 0; i < 4; ++i) {
        rgba.push_back(static_cast<std::uint8_t>(1 + i * 4));
        rgba.push_back(static_cast<std::uint8_t>(2 + i * 4));
        rgba.push_back(static_cast<std::uint8_t>(3 + i * 4));
        rgba.push_back(static_cast<std::uint8_t>(4 + i * 4));
    }

    auto png = writePng(rgba, 2, 2);
    REQUIRE(png);

    // Signature
    const std::array<std::uint8_t, 8> signature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(std::equal(signature.begin(), signature.end(), png->begin()));

    // IHDR
    size_t pos = 8;
    auto readU32 = [&] {
        const auto value = static_cast<std::uint32_t>((*png)[pos]) << 24U |
                           (static_cast<std::uint32_t>((*png)[pos + 1]) << 16U) |
                           (static_cast<std::uint32_t>((*png)[pos + 2]) << 8U) |
                           static_cast<std::uint32_t>((*png)[pos + 3]);
        pos += 4;
        return value;
    };
    auto readType = [&] {
        const std::string type(png->begin() + static_cast<std::ptrdiff_t>(pos),
                               png->begin() + static_cast<std::ptrdiff_t>(pos) + 4);
        pos += 4;
        return type;
    };

    const auto ihdrLength = readU32();
    CHECK(ihdrLength == 13);
    CHECK(readType() == "IHDR");
    const auto ihdrDataStart = pos;
    const auto pngWidth = readU32();
    const auto pngHeight = readU32();
    CHECK(pngWidth == 2);
    CHECK(pngHeight == 2);
    CHECK((*png)[ihdrDataStart + 8] == 8);    // bit depth
    CHECK((*png)[ihdrDataStart + 9] == 6);    // color type RGBA
    pos = ihdrDataStart + ihdrLength + 4;     // skip data + crc

    // IDAT
    const auto idatLength = readU32();
    REQUIRE(readType() == "IDAT");
    const std::vector<std::uint8_t> idat(png->begin() + static_cast<std::ptrdiff_t>(pos),
                                         png->begin() + static_cast<std::ptrdiff_t>(pos) +
                                             static_cast<std::ptrdiff_t>(idatLength));

    std::vector<std::uint8_t> raw(2 * (2 * 4 + 1));
    uLongf rawSize = static_cast<uLongf>(raw.size());
    const auto result = uncompress(raw.data(), &rawSize, idat.data(), static_cast<uLong>(idat.size()));
    REQUIRE(result == Z_OK);
    REQUIRE(rawSize == 18);

    // Row 0: filter byte 0 then 8 pixels bytes; row 1 likewise.
    const std::array<std::uint8_t, 18> expected{
        0, 1, 2, 3, 4, 5, 6, 7, 8,
        0, 9, 10, 11, 12, 13, 14, 15, 16,
    };
    CHECK(std::equal(expected.begin(), expected.end(), raw.begin()));
}

TEST_CASE("PngWriter rejects mismatched buffer size") {
    std::vector<std::uint8_t> rgba(7, 0);
    const auto png = writePng(rgba, 2, 2);
    REQUIRE_FALSE(png);
    CHECK(png.error().type == "InvalidArgument");
}
