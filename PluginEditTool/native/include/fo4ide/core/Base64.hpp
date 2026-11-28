#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::core {

// Standard RFC 4648 base64. Errors: "InvalidBase64" via a simple enum sentinel
// so callers can map it to their own error codes.

enum class Base64Error { None, InvalidInput };

inline std::string encodeBase64(std::span<const std::byte> data) {
    static constexpr char k_alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t index = 0;
    while (index + 3 <= data.size()) {
        const auto a = std::to_integer<std::uint32_t>(data[index]);
        const auto b = std::to_integer<std::uint32_t>(data[index + 1]);
        const auto c = std::to_integer<std::uint32_t>(data[index + 2]);
        const auto value = (a << 16u) | (b << 8u) | c;
        out.push_back(k_alphabet[(value >> 18u) & 0x3Fu]);
        out.push_back(k_alphabet[(value >> 12u) & 0x3Fu]);
        out.push_back(k_alphabet[(value >> 6u) & 0x3Fu]);
        out.push_back(k_alphabet[value & 0x3Fu]);
        index += 3;
    }
    const auto remaining = data.size() - index;
    if (remaining == 1) {
        const auto a = std::to_integer<std::uint32_t>(data[index]);
        out.push_back(k_alphabet[(a >> 2u) & 0x3Fu]);
        out.push_back(k_alphabet[(a << 4u) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const auto a = std::to_integer<std::uint32_t>(data[index]);
        const auto b = std::to_integer<std::uint32_t>(data[index + 1]);
        const auto value = (a << 8u) | b;
        out.push_back(k_alphabet[(value >> 10u) & 0x3Fu]);
        out.push_back(k_alphabet[(value >> 4u) & 0x3Fu]);
        out.push_back(k_alphabet[(value << 2u) & 0x3Fu]);
        out.push_back('=');
    }
    return out;
}

inline Base64Error decodeBase64(std::string_view input, std::vector<std::uint8_t>& output) {
    output.clear();
    output.reserve(input.size() * 3 / 4);
    const auto valueOf = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::uint32_t value = 0;
    int bits = -8;
    for (const unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int digit = valueOf(c);
        if (digit < 0) return Base64Error::InvalidInput;
        value = (value << 6u) | static_cast<std::uint32_t>(digit);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<std::uint8_t>((value >> bits) & 0xFFu));
            bits -= 8;
        }
    }
    return Base64Error::None;
}

}
