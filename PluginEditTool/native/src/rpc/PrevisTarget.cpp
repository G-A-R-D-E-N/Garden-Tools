#include <fo4ide/rpc/PrevisTarget.hpp>

#include <fo4/environment/environment.hpp>
#include <fo4/previs/detect.hpp>
#include <fo4/previs/previs_data.hpp>
#include <fo4ide/mo2/Mo2Profile.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4ide::rpc {
namespace {

constexpr std::size_t kMaxReportedCells = 5000;

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
    if (!settings.mo2InstancePath.empty()) {
        if (auto profile = mo2::readProfile(settings.mo2InstancePath, {}, data_dir)) {
            if (!profile->assetRootsLowToHigh.empty()) return profile->assetRootsLowToHigh;
        }
    }
    if (data_dir.empty()) return {};
    return {data_dir};
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
    if (method == "PatchBake") return callBackend("BakeOcNif", args);
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
        {{"id", "bake"},
         {"title", "Bake _OC.NIF"},
         {"detail", "Pass-through measured _OC.NIF with SharedGeom (#34 encoder from placements still open)."},
         {"status", "partial"}},
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
