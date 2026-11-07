#include "RadiclePluginSupport.hpp"

#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fo4ide::plugin::support {

bool sameLocalizedSpelling(const std::filesystem::path& left, const std::filesystem::path& right) {
    if (left == right) return true;
#ifdef _WIN32
    const auto& a = left.native();
    const auto& b = right.native();
    if (a.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        b.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("localized path is too long to compare");
    const auto compared = ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
                                                  b.data(), static_cast<int>(b.size()), TRUE);
    if (compared == 0) throw std::runtime_error("localized paths could not be compared");
    if (compared == CSTR_EQUAL) return true;
#endif
    return false;
}

bool sameLocalizedPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    if (sameLocalizedSpelling(left, right)) return true;
    std::error_code ec;
    const auto equivalent = std::filesystem::equivalent(left, right, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
        throw std::filesystem::filesystem_error("could not compare localized paths", left, right, ec);
    return !ec && equivalent;
}

}
