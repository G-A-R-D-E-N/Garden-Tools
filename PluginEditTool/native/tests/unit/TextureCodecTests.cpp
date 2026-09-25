#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include <fo4ide/textures/BcnDecoder.hpp>
#include <fo4ide/textures/DdsCodec.hpp>
#include <fo4ide/textures/DdsDecoder.hpp>
#include <fo4ide/textures/PngWriter.hpp>

namespace {

using namespace fo4ide::textures;

void u32(std::vector<std::uint8_t>& bytes, size_t offset, std::uint32_t value) {
    if (bytes.size() < offset + 4) bytes.resize(offset + 4, 0);
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

// LSB-first bit writer used to assemble synthetic BC7 blocks.
struct BitWriter {
    std::uint64_t lo{};
    std::uint64_t hi{};
    int n{};

    void write(int value, int count) {
        for (int i = 0; i < count; ++i) {
            const auto bit = (value >> i) & 1;
            if (n < 64) {
                lo |= static_cast<std::uint64_t>(bit) << n;
            } else {
                hi |= static_cast<std::uint64_t>(bit) << (n - 64);
            }
            ++n;
        }
    }

    std::array<std::uint8_t, 16> bytes() const {
        std::array<std::uint8_t, 16> out{};
        for (int i = 0; i < 8; ++i) {
            out[i] = static_cast<std::uint8_t>((lo >> (8 * i)) & 0xFFU);
            out[8 + i] = static_cast<std::uint8_t>((hi >> (8 * i)) & 0xFFU);
        }
        return out;
    }
};

std::vector<std::uint8_t> dx10Dds(std::uint8_t dxgi, std::span<const std::uint8_t> surface) {
    DdsFormatInfo format{dxgi, "test", 0, 0};
    if (dxgi >= 70 && dxgi <= 99) {
        format.blockBytes = (dxgi == 70 || dxgi == 71 || dxgi == 72 || dxgi == 79 || dxgi == 80) ? 8 : 16;
    } else {
        switch (dxgi) {
            case 28: case 29: case 87: case 88: case 91: case 93: format.pixelBytes = 4; break;
            case 49: format.pixelBytes = 2; break;
            case 85: case 86: case 115: format.pixelBytes = 2; break;
            default: break;
        }
    }
    const DdsInfo info{4, 4, 1, false, 1, 1, 148, format};
    auto header = buildDdsHeader(info);
    REQUIRE(header);
    header->insert(header->end(), surface.begin(), surface.end());
    return *header;
}

}  // namespace

TEST_CASE("DdsCodec rejects non-DDS input") {
    std::vector<std::uint8_t> junk(128, 0);
    const auto parsed = parseDds(junk);
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().type == "InvalidDds");
}

TEST_CASE("DdsCodec parses a legacy DXT1 header") {
    std::vector<std::uint8_t> bytes(128, 0);
    u32(bytes, 0, 0x20534444U);  // "DDS "
    u32(bytes, 4, 124);
    u32(bytes, 12, 4);  // height
    u32(bytes, 16, 4);  // width
    u32(bytes, 28, 1);  // mip count
    u32(bytes, 80, 0x4U);          // DDPF_FOURCC
    u32(bytes, 84, 0x31545844U);   // "DXT1"

    const auto parsed = parseDds(bytes);
    REQUIRE(parsed);
    CHECK(parsed->width == 4);
    CHECK(parsed->height == 4);
    CHECK(parsed->dataOffset == 128);
    CHECK(parsed->dxgiFormat() == 71);
}

TEST_CASE("DdsCodec parses a DX10 header built for BC1") {
    const DdsFormatInfo format{71, "BC1_UNORM", 8, 0};
    const DdsInfo info{8, 8, 1, false, 1, 1, 148, format};
    auto header = buildDdsHeader(info);
    REQUIRE(header);

    const auto parsed = parseDds(*header);
    REQUIRE(parsed);
    CHECK(parsed->width == 8);
    CHECK(parsed->height == 8);
    CHECK(parsed->dataOffset == 148);
    CHECK(parsed->dxgiFormat() == 71);
    CHECK(parsed->format.isBlockCompressed());
    CHECK(parsed->payloadSize() == 32);  // 2x2 blocks of 8 bytes
}

TEST_CASE("BcnDecoder decodes a BC1 four-color block to white") {
    // c0 = 0xFFFF (white 565), c1 = 0x0000 (black), all indices 0.
    const std::array<std::uint8_t, 8> block{0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto decoded = decodeBlockFormat(block, 71, 4, 4);
    REQUIRE(decoded);
    REQUIRE(decoded->size() == 4 * 4 * 4);
    for (size_t i = 0; i < decoded->size(); i += 4) {
        CHECK((*decoded)[i] == 255);
        CHECK((*decoded)[i + 1] == 255);
        CHECK((*decoded)[i + 2] == 255);
        CHECK((*decoded)[i + 3] == 255);
    }
}

TEST_CASE("BcnDecoder decodes a BC1 three-color block with transparency") {
    // c0 = 0x0000 (black), c1 = 0xFFFF (white): c0 < c1 -> refs[3] transparent.
    const std::array<std::uint8_t, 8> block{0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    auto decoded = decodeBlockFormat(block, 71, 4, 4);
    REQUIRE(decoded);
    for (size_t i = 0; i < decoded->size(); i += 4) {
        CHECK((*decoded)[i] == 0);
        CHECK((*decoded)[i + 1] == 0);
        CHECK((*decoded)[i + 2] == 0);
        CHECK((*decoded)[i + 3] == 0);
    }
}

TEST_CASE("BcnDecoder decodes a BC3 block with opaque alpha") {
    // Alpha: a0=255 > a1=0, indices 0 -> alpha 255. Color: white opaque.
    const std::array<std::uint8_t, 16> block{
        255, 0, 0, 0, 0, 0, 0, 0,  // alpha block
        0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // white color block
    };
    auto decoded = decodeBlockFormat(block, 77, 4, 4);
    REQUIRE(decoded);
    for (size_t i = 0; i < decoded->size(); i += 4) {
        CHECK((*decoded)[i] == 255);
        CHECK((*decoded)[i + 3] == 255);
    }
}

TEST_CASE("BcnDecoder decodes a BC7 mode-0 all-black block") {
    // Mode is unary-coded: leading zeros then a 1, so mode 0 is a single 1 bit.
    // Then: partition 0 (4 bits), 6 endpoints x 3 channels x 4 bits = 0,
    // 6 p-bits = 0, 45 index bits = 0 (three anchors read 2 bits, rest 3).
    BitWriter writer;
    writer.write(1, 1);   // mode 0
    writer.write(0, 4);   // partition 0
    for (int i = 0; i < 72; ++i) writer.write(0, 1);  // endpoints
    for (int i = 0; i < 6; ++i) writer.write(0, 1);   // p-bits
    for (int i = 0; i < 45; ++i) writer.write(0, 1);  // indices
    REQUIRE(writer.n == 128);

    const auto block = writer.bytes();
    auto decoded = decodeBlockFormat(block, 98, 4, 4);
    REQUIRE(decoded);
    for (size_t i = 0; i < decoded->size(); i += 4) {
        CHECK((*decoded)[i] == 0);
        CHECK((*decoded)[i + 1] == 0);
        CHECK((*decoded)[i + 2] == 0);
        CHECK((*decoded)[i + 3] == 255);
    }
}

TEST_CASE("BcnDecoder rejects a truncated surface") {
    const std::array<std::uint8_t, 4> tiny{0, 0, 0, 0};
    const auto decoded = decodeBlockFormat(tiny, 71, 4, 4);
    REQUIRE_FALSE(decoded);
    CHECK(decoded.error().type == "InvalidData");
}

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
