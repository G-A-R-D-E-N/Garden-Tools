#pragma once

#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/app/WikiService.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fo4ide/graph/GraphCompiler.hpp>
#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/GraphScriptLoader.hpp>
#include <fo4ide/graph/GraphToolService.hpp>
#include <fo4ide/graph/NodeDefinition.hpp>
#include <fo4ide/graph/NodePalette.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>

#include <fo4ide/core/JsonInt.hpp>

#include <cmath>
#include <limits>

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

inline bool boolArgument(const nlohmann::json& args, std::size_t index) {
    const auto* value = argument(args, index);
    return value && value->is_boolean() && value->get<bool>();
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

inline int intNumberArgument(const nlohmann::json& args, std::size_t index, int fallback) {
    const auto* value = argument(args, index);
    if (value == nullptr || !value->is_number()) return fallback;
    if (!value->is_number_float()) return core::jsonInt(*value).value_or(fallback);
    const double number = value->get<double>();
    if (!std::isfinite(number) || number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(number);
}

}
