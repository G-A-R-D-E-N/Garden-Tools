#include <fo4ide/app/DebugToolsTarget.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <string_view>
#include <system_error>

#include <fo4/c_api.h>

#include "DebugToolsInternal.hpp"

namespace fo4ide::app {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr std::string_view kAllowedSettings[] = {
    "AiProvider", "Model", "GeminiModel", "ClaudeCodePath", "OllamaModel", "OutputFolder", "Fallout4Path",
    "DataFolder", "Mo2InstancePath", "NewPluginFolder", "PrevisOutputFolder", "CkWikiPath", "PapyrusCompilerPath",
    "PapyrusBaseImports", "FfmpegPath", "XwmaEncodePath", "Archive2Path", "ReadLargePluginsIntoMemory"};

bool looksSecret(const std::string& key) {
    static const std::regex pattern("token|secret|password|cookie|authorization|api[_-]?key", std::regex::icase);
    return std::regex_search(key, pattern);
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string foldPath(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    auto text = lowered(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
    if (text.starts_with("//?/unc/")) {
        text = "//" + text.substr(8);
    } else if (text.starts_with("//?/") || text.starts_with("//./")) {
        text = text.substr(4);
    }
    while (text.size() > 1 && text.back() == '/') text.pop_back();
    return text;
}

bool foldedInside(const std::string& child, const std::string& parent) {
    if (parent.empty()) return false;
    if (child == parent) return true;
    if (child.size() <= parent.size() || child.compare(0, parent.size(), parent) != 0) return false;
    return parent.back() == '/' || child[parent.size()] == '/';
}

fs::path canonicalOrNormal(const fs::path& path) {
    std::error_code ec;
    auto canonical = fs::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

std::uint32_t le32(const std::vector<unsigned char>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::string settingText(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_null()) return {};
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

fs::path pathOf(const json& settings, const char* key) {
    const auto it = settings.find(key);
    return it != settings.end() && it->is_string() ? fs::path(it->get<std::string>()) : fs::path{};
}

std::mutex gBuildMutex;
struct BuildCacheEntry {
    std::uintmax_t size = 0;
    fs::file_time_type mtime{};
    std::string build;
};
std::map<std::string, BuildCacheEntry> gBuildCache;

std::string cachedGameBuild(const fs::path& exe) {
    const auto key = exe.string();
    std::error_code sizeEc;
    std::error_code timeEc;
    const auto size = fs::file_size(exe, sizeEc);
    const auto mtime = fs::last_write_time(exe, timeEc);
    const bool hasStat = !sizeEc && !timeEc;
    if (hasStat) {
        std::lock_guard<std::mutex> guard(gBuildMutex);
        if (const auto it = gBuildCache.find(key);
            it != gBuildCache.end() && it->second.size == size && it->second.mtime == mtime) {
            return it->second.build;
        }
    }
    auto build = gameBuildOf(exe);
    if (!build.empty() && hasStat) {
        std::lock_guard<std::mutex> guard(gBuildMutex);
        gBuildCache[key] = BuildCacheEntry{size, mtime, build};
    }
    return build;
}

json inventoryRow(std::string area, std::string capability, std::string status, std::string toolId, std::string note) {
    return {{"area", std::move(area)}, {"capability", std::move(capability)}, {"status", std::move(status)},
            {"toolId", std::move(toolId)}, {"note", std::move(note)}};
}

}  // namespace

json scrubDebugSettings(const json& settings) {
    json out = json::object();
    if (!settings.is_object()) return out;
    for (const auto key : kAllowedSettings) {
        const std::string name(key);
        if (looksSecret(name)) continue;
        const auto it = settings.find(name);
        if (it == settings.end()) continue;
        out[name] = settingText(*it);
    }
    return out;
}

Result<fs::path> validateScratchParent(const fs::path& outputDir, const std::vector<fs::path>& protectedPaths,
                                       const std::string& stamp) {
    const auto unsafe = [](std::string message) {
        return std::unexpected(Error{"UnsafeOutputDir", std::move(message)});
    };
    if (outputDir.empty()) return unsafe("the output folder is empty");
    if (!outputDir.is_absolute()) return unsafe("the output folder must be an absolute path");
    for (const auto& part : outputDir) {
        if (part == "..") return unsafe("the output folder must not contain '..'");
    }
    if (stamp.empty() || stamp.find_first_of("/\\:") != std::string::npos) {
        return unsafe("the scratch folder name is not usable");
    }
    std::error_code ec;
    if (!fs::is_directory(outputDir, ec)) return unsafe("the output folder must be an existing folder");
    const auto canonical = fs::weakly_canonical(outputDir, ec);
    if (ec) return unsafe("the output folder could not be resolved");
    const auto target = canonical / ("debugtools-" + stamp);
    if (fs::exists(fs::symlink_status(target, ec))) {
        return unsafe("the scratch folder already exists and is never reused: " + target.string());
    }
    const auto foldedTarget = foldPath(target);
    const auto foldedRequested = foldPath(outputDir);
    for (const auto& guarded : protectedPaths) {
        if (guarded.empty()) continue;
        if (foldedInside(foldedTarget, foldPath(canonicalOrNormal(guarded))) ||
            foldedInside(foldedRequested, foldPath(guarded))) {
            return unsafe("the output folder is inside a protected folder: " + guarded.string());
        }
    }
    return target;
}

std::string gameBuildOf(const fs::path& exe) {
    try {
        std::ifstream file(exe, std::ios::binary);
        if (!file) return {};
        file.seekg(0, std::ios::end);
        const auto size = static_cast<std::uint64_t>(file.tellg());
        constexpr std::uint64_t kChunk = 1u << 20;
        constexpr std::uint64_t kOverlap = 16;
        std::uint64_t end = size;
        while (end > 0) {
            const std::uint64_t start = end > kChunk ? end - kChunk : 0;
            const std::uint64_t stop = std::min(size, end + kOverlap);
            std::vector<unsigned char> buffer(static_cast<std::size_t>(stop - start));
            file.clear();
            file.seekg(static_cast<std::streamoff>(start));
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            if (static_cast<std::size_t>(file.gcount()) != buffer.size()) return {};
            if (buffer.size() >= 16) {
                for (std::size_t at = buffer.size() - 16 + 1; at-- > 0;) {
                    if (le32(buffer, at) != 0xFEEF04BDu || le32(buffer, at + 4) != 0x00010000u) continue;
                    const auto ms = le32(buffer, at + 8);
                    const auto ls = le32(buffer, at + 12);
                    return std::to_string(ms >> 16) + "." + std::to_string(ms & 0xFFFFu) + "." +
                           std::to_string(ls >> 16) + "." + std::to_string(ls & 0xFFFFu);
                }
            }
            if (start == 0) break;
            end = start;
        }
    } catch (const std::exception&) {
    }
    return {};
}

std::string readBuildCommit(const fs::path& exeDir) {
    std::ifstream file(exeDir / "build-info.txt");
    std::string line;
    while (std::getline(file, line)) {
        if (!line.starts_with("commit=")) continue;
        auto value = line.substr(7);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        return value.empty() ? "unknown" : value;
    }
    return "unknown";
}

json debugToolsContext(const DebugToolsEnv& env) {
    const auto snapshot = env.snapshot ? env.snapshot() : std::nullopt;
    const auto settings = env.settings ? env.settings() : json::object();
    const auto dataDir = debugtools::dataDirOf(env);

    json loadOrder = json::array();
    json roots = json::array();
    std::string profile;
    if (snapshot) {
        profile = snapshot->instance.profile;
        for (const auto& name : snapshot->loadOrder) loadOrder.push_back(name);
        for (const auto& root : snapshot->assetRootsLowToHigh) roots.push_back(root.string());
    }

    const auto build = debugtools::gameBuildFor(env);

    return {{"versions",
             {{"fo4ide", env.fo4ideVersion},
              {"radicleAbi", std::to_string(radicle_abi_major()) + "." + std::to_string(radicle_abi_minor())},
              {"commit", readBuildCommit(env.exeDir)}}},
            {"dataDir", dataDir.string()},
            {"mo2Profile", profile},
            {"loadOrder", std::move(loadOrder)},
            {"settings", scrubDebugSettings(settings)},
            {"gameBuild", build},
            {"assetRootsLowToHigh", std::move(roots)}};
}

json debugToolsInventory() {
    json rows = json::array();
    const auto add = [&](const char* area, const char* capability, const char* status, const char* toolId,
                         const char* note) { rows.push_back(inventoryRow(area, capability, status, toolId, note)); };

    add("plugin/load order", "Environment, versions and load order", "integrated", "env.context",
        "Cheap context only; no scan.");
    add("plugin/load order", "Structural validation of one plugin", "integrated", "plugin.validate",
        "backend.ValidatePlugin; a parse and rule check, not a game load.");
    add("plugin/load order", "Cross-record link error check", "integrated", "plugin.checkErrors",
        "backend.CheckForErrors.");
    add("plugin/load order", "Write round trip into a scratch folder", "integrated", "plugin.roundtrip",
        "Rebuilds the plugin from its parsed structure; writes only under the chosen scratch folder.");
    add("plugin/load order", "MO2 profile status and staleness", "available", "appInterop.GetMo2Status",
        "AppInteropTarget.cpp getMo2Status; it calls GetPlugins, so it is not run from Context.");
    add("plugin/load order", "Duplicate FormIDs", "available", "backend.GetDuplicateFormIds",
        "PluginBackendTarget.cpp backend namespace.");
    add("plugin/load order", "Circular leveled lists", "available", "backend.CheckCircularLeveledLists",
        "PluginBackendTarget.cpp backend namespace.");
    add("records read/edit", "Record tree and field read", "available", "backend.GetRecordTree",
        "PluginBackendTarget.cpp. Edit paths write plugin state and are excluded on purpose.");
    add("assets/archives", "Asset reference audit of one plugin", "integrated", "assets.audit",
        "nif.AuditAssetUsage.");
    add("assets/archives", "Which provider wins an asset path", "integrated", "env.assetPrecedence",
        "nif.ResolveAsset; FO4IDE ordering, not proven engine order.");
    add("assets/archives", "BA2 listing and comparison", "available", "archive.List",
        "ArchiveTarget.cpp List and Compare.");
    add("NIF", "NIF verify checks on a loose file", "integrated", "nif.verify", "nif.Verify; loose winner only.");
    add("NIF", "NIF parse and write round trip in memory", "integrated", "nif.roundtrip",
        "read_nif then write_nif against the shipped bytes; no disk writes.");
    add("NIF", "NIF inspect, tree and geometry", "available", "nif.Inspect",
        "ServiceTargets.cpp NifTarget Inspect, Tree and Geo.");
    add("NIF", "NIF byte identity over a folder or the whole corpus", "integrated", "nif.corpusRoundtrip",
        "read_nif then write_nif per winning NIF, cancellable; default is an even sample of 200 reported as unverified, limit all covers every winner. The NifCorpus gtest also walks shadowed archive entries.");
    add("material", "Material file inspect", "available", "material.Inspect", "MaterialTarget.cpp Inspect.");
    add("DDS texture", "DDS header and format diagnostics", "integrated", "dds.inspect",
        "read_dds, payload size, mip count and a first-mip decode in memory (Radicle texture/dds.hpp).");
    add("cells/worldspaces", "Cell and worldspace search", "available", "cell.SearchCells",
        "PluginBackendTarget.cpp cell namespace (SearchCells, SearchWorldspaces); CellTarget forwarding not traced.");
    add("navmesh", "Navmesh info, vertices, triangles, links", "available", "cell.GetNavmeshInfo",
        "PluginBackendTarget.cpp cell namespace; CellTarget forwarding not traced.");
    add("quests/dialogue", "Quest consistency check", "available", "backend.CheckQuest",
        "PluginBackendTarget.cpp backend namespace.");
    add("scripts/Papyrus PEX", "Papyrus source check and analysis", "available", "papyrus.CheckText",
        "PapyrusTarget.cpp CheckText and Analyze.");
    add("scripts/Papyrus PEX", "Compiled PEX inspection", "integrated", "pex.inspect",
        "PexFile::fromBytes and toBytes byte identity plus an object summary; no disassembly.");
    add("strings", "String table file diagnostics", "integrated", "strings.inspect",
        "Strict read_string_table plus an in-memory rewrite that must read back the same.");
    add("strings", "Plugin string tables found for a loaded plugin", "integrated", "strings.pluginTables",
        "strings.ListStringTables (RadiclePluginStrings.cpp); language en only.");
    add("HKX", "HKX summary, objects and skeleton", "available", "hkx.Summary",
        "ServiceTargets.cpp HkxTarget Summary, InspectObjects and Skeleton.");
    add("audio", "Audio decode diagnostics", "integrated", "audio.inspect",
        "read_sound (Radicle audio/sound_pcm.hpp) decodes WAV, XWM and FUZ in memory; the audio target itself only converts and writes.");
    add("SWF", "SWF open, state, characters and timeline", "available", "swf.Open", "SwfTarget.cpp.");
    add("LOD", "World list", "integrated", "lod.worlds", "lod.Worlds.");
    add("LOD", "Rebake plan", "integrated", "lod.plan", "lod.Plan; read only.");
    add("LOD", "Dry-run generate for one world", "integrated", "lod.dryRunGenerate",
        "lod.GenerateScoped with apply false.");
    add("LOD", "Verify one world's tiles", "integrated", "lod.verifyWorld", "lod.VerifyWorld.");
    add("LOD", "In-game acceptance of LOD tiles", "gap", "",
        "Only the game proves this (docs/lodgen-plan.md Gates); every LOD tool says unverified.");
    add("previs", "Scan for broken and outdated cells", "integrated", "previs.scan", "previs.Scan.");
    add("previs", "Precombine plan for one cell", "integrated", "previs.plan", "previs.PatchPlan.");
    add("previs", "Dry-run generate for one cell", "integrated", "previs.dryRunGenerate",
        "previs.Generate with apply false.");
    add("previs", "In-game previs and RVIS visibility", "gap", "",
        "Radicle/docs/PREVIS.md says not proven; every previs tool says unverified.");
    add("previs", "Comparison with the pinned vanilla broken-cell list", "integrated", "previs.vanillaBaseline",
        "Runs the PrevisReal.VanillaLoadOrderGate diagnosis (six official masters, Data folder only) and diffs it with the pinned list; never above unverified unless the game build matches the list's build.");
    add("layout/graph", "Layout overlap check and kit description", "available", "layout.CheckOverlaps",
        "LayoutTarget.cpp CheckOverlaps and DescribeKit.");
    add("layout/graph", "Graph validation", "available", "graph.ValidateGraph", "GraphTarget.cpp.");
    add("records read/edit", "Text export and re-import on a real plugin", "integrated", "plugin.textRoundtrip",
        "export_plugin_text, import_plugin_text and write_plugin against the canonical rebuild; scratch folder only.");
    add("records read/edit", "Row edit and write round trip on real data", "integrated", "plugin.rowRoundtrip",
        "Port of row_roundtrip_harness.hpp roundtrip_family for scalar rows of one record signature; scratch folder only. Link rows, link lists and editor ID length changes are not covered.");
    return rows;
}

namespace debugtools {

std::string dumpJson(const json& value) {
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

fs::path dataDirOf(const DebugToolsEnv& env) {
    const auto settings = env.settings ? env.settings() : json::object();
    if (auto path = pathOf(settings, "DataFolder"); !path.empty()) return path;
    if (env.snapshot) {
        if (const auto snapshot = env.snapshot(); snapshot && !snapshot->instance.gameDataFolder.empty()) {
            return snapshot->instance.gameDataFolder;
        }
    }
    if (auto path = pathOf(settings, "Fallout4Path"); !path.empty()) return path / "Data";
    if (env.gameRoot) {
        if (auto root = env.gameRoot(); !root.empty()) return root / "Data";
    }
    return {};
}

std::string gameBuildFor(const DebugToolsEnv& env) {
    const auto dataDir = dataDirOf(env);
    std::vector<fs::path> exes;
    if (env.gameRoot) exes.push_back(env.gameRoot() / "Fallout4.exe");
    if (!dataDir.empty()) exes.push_back(dataDir.parent_path() / "Fallout4.exe");
    for (const auto& exe : exes) {
        if (exe.parent_path().empty()) continue;
        if (auto build = cachedGameBuild(exe); !build.empty()) return build;
    }
    return {};
}

std::string gameBuildBesideData(const DebugToolsEnv& env) {
    const auto dataDir = dataDirOf(env);
    if (dataDir.empty() || dataDir.parent_path().empty()) return {};
    return cachedGameBuild(dataDir.parent_path() / "Fallout4.exe");
}

bool isInside(const fs::path& child, const fs::path& parent) {
    return foldedInside(foldPath(canonicalOrNormal(child)), foldPath(canonicalOrNormal(parent)));
}

fs::path resolvePluginPath(const DebugToolsEnv& env, const std::string& name) {
    if (name.empty()) return {};
    std::error_code ec;
    const fs::path asPath(name);
    if (asPath.is_absolute()) return fs::is_regular_file(asPath, ec) ? asPath : fs::path{};
    const auto wanted = lowered(name);
    if (env.snapshot) {
        if (const auto snapshot = env.snapshot()) {
            for (const auto& plugin : snapshot->plugins) {
                if (plugin.resolved && lowered(plugin.name) == wanted) return plugin.path;
            }
        }
    }
    if (const auto dataDir = dataDirOf(env); !dataDir.empty()) {
        if (fs::is_regular_file(dataDir / name, ec)) return dataDir / name;
    }
    return {};
}

std::vector<fs::path> protectedPaths(const DebugToolsEnv& env, const fs::path& sourcePlugin) {
    std::vector<fs::path> paths;
    const auto settings = env.settings ? env.settings() : json::object();
    paths.push_back(dataDirOf(env));
    if (env.gameRoot) paths.push_back(env.gameRoot());
    for (const char* key : {"Fallout4Path", "DataFolder", "PrevisOutputFolder", "NewPluginFolder", "Mo2InstancePath"}) {
        paths.push_back(pathOf(settings, key));
    }
    if (env.snapshot) {
        if (const auto snapshot = env.snapshot()) {
            paths.push_back(snapshot->instance.instancePath);
            paths.push_back(snapshot->instance.gameDataFolder);
            paths.push_back(snapshot->instance.modsFolder);
            paths.push_back(snapshot->instance.overwriteFolder);
            for (const auto& root : snapshot->assetRootsLowToHigh) paths.push_back(root);
        }
    }
    if (!sourcePlugin.empty()) paths.push_back(sourcePlugin.parent_path());
    return paths;
}

}  // namespace debugtools
}  // namespace fo4ide::app
