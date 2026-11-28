#include <fo4ide/assets/AssetPath.hpp>

#include <cctype>
#include <vector>

namespace fo4ide::assets {

namespace {

bool isSeparator(char c) noexcept {
    return c == '/' || c == '\\';
}

bool markerMatchesAt(std::string_view path, std::size_t pos, std::string_view marker) noexcept {
    if (pos + marker.size() > path.size()) return false;
    for (std::size_t i = 0; i < marker.size(); ++i) {
        const char a = path[pos + i];
        const char b = marker[i];
        if (isSeparator(a) && isSeparator(b)) continue;
        if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) return false;
    }
    return true;
}

std::size_t findMarker(std::string_view path, std::string_view marker) noexcept {
    for (std::size_t pos = 0; pos + marker.size() <= path.size(); ++pos) {
        if (pos != 0 && !isSeparator(path[pos - 1])) continue;
        if (markerMatchesAt(path, pos, marker)) return pos;
    }
    return std::string_view::npos;
}

}  // namespace

bool isAbsoluteOnAnyHost(std::string_view path) {
    if (path.empty()) return false;
    if (isSeparator(path[0])) return true;
    return path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) != 0 &&
           path[1] == ':' && isSeparator(path[2]);
}

std::string stripDevAbsolutePrefix(std::string_view path,
                                   std::initializer_list<std::string_view> markers) {
    if (!isAbsoluteOnAnyHost(path)) return std::string(path);
    for (const auto marker : markers) {
        const auto pos = findMarker(path, marker);
        if (pos != std::string_view::npos) return std::string(path.substr(pos));
    }
    return std::string(path);
}

}  // namespace fo4ide::assets