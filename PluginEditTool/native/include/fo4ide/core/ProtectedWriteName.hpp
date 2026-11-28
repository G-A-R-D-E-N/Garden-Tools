#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace fo4ide::core {

inline std::optional<std::string> protectedWriteName(std::string name) {
    if (name.find(':') != std::string::npos) return "cannot write an alternate data stream";
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
    std::ranges::transform(name, name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    constexpr std::string_view denied[] = {
        ".exe", ".dll", ".bat", ".cmd", ".ps1", ".vbs", ".js", ".msi", ".lnk", ".scr", ".com",
        ".py", ".pyw", ".hta", ".vbe", ".jse", ".wsf", ".wsh", ".reg", ".jar", ".psm1", ".cpl", ".url", ".pif", ".inf"};
    if (std::ranges::any_of(denied, [&name](std::string_view suffix) { return name.ends_with(suffix); }) ||
        name == "modorganizer.ini") return "is not a writable file type";
    return std::nullopt;
}

inline std::optional<std::string> unsafeScriptFileStem(std::string_view name) {
    if (name.empty()) return "is empty";
    const auto plain = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    if (!std::ranges::all_of(name, plain)) return "must use only ASCII letters, digits and underscore";
    std::string lower(name);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool numbered = lower.size() == 4 && lower[3] >= '1' && lower[3] <= '9';
    if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul" ||
        (numbered && (lower.starts_with("com") || lower.starts_with("lpt")))) {
        return "is a reserved device name";
    }
    return std::nullopt;
}

}
