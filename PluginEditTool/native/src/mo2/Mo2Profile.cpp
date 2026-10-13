#include <fo4ide/mo2/Mo2Profile.hpp>

#include <fo4/loadorder/load_order_files.hpp>
#include <fo4/plugin_key.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4ide::mo2 {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

void replaceAll(std::string& value, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    std::size_t offset = 0;
    while ((offset = value.find(from, offset)) != std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
}

std::string unwrapValue(std::string value) {
    value = trim(std::move(value));
    static constexpr std::string_view prefix = "@ByteArray(";
    if (value.size() > prefix.size() &&
        lowerAscii(value.substr(0, prefix.size())) == lowerAscii(std::string(prefix)) &&
        value.back() == ')') {
        value = value.substr(prefix.size(), value.size() - prefix.size() - 1);
    }
    replaceAll(value, "\\\\", "\\");
    return value;
}

std::unordered_map<std::string, std::string> readIni(const std::filesystem::path& path) {
    std::unordered_map<std::string, std::string> values;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        auto key = lowerAscii(trim(line.substr(0, split)));
        auto value = unwrapValue(line.substr(split + 1));
        values.insert_or_assign(std::move(key), std::move(value));
    }
    return values;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(trim(std::move(line)));
    return lines;
}

bool pluginExtension(const std::filesystem::path& path) {
    const auto ext = lowerAscii(path.extension().string());
    return ext == ".esp" || ext == ".esm" || ext == ".esl";
}

struct ResolvedPlugin {
    std::filesystem::path path;
    std::string source;
};

void indexFolder(const std::filesystem::path& folder,
                 std::string source,
                 std::unordered_map<std::string, ResolvedPlugin>& resolved,
                 std::vector<std::filesystem::path>& candidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !pluginExtension(it->path())) continue;
        const auto name = it->path().filename().string();
        resolved.insert_or_assign(lowerAscii(name), ResolvedPlugin{it->path(), source});
        candidates.push_back(it->path());
    }
}

}

std::filesystem::path nativePath(std::string value) {
    value = trim(std::move(value));
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
    const auto values = readIni(instancePath / "ModOrganizer.ini");
    std::string baseDir;
    std::string overwriteDir;
    if (const auto it = values.find("base_directory"); it != values.end()) baseDir = it->second;
    if (const auto it = values.find("overwrite_directory"); it != values.end()) overwriteDir = it->second;

    if (baseDir.empty()) baseDir = instancePath.string();
    replaceAll(baseDir, "%BASE_DIR%", instancePath.string());
    auto basePath = nativePath(baseDir);

    std::string overwrite = overwriteDir.empty() ? (basePath / "overwrite").string() : overwriteDir;
    replaceAll(overwrite, "%BASE_DIR%", basePath.string());
    auto result = nativePath(overwrite);
    if (result.is_relative()) result = basePath / result;
    std::error_code ec;
    auto absolute = std::filesystem::absolute(result, ec);
    return ec ? result.lexically_normal() : absolute.lexically_normal();
}

Result<InstanceInfo> readInstanceInfo(const std::filesystem::path& instancePath) {
    std::error_code ec;
    if (!std::filesystem::is_directory(instancePath, ec)) {
        return std::unexpected(Error{"Mo2InstanceNotFound", "MO2 instance folder not found: " + instancePath.string()});
    }

    InstanceInfo info;
    info.instancePath = std::filesystem::absolute(instancePath, ec);
    if (ec) info.instancePath = instancePath;
    info.modsFolder = info.instancePath / "mods";
    info.overwriteFolder = resolveOverwriteFolder(info.instancePath);

    const auto values = readIni(info.instancePath / "ModOrganizer.ini");
    if (const auto it = values.find("selected_profile"); it != values.end() && !it->second.empty()) {
        info.profile = it->second;
    }

    if (const auto it = values.find("gamepath"); it != values.end() && !it->second.empty()) {
        info.gameDataFolder = nativePath(it->second) / "Data";
    }

    if (!std::filesystem::is_directory(info.gameDataFolder, ec)) {
        const auto portable = info.instancePath / "Stock Folder" / "Data";
        if (std::filesystem::is_directory(portable, ec)) info.gameDataFolder = portable;
    }

    return info;
}

Result<ProfileSnapshot> readProfile(const std::filesystem::path& instancePath,
                                    std::string profileOverride,
                                    std::filesystem::path gameDataOverride) {
    auto infoResult = readInstanceInfo(instancePath);
    if (!infoResult) return std::unexpected(infoResult.error());
    auto info = std::move(*infoResult);
    if (!profileOverride.empty()) info.profile = std::move(profileOverride);
    if (!gameDataOverride.empty()) info.gameDataFolder = std::move(gameDataOverride);

    const auto profileDir = info.instancePath / "profiles" / info.profile;
    const auto pluginsPath = profileDir / "plugins.txt";
    const auto modlistPath = profileDir / "modlist.txt";
    if (!std::filesystem::is_regular_file(pluginsPath)) {
        return std::unexpected(Error{"Mo2PluginsMissing", "plugins.txt not found for profile '" + info.profile + "'"});
    }

    ProfileSnapshot snapshot;
    snapshot.instance = info;

    if (std::filesystem::is_regular_file(modlistPath)) {
        std::size_t priority = 0;
        for (const auto& line : readLines(modlistPath)) {
            if (line.size() < 2 || line.front() != '+') continue;
            auto name = trim(line.substr(1));
            if (name.empty()) continue;
            const auto lower = lowerAscii(name);
            if (lower.size() >= 10 && lower.ends_with("_separator")) continue;
            snapshot.enabledModsHighToLow.push_back({name, info.modsFolder / name, priority++});
        }
    }

    std::unordered_map<std::string, ResolvedPlugin> resolved;
    std::vector<std::filesystem::path> candidates;
    indexFolder(info.gameDataFolder, "Data", resolved, candidates);
    snapshot.assetRootsLowToHigh.push_back(info.gameDataFolder);

    for (auto it = snapshot.enabledModsHighToLow.rbegin(); it != snapshot.enabledModsHighToLow.rend(); ++it) {
        indexFolder(it->path, it->name, resolved, candidates);
        snapshot.assetRootsLowToHigh.push_back(it->path);
    }

    indexFolder(info.overwriteFolder, "overwrite", resolved, candidates);
    snapshot.assetRootsLowToHigh.push_back(info.overwriteFolder);

    fo4::loadorder::PluginList pluginList;
    std::vector<std::string> cccNames;
    std::vector<std::filesystem::path> winners;
    fo4::loadorder::AssembledLoadOrder assembled;
    try {
        pluginList = fo4::loadorder::read_plugin_list(pluginsPath);
        if (!info.gameDataFolder.empty()) {
            const auto gameRoot = info.gameDataFolder.has_filename() ? info.gameDataFolder.parent_path()
                                                                     : info.gameDataFolder.parent_path().parent_path();
            const auto cccPath = gameRoot / "Fallout4.ccc";
            if (std::filesystem::is_regular_file(cccPath)) cccNames = fo4::loadorder::read_ccc(cccPath);
        }
        for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
            if (resolved.at(lowerAscii(it->filename().string())).path == *it) winners.push_back(*it);
        }
        assembled = fo4::loadorder::assemble_load_order(winners, pluginList, cccNames, fo4::loadorder::OfficialTable::ae);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"Mo2LoadOrderInvalid", error.what()});
    }
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
        if (entry.checked && lowerAscii(entry.name) != "fallout4.esm") snapshot.loadOrder.push_back(entry.name);
    }
    if (!resolved.contains("fallout4.esm")) reportMissing("Fallout4.esm");
    for (const auto& entry : pluginList.entries) {
        if (!entry.active) continue;
        const auto isChecked = std::any_of(assembled.entries.begin(), assembled.entries.end(), [&](const auto& item) {
            return item.checked && fo4::PluginKey::from_filename(item.name) == fo4::PluginKey::from_filename(entry.name);
        });
        if (!isChecked) reportMissing(entry.name);
    }

    snapshot.plugins.reserve(snapshot.loadOrder.size());
    for (const auto& name : snapshot.loadOrder) {
        const auto found = resolved.find(lowerAscii(name));
        if (found == resolved.end()) {
            snapshot.plugins.push_back({name, {}, {}, false});
        } else {
            snapshot.plugins.push_back({name, found->second.path, found->second.source, true});
        }
    }

    return snapshot;
}

std::unordered_map<std::string, std::filesystem::path> ProfileSnapshot::pluginPathIndex() const {
    std::unordered_map<std::string, std::filesystem::path> result;
    for (const auto& plugin : plugins) {
        if (plugin.resolved) result.insert_or_assign(lowerAscii(plugin.name), plugin.path);
    }
    return result;
}

}
