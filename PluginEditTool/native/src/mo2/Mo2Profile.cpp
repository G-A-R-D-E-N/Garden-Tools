#include <fo4ide/mo2/Mo2Profile.hpp>

#include <fo4/io/path_text.hpp>
#include <fo4/loadorder/load_order_files.hpp>
#include <fo4/loadorder/mo2_profile.hpp>
#include <fo4/plugin_key.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4ide::mo2 {
namespace {

fo4::loadorder::Mo2Instance readInstance(const std::filesystem::path& instancePath) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(instancePath, ec);
    return fo4::loadorder::read_mo2_instance(ec ? instancePath : absolute, [](std::string value) {
        return nativePath(std::move(value));
    });
}

Result<fo4::loadorder::Mo2Instance> loadInstance(const std::filesystem::path& instancePath) {
    std::error_code ec;
    if (!std::filesystem::is_directory(instancePath, ec)) {
        return std::unexpected(Error{"Mo2InstanceNotFound", "MO2 instance folder not found: " + instancePath.string()});
    }
    return readInstance(instancePath);
}

InstanceInfo toInfo(const fo4::loadorder::Mo2Instance& instance) {
    InstanceInfo info;
    info.instancePath = instance.instance_dir;
    info.profile = instance.selected_profile;
    info.gameDataFolder = instance.data_dir;
    info.overwriteFolder = instance.overwrite_dir;
    info.modsFolder = instance.mods_dir;
    return info;
}

std::filesystem::path folderKey(const std::filesystem::path& folder) {
    auto key = folder.lexically_normal();
    if (key.filename().empty()) key = key.parent_path();
    return key;
}

std::string sourceOf(const std::filesystem::path& file,
                     const fo4::loadorder::Mo2Instance& instance,
                     const std::vector<ModEntry>& modsHighToLow) {
    const auto folder = folderKey(file.parent_path());
    if (folder == folderKey(instance.overwrite_dir)) return "overwrite";
    for (const auto& mod : modsHighToLow) {
        if (folder == folderKey(mod.path)) return mod.name;
    }
    return "Data";
}

}

std::filesystem::path nativePath(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (value.empty()) return {};
#if defined(_WIN32)
    return std::filesystem::path(value);
#else
    if (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':') {
        const char drive = static_cast<char>(std::tolower(static_cast<unsigned char>(value[0])));
        auto rest = value.substr(2);
        std::replace(rest.begin(), rest.end(), '\\', '/');
        while (!rest.empty() && rest.front() == '/') rest.erase(rest.begin());
        if (drive == 'z') return std::filesystem::path("/") / rest;
        const char* winePrefix = std::getenv("WINEPREFIX");
        std::filesystem::path prefix;
        if (winePrefix && *winePrefix) {
            prefix = winePrefix;
        } else if (const char* home = std::getenv("HOME"); home && *home) {
            prefix = std::filesystem::path(home) / ".wine";
        } else {
            prefix = ".wine";
        }
        return prefix / ("drive_" + std::string(1, drive)) / rest;
    }
    std::replace(value.begin(), value.end(), '\\', '/');
    return std::filesystem::path(value);
#endif
}

std::filesystem::path resolveOverwriteFolder(const std::filesystem::path& instancePath) {
    return readInstance(instancePath).overwrite_dir;
}

Result<InstanceInfo> readInstanceInfo(const std::filesystem::path& instancePath) {
    auto loaded = loadInstance(instancePath);
    if (!loaded) return std::unexpected(loaded.error());
    return toInfo(*loaded);
}

Result<ProfileSnapshot> readProfile(const std::filesystem::path& instancePath,
                                    std::string profileOverride,
                                    std::filesystem::path gameDataOverride) {
    auto loaded = loadInstance(instancePath);
    if (!loaded) return std::unexpected(loaded.error());
    auto instance = std::move(*loaded);
    if (!profileOverride.empty()) instance.selected_profile = std::move(profileOverride);
    if (!gameDataOverride.empty()) instance.data_dir = std::move(gameDataOverride);

    const auto pluginsPath = instance.profiles_dir / fo4::io::from_utf8(instance.selected_profile) / "plugins.txt";
    if (!std::filesystem::is_regular_file(pluginsPath)) {
        return std::unexpected(Error{"Mo2PluginsMissing", "plugins.txt not found for profile '" + instance.selected_profile + "'"});
    }

    ProfileSnapshot snapshot;
    snapshot.instance = toInfo(instance);

    fo4::loadorder::Mo2Profile profile;
    fo4::loadorder::AssembledLoadOrder assembled;
    std::map<std::string, std::filesystem::path> resolved;
    try {
        profile = fo4::loadorder::read_mo2_profile(instance, instance.selected_profile);
        resolved = fo4::loadorder::mo2_resolve_plugin_paths(instance, profile);
        std::vector<std::string> cccNames;
        if (!instance.data_dir.empty()) {
            const auto gameRoot = instance.data_dir.has_filename() ? instance.data_dir.parent_path()
                                                                   : instance.data_dir.parent_path().parent_path();
            const auto cccPath = gameRoot / "Fallout4.ccc";
            if (std::filesystem::is_regular_file(cccPath)) cccNames = fo4::loadorder::read_ccc(cccPath);
        }
        std::vector<std::filesystem::path> winners;
        for (const auto& [name, file] : resolved) winners.push_back(file);
        assembled = fo4::loadorder::assemble_load_order(winners, profile.plugins, cccNames, fo4::loadorder::OfficialTable::ae);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"Mo2LoadOrderInvalid", error.what()});
    }

    std::size_t priority = 0;
    for (const auto& mod : profile.mods_high_to_low) {
        if (!mod.enabled || mod.is_separator) continue;
        snapshot.enabledModsHighToLow.push_back({mod.name, instance.mods_dir / fo4::io::from_utf8(mod.name), priority++});
    }
    snapshot.assetRootsLowToHigh = fo4::loadorder::mo2_asset_roots_low_to_high(instance, profile);

    if (assembled.failed) {
        std::string message;
        for (const auto& finding : assembled.findings) {
            if (finding.code != "missing_master" && finding.code != "master_too_new" &&
                finding.code != "hoist_cycle") {
                continue;
            }
            if (!message.empty()) message += "; ";
            message += finding.plugin + ": " + finding.detail;
        }
        return std::unexpected(Error{"Mo2LoadOrderInvalid", message});
    }
    for (const auto& finding : assembled.findings) {
        snapshot.warnings.push_back(finding.plugin.empty() ? finding.detail : finding.plugin + ": " + finding.detail);
    }

    const auto reportMissing = [&](const std::string& name) {
        if (std::find(snapshot.missingPlugins.begin(), snapshot.missingPlugins.end(), name) ==
            snapshot.missingPlugins.end()) {
            snapshot.missingPlugins.push_back(name);
        }
    };

    snapshot.loadOrder.push_back("Fallout4.esm");
    for (const auto& entry : assembled.entries) {
        if (entry.checked && fo4::text::fold(entry.name) != fo4::text::fold("Fallout4.esm")) {
            snapshot.loadOrder.push_back(entry.name);
        }
    }
    if (!resolved.contains(fo4::text::fold("Fallout4.esm"))) reportMissing("Fallout4.esm");
    for (const auto& entry : profile.plugins.entries) {
        if (!entry.active) continue;
        const auto isChecked = std::any_of(assembled.entries.begin(), assembled.entries.end(), [&](const auto& item) {
            return item.checked && fo4::PluginKey::from_filename(item.name) == fo4::PluginKey::from_filename(entry.name);
        });
        if (!isChecked) reportMissing(entry.name);
    }

    snapshot.plugins.reserve(snapshot.loadOrder.size());
    for (const auto& name : snapshot.loadOrder) {
        const auto found = resolved.find(fo4::text::fold(name));
        if (found == resolved.end()) {
            snapshot.plugins.push_back({name, {}, {}, false});
        } else {
            snapshot.plugins.push_back(
                {name, found->second, sourceOf(found->second, instance, snapshot.enabledModsHighToLow), true});
        }
    }

    return snapshot;
}

std::unordered_map<std::string, std::filesystem::path> ProfileSnapshot::pluginPathIndex() const {
    std::unordered_map<std::string, std::filesystem::path> result;
    for (const auto& plugin : plugins) {
        if (plugin.resolved) result.insert_or_assign(fo4::text::fold(plugin.name), plugin.path);
    }
    return result;
}

}
