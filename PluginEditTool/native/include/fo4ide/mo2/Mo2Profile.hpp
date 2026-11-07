#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::mo2 {

struct InstanceInfo {
    std::filesystem::path instancePath;
    std::string profile{"Default"};
    std::filesystem::path gameDataFolder;
    std::filesystem::path overwriteFolder;
    std::filesystem::path modsFolder;
};

struct ModEntry {
    std::string name;
    std::filesystem::path path;
    std::size_t priority{0};
};

struct PluginEntry {
    std::string name;
    std::filesystem::path path;
    std::string source;
    bool resolved{false};
};

struct ProfileSnapshot {
    InstanceInfo instance;
    std::vector<std::string> loadOrder;
    std::vector<ModEntry> enabledModsHighToLow;
    std::vector<PluginEntry> plugins;
    std::vector<std::string> missingPlugins;
    std::vector<std::string> warnings;
    std::vector<std::filesystem::path> assetRootsLowToHigh;

    std::unordered_map<std::string, std::filesystem::path> pluginPathIndex() const;
};

Result<InstanceInfo> readInstanceInfo(const std::filesystem::path& instancePath);
Result<ProfileSnapshot> readProfile(const std::filesystem::path& instancePath,
                                    std::string profileOverride = {},
                                    std::filesystem::path gameDataOverride = {});
std::filesystem::path resolveOverwriteFolder(const std::filesystem::path& instancePath);
std::filesystem::path nativePath(std::string value);

}
