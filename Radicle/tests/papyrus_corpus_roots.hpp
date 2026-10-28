#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fo4::papyrus::test {

inline char rootListSeparator() {
#ifdef _WIN32
    return ';';
#else
    return ':';
#endif
}

inline std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

inline bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) return false;
    const auto tail = value.substr(value.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(tail[i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

inline std::vector<std::filesystem::path> rootsFromEnv(const char* variable) {
    std::vector<std::filesystem::path> roots;
    const char* raw = std::getenv(variable);
    if (!raw || !*raw) return roots;
    std::istringstream stream(raw);
    std::string part;
    while (std::getline(stream, part, rootListSeparator())) {
        const auto cleaned = trimmed(part);
        if (!cleaned.empty() && std::filesystem::is_directory(cleaned)) {
            roots.emplace_back(cleaned);
        }
    }
    return roots;
}

inline std::vector<std::filesystem::path> enumerateFiles(const std::filesystem::path& root,
                                                         std::string_view extension) {
    std::vector<std::filesystem::path> files;
    std::vector<std::filesystem::path> stack{root};
    std::error_code ec;
    while (!stack.empty()) {
        const auto dir = stack.back();
        stack.pop_back();
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code statusEc;
            const auto& entry = *it;
            if (entry.is_symlink(statusEc)) continue;
            if (entry.is_directory(statusEc)) {
                stack.push_back(entry.path());
            } else if (entry.is_regular_file(statusEc) &&
                       endsWithIgnoreCase(entry.path().extension().string(), extension)) {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace fo4::papyrus::test
