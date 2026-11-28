#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace fo4::papyrus {
namespace detail {

// Folding must stay ASCII and byte-wise: the oracle folds one char at a time.
inline std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

// Name comparison must be the same ASCII fold the lexer applies to keywords.
inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = a[i];
        const auto y = b[i];
        if ((x >= 'A' && x <= 'Z' ? x - 'A' + 'a' : x) !=
            (y >= 'A' && y <= 'Z' ? y - 'A' + 'a' : y)) {
            return false;
        }
    }
    return true;
}

}  // namespace detail
}  // namespace fo4::papyrus
