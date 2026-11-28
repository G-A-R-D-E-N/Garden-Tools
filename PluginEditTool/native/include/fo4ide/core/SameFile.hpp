#pragma once

#include <filesystem>
#include <system_error>

namespace fo4ide::core {

inline bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && !ec) {
        std::error_code equivalentError;
        if (std::filesystem::equivalent(a, b, equivalentError) && !equivalentError) return true;
    }
    const auto canonical = [](const std::filesystem::path& path, std::error_code& error) {
        const auto absolute = std::filesystem::absolute(path, error);
        return error ? absolute : std::filesystem::weakly_canonical(absolute, error);
    };
    std::error_code errorA;
    std::error_code errorB;
    const auto canonicalA = canonical(a, errorA);
    const auto canonicalB = canonical(b, errorB);
    return !errorA && !errorB && canonicalA == canonicalB;
}

}
