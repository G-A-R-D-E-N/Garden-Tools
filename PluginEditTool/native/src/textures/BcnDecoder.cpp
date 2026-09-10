#include <fo4ide/textures/BcnDecoder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace fo4ide::textures {
namespace {

constexpr std::array<int, 4> kWeight2{0, 21, 43, 64};
constexpr std::array<int, 8> kWeight3{0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::array<int, 16> kWeight4{0, 4, 9, 13, 17, 21, 26, 30,
                                       34, 38, 43, 47, 51, 55, 60, 64};

// BC7 endpoint/partition tables, byte-identical to the C# port.
constexpr std::string_view kPartitions2Hex =
    "80000101000001010000010100000181" "80000001000000010000000100000081"
    "80010101000101010001010100010181" "80000001000001010000010100010181"
    "80000000000000010000000100000181" "80000101000101010001010101010181"
    "80000001000001010001010101010181" "80000000000000010000010100010181"
    "80000000000000000000000100000181" "80000101000101010101010101010181"
    "80000000000000010001010101010181" "80000000000000000000000100010181"
    "80000001000101010101010101010181" "80000000000000000101010101010181"
    "80000000010101010101010101010181" "80000000000000000000000001010181"
    "80000000010000000101010001010181" "80018101000000010000000000000000"
    "80000000000000008100000001010100" "80018101000001010000000100000000"
    "80008101000000010000000000000000" "80000000010000008101000001010100"
    "80000000000000008100000001010000" "80010101000001010000010100000081"
    "80008101000000010000000100000000" "80000000010000008100000001010000"
    "80018100000101000001010000010100" "80008101000101000001010001010000"
    "80000001000101018101010001000000" "80000000010101018101010100000000"
    "80018101000000010100000001010100" "80008101010000010100000101010000"
    "80010001000100010001000100010081" "80000000010101010000000001010181"
    "80010001010081000001000101000100" "80000101000001018101000001010000"
    "80008101010100000000010101010000" "80010001000100018100010001000100"
    "80010100010000010001010001000081" "80010001010001000100010000010081"
    "80018101000001010101000001010100" "80000001000001018101000001000000"
    "80008101000001000001000001010000" "80008101010001010101000101010000"
    "80018100010000010100000100010100" "80000101010100000101000000000181"
    "80010100000101000100000101000081" "80000000000181000001010000000000"
    "80010000010181000001000000000000" "80008100000101010000010000000000"
    "80000000000081000001010100000100" "80000000000100008101010000010000"
    "80010100010100000100000100000181" "80000101000101000101000001000081"
    "80018100000001010100000101010000" "80008101010000010101000000010100"
    "80010100010100000101000001000081" "80010100000001010000010101000081"
    "80010101010101000100000000000081" "80000001010000000101010000010181"
    "80000000010101010000010100000181" "80008101000001010101010100000000"
    "80008100000001000101010001010100" "80010000000100000001010100010181";

constexpr std::string_view kPartitions3Hex =
    "80000181000001010002020102020282" "80000081000001018202010102020201"
    "80000000020000018202010102020181" "80020282000002020000010100010181"
    "80000000000000008101020201010282" "80000181000001010000020200000282"
    "80000282000002020101010101010181" "80000101000001018202010102020181"
    "80000000000000008101010102020282" "80000000010101018101010102020282"
    "80000000010181010202020202020282" "80000102000081020000010200000182"
    "80010102000181020001010200010182" "80010202008102020001020200010282"
    "80000181000101020101020201020282" "80000181020000018202000002020200"
    "80000081000001010001010201010282" "80010181000001018200000102020000"
    "80000000010102028101020201010282" "80000282000002020000020201010181"
    "80010181000101010002020200020282" "80000081000000018202020102020201"
    "80000000000081010001020200010282" "80000000010100008202810002020100"
    "80010282008102020000010100000000" "80000102000001028101020202020282"
    "80010100010282018102020100010100" "80000000000181000102820101020201"
    "80000202010100028101000200000282" "80010100008101000200000202020282"
    "80000101000102020001820200000181" "80000000020000008202010102020281"
    "80000000000000028101020201020282" "80020282000002020000010200000181"
    "80000181000001020000020200020282" "80010200008102000001820000010200"
    "80000000010181010202820200000000" "80010200010200018200810200010200"
    "80010200020001028182000100010200" "80000101020200000101820200000181"
    "80000101010182020202000000000181" "80010081000100010202020202020282"
    "80000000000000008201020102010281" "80000202018102020000020201010282"
    "80000282000001010000020200000181" "80020200010282010002020001020281"
    "80010001020282020202020200010081" "80000000020102018201020102010281"
    "80010081000100010001000102020282" "80020282000101010002020200010181"
    "80000002018101020000000201010182" "80000000028101020201010202010182"
    "80020202008101010001010100020282" "80000002010101028101010200000082"
    "80010100008101000001010002020282" "80000000000000000201810202010182"
    "80010100008101000202020202020282" "80000202000001010000810100000282"
    "80000202010102028101020200000282" "80000000000000000000000002810182"
    "80000082000000010000000200000081" "80020202010202020002020281020282"
    "80010081020202020202020202020282" "80010181020001018202000102020200";

std::array<std::uint8_t, 64 * 16> parseTable(std::string_view hex) {
    std::array<std::uint8_t, 64 * 16> table{};
    auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        return static_cast<std::uint8_t>(c - 'a' + 10);
    };
    for (size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4U) | nibble(hex[i * 2 + 1]));
    }
    return table;
}

const std::array<std::uint8_t, 64 * 16>& partitions2() {
    static const auto table = parseTable(kPartitions2Hex);
    return table;
}

const std::array<std::uint8_t, 64 * 16>& partitions3() {
    static const auto table = parseTable(kPartitions3Hex);
    return table;
}

void set(std::span<std::uint8_t> dst, int slot, int r, int g, int b, int a) {
    dst[slot * 4] = static_cast<std::uint8_t>(r);
    dst[slot * 4 + 1] = static_cast<std::uint8_t>(g);
    dst[slot * 4 + 2] = static_cast<std::uint8_t>(b);
    dst[slot * 4 + 3] = static_cast<std::uint8_t>(a);
}

// BC1-style color block: 2 565 endpoints + 16 2-bit indices.
void colorBlock(std::span<const std::uint8_t> block, std::span<std::uint8_t> texels, bool onlyOpaque) {
    const auto c0 = static_cast<int>(block[0]) | (static_cast<int>(block[1]) << 8);
    const auto c1 = static_cast<int>(block[2]) | (static_cast<int>(block[3]) << 8);

    const auto r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    const auto r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;

    std::array<std::uint8_t, 16> refs{};
    set(refs, 0, (r0 * 527 + 23) >> 6, (g0 * 259 + 33) >> 6, (b0 * 527 + 23) >> 6, 255);
    set(refs, 1, (r1 * 527 + 23) >> 6, (g1 * 259 + 33) >> 6, (b1 * 527 + 23) >> 6, 255);

    if (c0 > c1 || onlyOpaque) {
        set(refs, 2, ((2 * r0 + r1) * 351 + 61) >> 7, ((2 * g0 + g1) * 2763 + 1039) >> 11,
            ((2 * b0 + b1) * 351 + 61) >> 7, 255);
        set(refs, 3, ((r0 + 2 * r1) * 351 + 61) >> 7, ((g0 + 2 * g1) * 2763 + 1039) >> 11,
            ((b0 + 2 * b1) * 351 + 61) >> 7, 255);
    } else {
        set(refs, 2, ((r0 + r1) * 1053 + 125) >> 8, ((g0 + g1) * 4145 + 1019) >> 11,
            ((b0 + b1) * 1053 + 125) >> 8, 255);
        set(refs, 3, 0, 0, 0, 0);
    }

    auto indices = static_cast<std::uint32_t>(block[4]) |
                   (static_cast<std::uint32_t>(block[5]) << 8U) |
                   (static_cast<std::uint32_t>(block[6]) << 16U) |
                   (static_cast<std::uint32_t>(block[7]) << 24U);
    for (int i = 0; i < 16; ++i) {
        const auto idx = static_cast<int>(indices & 3U);
        indices >>= 2;
        texels[i * 4] = refs[idx * 4];
        texels[i * 4 + 1] = refs[idx * 4 + 1];
        texels[i * 4 + 2] = refs[idx * 4 + 2];
        texels[i * 4 + 3] = refs[idx * 4 + 3];
    }
}

// BC4-style single-channel block: 2 8-bit endpoints + 16 3-bit indices.
void smoothAlpha(std::span<const std::uint8_t> block, std::span<std::uint8_t> values) {
    std::array<std::uint8_t, 8> palette{};
    palette[0] = block[0];
    palette[1] = block[1];

    if (palette[0] > palette[1]) {
        for (int i = 1; i < 7; ++i) {
            palette[i + 1] = static_cast<std::uint8_t>(((7 - i) * palette[0] + i * palette[1] + 3) / 7);
        }
    } else {
        for (int i = 1; i < 5; ++i) {
            palette[i + 1] = static_cast<std::uint8_t>(((5 - i) * palette[0] + i * palette[1] + 2) / 5);
        }
        palette[6] = 0x00;
        palette[7] = 0xFF;
    }

    std::uint64_t indices = 0;
    for (int i = 0; i < 6; ++i) indices |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) values[i] = palette[(indices >> (3 * i)) & 7];
}

void bc2(std::span<const std::uint8_t> block, std::span<std::uint8_t> texels) {
    colorBlock(block.subspan(8, 8), texels, true);
    for (int i = 0; i < 16; ++i) {
        texels[i * 4 + 3] = static_cast<std::uint8_t>(
            (((block[i >> 1] >> ((i & 1) * 4)) & 0xF)) * 17);
    }
}

void bc3(std::span<const std::uint8_t> block, std::span<std::uint8_t> texels) {
    colorBlock(block.subspan(8, 8), texels, true);
    std::array<std::uint8_t, 16> alpha{};
    smoothAlpha(block.subspan(0, 8), alpha);
    for (int i = 0; i < 16; ++i) texels[i * 4 + 3] = alpha[i];
}

void bc4(std::span<const std::uint8_t> block, std::span<std::uint8_t> texels) {
    std::array<std::uint8_t, 16> red{};
    smoothAlpha(block.subspan(0, 8), red);
    for (int i = 0; i < 16; ++i) set(texels, i, red[i], red[i], red[i], 255);
}

void bc5(std::span<const std::uint8_t> block, std::span<std::uint8_t> texels) {
    std::array<std::uint8_t, 16> red{};
    std::array<std::uint8_t, 16> green{};
    smoothAlpha(block.subspan(0, 8), red);
    smoothAlpha(block.subspan(8, 8), green);
    for (int i = 0; i < 16; ++i) set(texels, i, red[i], green[i], 0, 255);
}

// LSB-first bit reader over a 16-byte BC7 block.
class BitStream {
public:
    explicit BitStream(std::span<const std::uint8_t> block) {
        low_ = readLe64(block.subspan(0, 8));
        high_ = readLe64(block.subspan(8, 8));
    }

    int read(int count) {
        if (count <= 0) return 0;
        const auto mask = (1ULL << count) - 1;
        const auto value = static_cast<int>(low_ & mask);
        low_ >>= count;
        low_ |= (high_ & mask) << (64 - count);
        high_ >>= count;
        return value;
    }

    int readBit() { return read(1); }

private:
    static std::uint64_t readLe64(std::span<const std::uint8_t> b) {
        std::uint64_t value = 0;
        for (int i = 7; i >= 0; --i) {
            value = (value << 8U) | b[i];
        }
        return value;
    }

    std::uint64_t low_{};
    std::uint64_t high_{};
};

constexpr std::array<int, 8> kColorBitsPerMode{4, 6, 5, 7, 5, 7, 7, 5};
constexpr std::array<int, 8> kAlphaBitsPerMode{0, 0, 0, 0, 6, 8, 7, 5};
constexpr int kModesWithPBits = 0b11001011;

std::uint8_t partitionSet(int subsets, int partition, int texel) {
    if (subsets == 1) return texel == 0 ? 0x80 : 0;
    if (subsets == 2) return partitions2()[partition * 16 + texel];
    return partitions3()[partition * 16 + texel];
}

int interpolate(int a, int b, std::span<const int> weights, int index) {
    return (a * (64 - weights[index]) + b * weights[index] + 32) >> 6;
}

void bc7(std::span<const std::uint8_t> block, std::span<std::uint8_t> texels) {
    BitStream bits(block);

    int mode = 0;
    while (mode < 8 && bits.readBit() == 0) ++mode;
    if (mode >= 8) {
        std::fill(texels.begin(), texels.end(), std::uint8_t{0});
        return;
    }

    int partition = 0;
    int subsets = 1;
    int rotation = 0;
    int indexSelectionBit = 0;

    if (mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 7) {
        subsets = (mode == 0 || mode == 2) ? 3 : 2;
        partition = bits.read(mode == 0 ? 4 : 6);
    }
    if (mode == 4 || mode == 5) {
        rotation = bits.read(2);
        if (mode == 4) indexSelectionBit = bits.readBit();
    }

    const auto endpointCount = subsets * 2;
    const auto colorBits = kColorBitsPerMode[mode];
    const auto alphaBits = kAlphaBitsPerMode[mode];
    const auto hasPBits = (kModesWithPBits & (1 << mode)) != 0;

    std::array<std::array<int, 4>, 6> endpoints{};
    for (int c = 0; c < 3; ++c) {
        for (int e = 0; e < endpointCount; ++e) endpoints[e][c] = bits.read(colorBits);
    }
    if (alphaBits > 0) {
        for (int e = 0; e < endpointCount; ++e) endpoints[e][3] = bits.read(alphaBits);
    }

    if (hasPBits) {
        for (int e = 0; e < endpointCount; ++e) {
            for (int c = 0; c < 4; ++c) endpoints[e][c] <<= 1;
        }
        if (mode == 1) {
            const auto p0 = bits.readBit();
            const auto p1 = bits.readBit();
            for (int c = 0; c < 3; ++c) {
                endpoints[0][c] |= p0;
                endpoints[1][c] |= p0;
                endpoints[2][c] |= p1;
                endpoints[3][c] |= p1;
            }
        } else {
            for (int e = 0; e < endpointCount; ++e) {
                const auto p = bits.readBit();
                for (int c = 0; c < 4; ++c) endpoints[e][c] |= p;
            }
        }
    }

    const auto pBit = hasPBits ? 1 : 0;
    for (int e = 0; e < endpointCount; ++e) {
        const auto precision = colorBits + pBit;
        for (int c = 0; c < 3; ++c) {
            endpoints[e][c] <<= 8 - precision;
            endpoints[e][c] |= endpoints[e][c] >> precision;
        }
        const auto alphaPrecision = alphaBits + pBit;
        endpoints[e][3] <<= 8 - alphaPrecision;
        endpoints[e][3] |= endpoints[e][3] >> alphaPrecision;
    }

    if (alphaBits == 0) {
        for (int e = 0; e < endpointCount; ++e) endpoints[e][3] = 0xFF;
    }

    const auto indexBits = (mode == 0 || mode == 1) ? 3 : (mode == 6 ? 4 : 2);
    const auto indexBits2 = mode == 4 ? 3 : (mode == 5 ? 2 : 0);

    std::span<const int> weights;
    if (indexBits == 2) {
        weights = kWeight2;
    } else if (indexBits == 3) {
        weights = kWeight3;
    } else {
        weights = kWeight4;
    }
    std::span<const int> weights2 = indexBits2 == 2 ? std::span<const int>(kWeight2)
                                                    : std::span<const int>(kWeight3);

    std::array<int, 16> indices{};
    for (int i = 0; i < 16; ++i) {
        const auto setBits = partitionSet(subsets, partition, i);
        indices[i] = bits.read((setBits & 0x80) != 0 ? indexBits - 1 : indexBits);
    }

    for (int i = 0; i < 16; ++i) {
        const auto setBits = partitionSet(subsets, partition, i) & 0x03;
        const auto e0 = setBits * 2;
        const auto e1 = setBits * 2 + 1;
        const auto index = indices[i];

        int r{}, g{}, b{}, a{};
        if (indexBits2 == 0) {
            r = interpolate(endpoints[e0][0], endpoints[e1][0], weights, index);
            g = interpolate(endpoints[e0][1], endpoints[e1][1], weights, index);
            b = interpolate(endpoints[e0][2], endpoints[e1][2], weights, index);
            a = interpolate(endpoints[e0][3], endpoints[e1][3], weights, index);
        } else {
            const auto index2 = bits.read(i == 0 ? indexBits2 - 1 : indexBits2);
            if (indexSelectionBit == 0) {
                r = interpolate(endpoints[e0][0], endpoints[e1][0], weights, index);
                g = interpolate(endpoints[e0][1], endpoints[e1][1], weights, index);
                b = interpolate(endpoints[e0][2], endpoints[e1][2], weights, index);
                a = interpolate(endpoints[e0][3], endpoints[e1][3], weights2, index2);
            } else {
                r = interpolate(endpoints[e0][0], endpoints[e1][0], weights2, index2);
                g = interpolate(endpoints[e0][1], endpoints[e1][1], weights2, index2);
                b = interpolate(endpoints[e0][2], endpoints[e1][2], weights2, index2);
                a = interpolate(endpoints[e0][3], endpoints[e1][3], weights, index);
            }
        }

        switch (rotation) {
            case 1: std::swap(a, r); break;
            case 2: std::swap(a, g); break;
            case 3: std::swap(a, b); break;
            default: break;
        }

        set(texels, i, r, g, b, a);
    }
}

}  // namespace

bool canDecodeBlockFormat(std::uint8_t dxgi) noexcept {
    switch (dxgi) {
        case 70: case 71: case 72:
        case 73: case 74: case 75:
        case 76: case 77: case 78:
        case 79: case 80:
        case 82: case 83:
        case 97: case 98: case 99:
            return true;
        default:
            return false;
    }
}

Result<std::vector<std::uint8_t>> decodeBlockFormat(std::span<const std::uint8_t> surface,
                                                    std::uint8_t dxgi,
                                                    int width,
                                                    int height) {
    const auto blockBytes = (dxgi == 70 || dxgi == 71 || dxgi == 72 || dxgi == 79 || dxgi == 80) ? 8 : 16;
    const auto blocksX = (width + 3) / 4;
    const auto blocksY = (height + 3) / 4;
    const auto needed = static_cast<std::int64_t>(blocksX) * blocksY * blockBytes;
    if (surface.size() < static_cast<size_t>(needed)) {
        return std::unexpected(Error{
            "InvalidData",
            "Surface is " + std::to_string(surface.size()) + " bytes; " + std::to_string(width) +
                "x" + std::to_string(height) + " needs " + std::to_string(needed) + "."});
    }

    std::vector<std::uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    std::array<std::uint8_t, 64> texels{};

    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const auto block = surface.subspan((by * blocksX + bx) * blockBytes, blockBytes);
            switch (dxgi) {
                case 70: case 71: case 72: colorBlock(block, texels, false); break;
                case 73: case 74: case 75: bc2(block, texels); break;
                case 76: case 77: case 78: bc3(block, texels); break;
                case 79: case 80: bc4(block, texels); break;
                case 82: case 83: bc5(block, texels); break;
                default: bc7(block, texels); break;
            }

            for (int y = 0; y < 4; ++y) {
                const auto py = by * 4 + y;
                if (py >= height) break;
                for (int x = 0; x < 4; ++x) {
                    const auto px = bx * 4 + x;
                    if (px >= width) break;
                    const auto dst = (py * width + px) * 4;
                    const auto src = (y * 4 + x) * 4;
                    rgba[dst] = texels[src];
                    rgba[dst + 1] = texels[src + 1];
                    rgba[dst + 2] = texels[src + 2];
                    rgba[dst + 3] = texels[src + 3];
                }
            }
        }
    }

    return rgba;
}

}  // namespace fo4ide::textures
