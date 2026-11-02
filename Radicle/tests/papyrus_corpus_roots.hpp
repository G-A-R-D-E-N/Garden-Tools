#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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

// A staged .pex corpus must be present when FO4RE_PEX_REQUIRE=1; a skip would look green.
inline void skipOrFailPexCorpus(const char* message) {
    const char* require = std::getenv("FO4RE_PEX_REQUIRE");
    if (require == nullptr || std::string_view(require) != "1") GTEST_SKIP() << message;
    FAIL() << message;
}

// Only an unset or empty value may fall back; anything else must parse as a positive integer.
inline std::size_t corpusFloor(const char* variable, std::size_t fallback) {
    const char* raw = std::getenv(variable);
    if (raw == nullptr || *raw == '\0') return fallback;
    const std::string_view text(raw);
    long long value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0) {
        ADD_FAILURE() << variable << " must be a positive integer, not '" << text << "'";
        return fallback;
    }
    return static_cast<std::size_t>(value);
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

struct StratifiedSample {
    std::size_t stride{1};
    std::vector<std::filesystem::path> files;
};

inline std::string sampleSortKey(const std::filesystem::path& file,
                                 const std::vector<std::filesystem::path>& roots) {
    const auto normal = file.lexically_normal();
    auto relative = normal;
    for (const auto& root : roots) {
        const auto candidate = normal.lexically_relative(root.lexically_normal());
        if (!candidate.empty() && candidate.begin()->string() != "..") {
            relative = candidate;
            break;
        }
    }
    auto key = relative.generic_string();
    for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

inline StratifiedSample stratifiedSample(const std::vector<std::filesystem::path>& files,
                                         const std::vector<std::filesystem::path>& roots,
                                         std::size_t limit) {
    std::vector<std::pair<std::string, std::filesystem::path>> keyed;
    keyed.reserve(files.size());
    for (const auto& file : files) keyed.emplace_back(sampleSortKey(file, roots), file);
    std::sort(keyed.begin(), keyed.end());

    const auto total = keyed.size();
    StratifiedSample sample;
    if (limit != 0 && total > limit) sample.stride = (total + limit - 1) / limit;
    for (std::size_t i = 0; i < total; i += sample.stride) {
        sample.files.push_back(keyed[i].second);
    }
    return sample;
}

}  // namespace fo4::papyrus::test
