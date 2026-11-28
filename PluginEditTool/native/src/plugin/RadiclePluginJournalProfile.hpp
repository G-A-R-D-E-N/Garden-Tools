#pragma once

#include "RadiclePluginJournalSession.hpp"
#include <map>
#include <nlohmann/json_fwd.hpp>

namespace fo4ide::plugin::detail {
void prepareJournalProfile(const nlohmann::json& arguments, const std::filesystem::path& dataDirectory,
                           const std::shared_ptr<const Ba2ReadRoot>& dataRoot,
                           const std::map<std::filesystem::path, std::shared_ptr<const Ba2ReadRoot>>& profileRoots,
                           const JournalManifest& manifest, const std::vector<std::byte>& finalPlugin,
                           JournalReadConfiguration& config);
}
