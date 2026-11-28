#include <fo4ide/assets/TextureResolver.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4ide::assets {
namespace {

std::array<std::uint32_t, 5> sha1(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const auto bitLength = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<std::uint8_t>((bitLength >> (8u * i)) & 0xFFu));
    }

    std::array<std::uint32_t, 5> h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const auto rotateLeft = [](std::uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    };
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i) {
            const auto p = offset + static_cast<std::size_t>(i) * 4u;
            w[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(message[p]) << 24u) |
                (static_cast<std::uint32_t>(message[p + 1]) << 16u) |
                (static_cast<std::uint32_t>(message[p + 2]) << 8u) |
                static_cast<std::uint32_t>(message[p + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[static_cast<std::size_t>(i)] =
                rotateLeft(w[static_cast<std::size_t>(i - 3)] ^
                               w[static_cast<std::size_t>(i - 8)] ^
                               w[static_cast<std::size_t>(i - 14)] ^
                               w[static_cast<std::size_t>(i - 16)],
                           1);
        }
        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f{};
            std::uint32_t k{};
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const auto temp = rotateLeft(a, 5) + f + e + k + w[static_cast<std::size_t>(i)];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    return h;
}

}  // namespace

std::string TextureResolver::sha1Hex16(std::string_view text) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(text.data());
    const auto digest = sha1(std::span<const std::uint8_t>(begin, text.size()));
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(40);
    for (const auto word : digest) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex.push_back(digits[(word >> shift) & 0xFu]);
        }
    }
    return hex.substr(0, 16);
}

}  // namespace fo4ide::assets
