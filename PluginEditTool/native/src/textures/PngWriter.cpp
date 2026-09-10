#include <fo4ide/textures/PngWriter.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <zlib.h>

namespace fo4ide::textures {
namespace {

constexpr std::array<std::uint8_t, 8> kSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

void writeBigEndian(std::vector<std::uint8_t>& dst, std::uint32_t value) {
    dst.push_back(static_cast<std::uint8_t>(value >> 24U));
    dst.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    dst.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    dst.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::array<std::uint32_t, 256> buildCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        auto c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}

std::uint32_t crc32(std::span<const std::uint8_t> type, std::span<const std::uint8_t> data) {
    static const auto table = buildCrcTable();
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto x : type) crc = table[(crc ^ x) & 0xFFU] ^ (crc >> 8U);
    for (const auto x : data) crc = table[(crc ^ x) & 0xFFU] ^ (crc >> 8U);
    return crc ^ 0xFFFFFFFFU;
}

void writeChunk(std::vector<std::uint8_t>& output,
                std::string_view type,
                std::span<const std::uint8_t> data) {
    writeBigEndian(output, static_cast<std::uint32_t>(data.size()));
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), data.begin(), data.end());
    const auto crc = crc32(std::span<const std::uint8_t>(
                               reinterpret_cast<const std::uint8_t*>(type.data()), type.size()),
                           data);
    writeBigEndian(output, crc);
}

Result<std::vector<std::uint8_t>> deflate(std::span<const std::uint8_t> rgba,
                                          int width,
                                          int height) {
    const auto stride = static_cast<size_t>(width) * 4;
    const auto rawSize = static_cast<size_t>(height) * (stride + 1);
    std::vector<std::uint8_t> raw(rawSize, 0);
    for (int y = 0; y < height; ++y) {
        // filter byte 0 (None) + one scanline, matching the C# writer
        std::copy_n(rgba.begin() + static_cast<size_t>(y) * stride, stride,
                    raw.begin() + static_cast<size_t>(y) * (stride + 1) + 1);
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedSize);
    const auto result = compress2(compressed.data(), &compressedSize, raw.data(),
                                  static_cast<uLong>(raw.size()), Z_BEST_SPEED);
    if (result != Z_OK) {
        return std::unexpected(Error{
            "PngEncodeFailed",
            "zlib compress2 failed with code " + std::to_string(result) + "."});
    }
    compressed.resize(compressedSize);
    return compressed;
}

}  // namespace

Result<std::vector<std::uint8_t>> writePng(std::span<const std::uint8_t> rgba,
                                           int width,
                                           int height) {
    const auto expected = static_cast<std::int64_t>(width) * height * 4;
    if (rgba.size() != static_cast<size_t>(expected)) {
        return std::unexpected(Error{
            "InvalidArgument",
            std::to_string(width) + "x" + std::to_string(height) + " RGBA is " +
                std::to_string(expected) + " bytes, got " + std::to_string(rgba.size()) + "."});
    }

    std::vector<std::uint8_t> output(kSignature.begin(), kSignature.end());

    // IHDR: width, height, bit depth 8, color type 6 (RGBA), 0/0/0
    std::array<std::uint8_t, 13> ihdr{};
    ihdr[0] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 24U) & 0xFFU);
    ihdr[1] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 16U) & 0xFFU);
    ihdr[2] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 8U) & 0xFFU);
    ihdr[3] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(width) & 0xFFU);
    ihdr[4] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 24U) & 0xFFU);
    ihdr[5] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 16U) & 0xFFU);
    ihdr[6] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 8U) & 0xFFU);
    ihdr[7] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) & 0xFFU);
    ihdr[8] = 8;
    ihdr[9] = 6;
    writeChunk(output, "IHDR", ihdr);

    auto idat = deflate(rgba, width, height);
    if (!idat) return std::unexpected(idat.error());
    writeChunk(output, "IDAT", *idat);

    writeChunk(output, "IEND", {});
    return output;
}

}  // namespace fo4ide::textures
