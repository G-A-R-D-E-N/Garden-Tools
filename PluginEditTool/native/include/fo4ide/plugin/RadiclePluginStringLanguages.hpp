#pragma once

#include <filesystem>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include <fo4/archive/assets.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace fo4ide::plugin {

Result<std::set<std::string>> discoverPluginStringLanguages(
    std::string_view pluginFilename,
    std::span<const std::filesystem::path> stringsDirectories,
    const fo4::archive::AssetResolver* assets,
    std::span<const std::string_view> stagedLanguages);

}
