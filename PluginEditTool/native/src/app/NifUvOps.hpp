#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::app {

Result<std::string> nifUvOp(std::string_view op, const nlohmann::json& body);

}
