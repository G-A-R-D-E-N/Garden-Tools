#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::app {

Result<std::optional<std::filesystem::path>> stageNamespacedScripts(
    const std::vector<std::filesystem::path>& files, const std::filesystem::path& staging);

}
