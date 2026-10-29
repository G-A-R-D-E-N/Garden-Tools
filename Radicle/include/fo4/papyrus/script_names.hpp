#pragma once

#include <cstddef>
#include <string_view>

namespace fo4::papyrus {

inline bool safeScriptObjectName(std::string_view name) {
    if (name.empty()) return false;
    std::size_t begin = 0;
    while (begin <= name.size()) {
        const auto end = name.find(':', begin);
        const auto length = end == std::string_view::npos ? name.size() - begin : end - begin;
        const auto part = name.substr(begin, length);
        if (part.empty() || part == "." || part == "..") return false;
        for (const unsigned char ch : part) {
            if (ch < 0x20 || ch == '/' || ch == '\\' || ch == 0x7F || ch == '<' || ch == '>' || ch == '"' ||
                ch == '|' || ch == '?' || ch == '*') {
                return false;
            }
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

}  // namespace fo4::papyrus
