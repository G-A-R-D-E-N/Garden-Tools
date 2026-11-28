#include <fo4ide/rpc/PrevisTarget.hpp>

#include <fo4/environment/environment.hpp>
#include <fo4/formid.hpp>
#include <fo4/previs/detect.hpp>
#include <fo4/previs/previs_data.hpp>
#include <fo4ide/mo2/Mo2Profile.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::rpc {
namespace {

constexpr std::size_t kMaxReportedCells = 5000;
constexpr std::size_t kMaxReportedOcTemplates = 64;
constexpr std::string_view kOcNifSuffix = "_oc.nif";

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::string cellHexPrefix(std::uint32_t local24) {
    char buffer[9]{};
    std::snprintf(buffer, sizeof(buffer), "%08x", local24 & 0xFFFFFFu);
    return buffer;
}

bool isOcNifFileName(std::string_view file_name) {
    if (file_name.size() != 24 || file_name[8] != '_') return false;
    return file_name.ends_with(kOcNifSuffix);
}

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

nlohmann::json issueArray(const std::vector<fo4::previs::CellIssue>& issues) {
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& issue : issues) {
        rows.push_back({
            {"code", std::string(fo4::previs::to_string(issue.code))},
            {"detail", issue.detail},
        });
    }
    return rows;
}

nlohmann::json cellJson(const fo4::previs::CellDiagnosis& diagnosis,
                        const std::string& plugin) {
    nlohmann::json row = {
        {"cell", diagnosis.cell.to_string()},
        {"plugin", plugin},
        {"issues", issueArray(diagnosis.status.issues)},
        {"outdated", issueArray(diagnosis.status.outdated_issues)},
        {"skips", diagnosis.status.skips},
    };
    return row;
}

struct LoadOrder {
    std::vector<std::filesystem::path> paths;
    std::vector<std::string> pending;
};

Result<LoadOrder> loadOrderFromJson(const nlohmann::json& value) {
    nlohmann::json rows = value;
    if (rows.is_string()) {
        try {
            rows = nlohmann::json::parse(rows.get<std::string>());
        } catch (const std::exception& error) {
            return std::unexpected(Error{"PrevisEnvironment",
                                         std::string("plugin list is not JSON: ") + error.what()});
        }
    }
    if (!rows.is_array()) {
        return std::unexpected(Error{"PrevisEnvironment", "plugin list is not an array"});
    }
    LoadOrder order;
    order.paths.reserve(rows.size());
    for (const auto& row : rows) {
        if (!row.is_object()) continue;
        const auto path = row.value("FilePath", std::string{});
        if (path.empty()) continue;
        order.paths.emplace_back(path);
        if (row.value("Pending", false)) {
            order.pending.push_back(std::filesystem::path(path).filename().string());
        }
    }
    if (order.paths.empty()) {
        return std::unexpected(Error{"PrevisNoEnvironment",
                                     "no plugin paths are loaded; load an MO2 profile first"});
    }
    return order;
}

}

std::vector<std::filesystem::path> previsAssetRoots(const settings::Settings& settings,
                                                    const std::filesystem::path& data_dir) {
    std::vector<std::filesystem::path> roots;
    if (!settings.mo2InstancePath.empty()) {
        if (auto profile = mo2::readProfile(settings.mo2InstancePath, {}, data_dir)) {
            roots = profile->assetRootsLowToHigh;
        }
    }
    if (roots.empty() && !data_dir.empty()) roots.push_back(data_dir);
    // Constraint: the generate output root is the highest-priority root, so a scan sees what a run just wrote.
    if (!settings.previsOutputFolder.empty() &&
        std::find(roots.begin(), roots.end(), settings.previsOutputFolder) == roots.end()) {
        roots.push_back(settings.previsOutputFolder);
    }
    return roots;
}

PrevisTarget::PrevisTarget(plugin::IPluginBackend& backend,
                           settings::SharedSettings& settings)
    : backend_(backend), settings_(settings) {}

Result<nlohmann::json> PrevisTarget::invoke(std::string_view method,
                                            const nlohmann::json& args) {
    if (method == "Scan") return scan(args);
    if (method == "GetDataFolder") return dataFolder();
    if (method == "PatchWorkflow") return patchWorkflow();
    if (method == "PatchStamp") return callBackend("StampCellPrevis", args);
    if (method == "PatchDisable") return callBackend("DisableCellPrevis", args);
    if (method == "PatchPlan") return callBackend("PrecombinePlan", args);
    if (method == "Generate") return callBackend("GenerateCellPrevis", args);
    if (method == "PatchBake") return callBackend("BakeOcNif", args);
    if (method == "ResolveOcTemplate") return resolveOcTemplate(args);
    if (method == "PatchWriteContainer") return callBackend("WritePrevisContainer", args);
    if (method == "PatchSave") return callBackend("SavePlugin", args);
    return std::unexpected(Error{"UnknownMethod",
                                 "Unknown previs RPC method: " + std::string(method)});
}

Result<nlohmann::json> PrevisTarget::callBackend(std::string_view method, const nlohmann::json& args) {
    const auto response = backend_.invokeBackend(method, args);
    if (!response) return std::unexpected(response.error());
    return *response;
}

Result<nlohmann::json> PrevisTarget::patchWorkflow() const {
    return nlohmann::json::array({
        {{"id", "scan"}, {"title", "Scan"}, {"detail", "Find broken or stale cells in the saved load order."},
         {"status", "done"}},
        {{"id", "pick"}, {"title", "Pick cells"}, {"detail", "Choose rows from broken/outdated tables."},
         {"status", "done"}},
        {{"id", "disable"},
         {"title", "Disable stale previs"},
         {"detail", "Clear PCMB/VISI/XCRI on superseded exterior cells (native backend)."},
         {"status", "done"}},
        {{"id", "plan"},
         {"title", "Plan precombine"},
         {"detail", "Native PrecombinePlan for interior and exterior STAT groups (minInstances default 2)."},
         {"status", "done"}},
        {{"id", "bake"},
         {"title", "Bake _OC.NIF"},
         {"detail", "Bake Shared extra from plan placements (resize compact/table 1-combined blocks when slot counts differ); the mesh drawn is still the template's, since Shared geometry lives in the CSG."},
         {"status", "partial"}},
        {{"id", "generate"},
         {"title", "Generate previs"},
         {"detail", "GenerateCellPrevis: plan by material, write the patch's own <plugin> - Geometry.csg (the _OC template may be omitted: the cell's own comes from Data), bake one _OC per planned model that addresses it, write <cell8>_Physics.NIF, pair XCRI, stamp PCMB/VISI, CELL override, then merge the patch's <plugin>.cdx row copied from the first loaded index that has it, the winning plugin's first (save to persist). Writes the cell's own Vis\\<patch>\\<cell8>.uvd tome from that geometry, patches it into <Data>\\Vis\\<patch>\\<cell8>.uvd under the existing name's casing, after the native validator accepts it, and a failed run puts every written file back; copy_vanilla_tome=true opts into copying a shipped cluster tome instead and reports a warning."},
         {"status", "partial"}},
        {{"id", "containers-owned"},
         {"title", "Geometry and tome"},
         {"detail", "Emits the combined geometry stream <plugin> - Geometry.csg beside the patch and the per-cell <cell8>_Physics.NIF. and the cell's own cluster tome at Vis\\<patch>\\<cell8>.uvd built from that geometry."},
         {"status", "manual"}},
        {{"id", "verify"}, {"title", "Verify scan"}, {"detail", "Re-scan after saving patch plugin and assets on disk."},
         {"status", "done"}},
        {{"id", "stamp"}, {"title", "Stamp patch CELL"}, {"detail", "PCMB, VISI, XCRI (+ RVIS on exteriors)."},
         {"status", "done"}},
        {{"id", "containers"},
         {"title", "CDX / CSG / UVD"},
         {"detail", "WritePrevisContainer / write_previs_container round-trip rewrite (native writers)."},
         {"status", "done"}},
        {{"id", "save"}, {"title", "Save + archive"}, {"detail", "save_plugin then pack Main/Vis BA2."},
         {"status", "done"}},
    });
}

std::filesystem::path PrevisTarget::resolveDataFolder(const nlohmann::json& args) const {
    const auto explicitFolder = stringArgument(args, 0);
    if (!explicitFolder.empty()) return std::filesystem::path(explicitFolder);
    const auto fromBackend = backend_.dataDirectory();
    if (!fromBackend.empty()) return fromBackend;
    const auto settings = settings_.snapshot();
    if (!settings.dataFolder.empty()) return settings.dataFolder;
    if (!settings.fallout4Path.empty()) return settings.fallout4Path / "Data";
    return {};
}

Result<nlohmann::json> PrevisTarget::dataFolder() const {
    const auto folder = resolveDataFolder(nlohmann::json::array());
    return nlohmann::json{{"dataDir", folder.string()}};
}

Result<nlohmann::json> PrevisTarget::resolveOcTemplate(const nlohmann::json& args) {
    const auto cellText = stringArgument(args, 0);
    if (cellText.empty()) {
        return std::unexpected(Error{"ResolveOcTemplateArgs", "cell form key is required"});
    }
    fo4::FormKey cell;
    try {
        cell = fo4::FormKey::parse(cellText);
    } catch (const std::exception& problem) {
        return std::unexpected(
            Error{"ResolveOcTemplateArgs", std::string("could not parse cell: ") + problem.what()});
    }
    const auto cellPrefix = cellHexPrefix(cell.local().value());
    const auto sourcePlugin = lowered(stringArgument(args, 1));
    const auto patchPlugin = stringArgument(args, 2);

    const auto folder = resolveDataFolder(nlohmann::json::array());
    if (folder.empty()) {
        return std::unexpected(Error{"PrevisDataDir",
                                     "no Data folder is configured; set one in Settings"});
    }
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        return std::unexpected(Error{"PrevisDataDir",
                                     "not a readable Data folder: " + folder.string()});
    }

    const auto roots = previsAssetRoots(settings_.snapshot(), folder);
    nlohmann::json templates = nlohmann::json::array();
    std::string suggestedSource;
    std::uint32_t suggestedId{0};
    for (const auto& root : roots) {
        const auto precombined = root / "Meshes" / "PreCombined";
        if (!std::filesystem::is_directory(precombined, error)) continue;
        for (std::filesystem::recursive_directory_iterator it(precombined, error), end;
             !error && it != end;
             it.increment(error)) {
            std::error_code file_error;
            if (!it->is_regular_file(file_error)) continue;
            const auto file_name = lowered(it->path().filename().string());
            if (!isOcNifFileName(file_name)) continue;
            if (file_name.substr(0, 8) != cellPrefix) continue;
            const auto relative = it->path().lexically_relative(precombined);
            const auto relative_text = relative.generic_string();
            std::string plugin_folder;
            const auto slash = relative_text.find_first_of("/\\");
            if (slash != std::string::npos) {
                plugin_folder = relative_text.substr(0, slash);
            }
            if (!sourcePlugin.empty()) {
                const auto owner = lowered(std::string(cell.owner().filename()));
                const auto folder_plugin = plugin_folder.empty() ? owner : lowered(plugin_folder);
                if (folder_plugin != sourcePlugin && owner != sourcePlugin) continue;
            }
            const auto idText = file_name.substr(9, 8);
            char* id_end = nullptr;
            const auto idValue = static_cast<std::uint32_t>(std::strtoul(idText.data(), &id_end, 16));
            if (templates.size() < kMaxReportedOcTemplates) {
                templates.push_back({
                    {"path", it->path().string()},
                    {"relative", relative_text},
                    {"plugin", plugin_folder},
                    {"ocId", idText},
                });
            }
            if (suggestedSource.empty()) {
                suggestedSource = it->path().string();
                suggestedId = idValue;
            }
        }
    }

    std::string suggestedOutput;
    if (!patchPlugin.empty() && suggestedId != 0) {
        char idBuffer[9]{};
        std::snprintf(idBuffer, sizeof(idBuffer), "%08X", suggestedId);
        // Constraint: a non-base plugin's combined meshes sit under a folder named after the plugin file.
        const auto pluginFolder = std::filesystem::path(patchPlugin).filename().string();
        // Constraint: the suggested name must be the uppercase form generateCellPrevis writes, while the prefix stays lowercase for the compare above.
        std::string upperPrefix = cellPrefix;
        for (auto& letter : upperPrefix) letter = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
        suggestedOutput = (folder / "Meshes" / "PreCombined" / pluginFolder /
                           (upperPrefix + '_' + idBuffer + "_OC.NIF"))
                              .string();
    }

    // Constraint: read the count before the list is moved into the result, or truncated is always false.
    const bool truncated = templates.size() >= kMaxReportedOcTemplates;
    return nlohmann::json{
        {"cell", cell.to_string()},
        {"cellPrefix", cellPrefix},
        {"dataDir", folder.string()},
        {"templates", std::move(templates)},
        {"suggestedSourceNif", suggestedSource},
        {"suggestedOutputOc", suggestedOutput},
        {"truncated", truncated},
    };
}

Result<nlohmann::json> PrevisTarget::scan(const nlohmann::json& args) {
    bool expected = false;
    if (!scanning_.compare_exchange_strong(expected, true)) {
        return std::unexpected(
            Error{"PrevisScanBusy", "a previs scan is already running; wait for it to finish"});
    }
    struct ScanRelease {
        std::atomic<bool>& flag;
        ~ScanRelease() { flag.store(false); }
    } release{scanning_};

    const auto folder = resolveDataFolder(args);
    if (folder.empty()) {
        return std::unexpected(Error{"PrevisDataDir",
                                     "no Data folder is configured; set one in Settings"});
    }
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        return std::unexpected(Error{"PrevisDataDir",
                                     "not a readable Data folder: " + folder.string()});
    }

    auto plugins = backend_.invoke("appInterop.GetPlugins", nlohmann::json::array());
    if (!plugins) return std::unexpected(plugins.error());
    auto loadOrder = loadOrderFromJson(*plugins);
    if (!loadOrder) return std::unexpected(loadOrder.error());

    fo4::env::Environment environment;
    try {
        environment = fo4::env::Environment::load(loadOrder->paths);
    } catch (const std::exception& problem) {
        return std::unexpected(Error{"PrevisEnvironment",
                                     std::string("could not load the plugin list: ") + problem.what()});
    }

    const auto roots = previsAssetRoots(settings_.snapshot(), folder);
    const auto data = fo4::previs::PrevisData::load(roots, environment);
    const auto report = fo4::previs::diagnose_load_order(environment, data.indexes());

    std::size_t broken = 0;
    std::size_t outdated = 0;
    std::size_t skipped = 0;
    nlohmann::json brokenRows = nlohmann::json::array();
    nlohmann::json outdatedRows = nlohmann::json::array();
    nlohmann::json skippedRows = nlohmann::json::array();
    for (const auto& diagnosis : report) {
        const std::string plugin(environment.plugins()[diagnosis.plugin].key.filename());
        if (diagnosis.status.skip_unsupported) {
            ++skipped;
            if (skippedRows.size() < kMaxReportedCells) {
                skippedRows.push_back(cellJson(diagnosis, plugin));
            }
        }
        if (diagnosis.status.outdated) {
            ++outdated;
            if (outdatedRows.size() < kMaxReportedCells) {
                outdatedRows.push_back(cellJson(diagnosis, plugin));
            }
        }
        if (diagnosis.status.broken) {
            ++broken;
            if (brokenRows.size() < kMaxReportedCells) {
                brokenRows.push_back(cellJson(diagnosis, plugin));
            }
        }
    }

    const auto& summary = data.summary();
    return nlohmann::json{
        {"dataDir", folder.string()},
        {"pendingPlugins", loadOrder->pending},
        {"summary",
         {
             {"cells", report.size()},
             {"broken", broken},
             {"outdated", outdated},
             {"skipped", skipped},
             {"cdxFiles", summary.cdx_files},
             {"cdxRows", summary.cdx_rows},
             {"csgFiles", summary.csg_files},
             {"csgRows", summary.csg_rows},
             {"csgChunks", summary.csg_chunks},
             {"tomeFiles", summary.tome_files},
             {"ocNifFiles", summary.oc_nif_files},
             {"problems", summary.problems.size()},
         }},
        {"problems", summary.problems},
        {"broken", std::move(brokenRows)},
        {"outdated", std::move(outdatedRows)},
        {"skipped", std::move(skippedRows)},
        {"truncated", broken > kMaxReportedCells || outdated > kMaxReportedCells ||
                          skipped > kMaxReportedCells},
    };
}

}
