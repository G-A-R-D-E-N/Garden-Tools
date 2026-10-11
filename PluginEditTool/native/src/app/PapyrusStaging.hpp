#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::app {

std::string extractScriptName(const std::string& text);
Result<std::optional<std::filesystem::path>> stageNamespacedScripts(
    const std::vector<std::filesystem::path>& files, const std::filesystem::path& staging);

}
