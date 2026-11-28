#include <fo4ide/platform/PopoutRoute.hpp>

#include <cctype>

namespace fo4ide::platform {
namespace {

constexpr std::string_view PopoutMarker = "#/popout/";

bool routeCharacter(char value) {
    return std::isalnum(static_cast<unsigned char>(value)) || value == '-';
}

}

std::optional<std::string> popoutRouteFor(std::string_view uri, std::string_view appUrl) {
    const auto hash = appUrl.find('#');
    const auto base = hash == std::string_view::npos ? appUrl : appUrl.substr(0, hash);
    if (base.empty() || !uri.starts_with(base)) return std::nullopt;

    const auto rest = uri.substr(base.size());
    if (!rest.starts_with(PopoutMarker)) return std::nullopt;

    const auto route = rest.substr(PopoutMarker.size());
    std::size_t length = 0;
    while (length < route.size() && routeCharacter(route[length])) ++length;
    if (length == 0) return std::nullopt;
    if (length < route.size() && route[length] != '?') return std::nullopt;
    return std::string(route.substr(0, length));
}

}
