#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/JsonInt.hpp>
#include <fo4ide/core/PayloadLimits.hpp>
#include <fo4ide/core/Result.hpp>

namespace fo4ide::app::support {

inline std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

inline const nlohmann::json* argument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return nullptr;
    return &args[index];
}

inline std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argument(args, index);
    return value && value->is_string() ? value->get<std::string>() : std::string{};
}

inline bool boolArgument(const nlohmann::json& args, std::size_t index, bool fallback = false) {
    const auto* value = argument(args, index);
    return value && value->is_boolean() ? value->get<bool>() : fallback;
}

inline nlohmann::json pathResult(Result<std::filesystem::path> result) {
    if (!result) return std::string{};
    return result->string();
}

inline int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

inline Result<std::vector<unsigned char>> decodeBase64(std::string_view input) {
    std::size_t digits = 0;
    for (const unsigned char c : input) {
        if (c == '=') break;
        if (!std::isspace(c)) ++digits;
    }
    if (digits * 3 / 4 > core::kMaxDroppedFileBytes) {
        return std::unexpected(Error{"DroppedFileTooLarge", "Dropped file payload exceeds the 16 MiB limit"});
    }
    std::vector<unsigned char> output;
    output.reserve(input.size() * 3 / 4);
    unsigned int value = 0;
    int bits = -8;
    for (const unsigned char c : input) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int digit = base64Value(c);
        if (digit < 0) return std::unexpected(Error{"InvalidBase64", "Dropped file payload is not valid base64"});
        value = (value << 6) | static_cast<unsigned int>(digit);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<unsigned char>((value >> bits) & 0xFFU));
            bits -= 8;
        }
    }
    return output;
}

inline std::filesystem::path uniqueStageDirectory() {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    return std::filesystem::temp_directory_path() /
        ("FO4RE_Nif_" + std::to_string(ticks) + "_" + std::to_string(thread));
}

inline bool isWhitespace(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
}

inline std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

inline std::string readFileText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

inline void writeFileText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not open '" + path.string() + "' for writing.");
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) throw std::runtime_error("Could not write '" + path.string() + "'.");
}

inline int intNumberArgument(const nlohmann::json& args, std::size_t index, int fallback,
                             bool saturate = false) {
    const auto* value = argument(args, index);
    if (value == nullptr || !value->is_number()) return fallback;
    if (!value->is_number_float()) {
        return (saturate ? core::jsonIntSaturated(*value) : core::jsonInt(*value)).value_or(fallback);
    }
    const double number = value->get<double>();
    if (!std::isfinite(number)) return fallback;
    const double whole = std::trunc(number);
    const double low = std::numeric_limits<int>::min();
    const double high = std::numeric_limits<int>::max();
    if (whole < low || whole > high) {
        return saturate ? static_cast<int>(std::clamp(whole, low, high)) : fallback;
    }
    return static_cast<int>(whole);
}

}
