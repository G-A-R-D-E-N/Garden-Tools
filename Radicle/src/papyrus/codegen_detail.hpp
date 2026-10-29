#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <fo4/pex/pex.hpp>

namespace fo4::papyrus::codegen_detail {

using fo4::pex::PexFile;

inline std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

inline std::uint8_t opcodeFor(const std::string& name) {
    for (std::size_t i = 0; i < std::size(PexFile::kOpCodes); ++i) {
        if (name == PexFile::kOpCodes[i].name) return static_cast<std::uint8_t>(i);
    }
    throw std::runtime_error("No PEX opcode named '" + name + "'.");
}

}  // namespace fo4::papyrus::codegen_detail
