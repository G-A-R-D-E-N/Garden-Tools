#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace fo4ide::platform {

std::optional<std::string> popoutRouteFor(std::string_view uri, std::string_view appUrl);

}
