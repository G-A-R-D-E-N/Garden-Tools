#include <fo4ide/app/ServiceTargets.hpp>
#include <fo4ide/assets/TextureResolver.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <fo4/archive/asset_scan.hpp>
#include <fo4/archive/assets.hpp>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {

using namespace support;

namespace {

constexpr int k_default_missing_rows = 200;
constexpr int k_max_missing_rows = 5000;
constexpr std::size_t k_records_per_row = 5;

bool isRoot(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& directory) {
    return std::any_of(roots.begin(), roots.end(), [&](const std::filesystem::path& root) {
        std::error_code ec;
        return std::filesystem::equivalent(root, directory, ec);
    });
}

}

Result<nlohmann::json> NifTarget::auditAssetUsage(const nlohmann::json& args) {
    const auto pluginName = stringArgument(args, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto limit = static_cast<std::size_t>(
        std::clamp(intNumberArgument(args, 1, k_default_missing_rows), 1, k_max_missing_rows));

    auto collected = plugins_.invokeBackend("CollectAssetReferences", nlohmann::json::array({pluginName}));
    if (!collected) return std::unexpected(collected.error());

    try {
        const auto base = textures_.assetResolver();
        const auto folderText = collected->at("pluginDirectory").get<std::string>();
        const std::filesystem::path pluginDirectory(std::u8string(folderText.begin(), folderText.end()));
        std::shared_ptr<const fo4::archive::AssetResolver> resolver = base;
        std::vector<fo4::archive::ArchiveFailure> failures;
        if (!pluginDirectory.empty() && !isRoot(base->roots(), pluginDirectory)) {
            auto roots = base->roots();
            roots.push_back(pluginDirectory);
            std::vector<std::filesystem::path> archives;
            for (const auto& archive : base->archives()) archives.push_back(archive.path());
            const std::vector<std::string> own{collected->at("plugin").get<std::string>()};
            for (auto& archive : fo4::archive::archive_load_order(pluginDirectory, {}, own)) {
                archives.push_back(std::move(archive));
            }
            auto opened = fo4::archive::AssetResolver::open_roots(roots, archives, fo4::archive::ArchiveFailurePolicy::skip);
            failures = std::move(opened.failures);
            resolver = std::make_shared<const fo4::archive::AssetResolver>(std::move(opened.resolver));
        }

        std::size_t resolved = 0;
        std::size_t missing = 0;
        std::size_t unusable = 0;
        nlohmann::json missingRows = nlohmann::json::array();
        nlohmann::json unusableRows = nlohmann::json::array();
        for (const auto& asset : collected->at("assets")) {
            const auto path = asset.at("path").get<std::string>();
            std::vector<std::string> candidates;
            try {
                candidates = fo4::archive::asset_path_candidates(path);
            } catch (const fo4::archive::AssetPathError&) {
                ++unusable;
                if (unusableRows.size() < limit) unusableRows.push_back(path);
                continue;
            }
            const bool found = std::any_of(candidates.begin(), candidates.end(), [&](const std::string& candidate) {
                return resolver->locate(candidate).has_value();
            });
            if (found) {
                ++resolved;
                continue;
            }
            ++missing;
            if (missingRows.size() >= limit) continue;
            const auto& records = asset.at("records");
            nlohmann::json shown = nlohmann::json::array();
            for (std::size_t index = 0; index < records.size() && index < k_records_per_row; ++index) {
                shown.push_back(records[index]);
            }
            missingRows.push_back({{"path", candidates.back()}, {"records", std::move(shown)},
                                   {"recordCount", records.size()}});
        }

        nlohmann::json report{{"plugin", collected->at("plugin")},
                              {"nativeRecords", collected->at("nativeRecords")},
                              {"referenced", collected->at("assets").size()},
                              {"resolved", resolved},
                              {"missing", missing},
                              {"unusable", unusable},
                              {"truncated", missing > missingRows.size() || unusable > unusableRows.size()},
                              {"missingAssets", std::move(missingRows)},
                              {"unusablePaths", std::move(unusableRows)},
                              {"skippedArchives", failures.size()}};
        if (base->roots().empty() && base->archives().empty()) {
            report["note"] = "No data roots are known; only the plugin's own folder was searched, so assets served by "
                             "the game or other mods are listed as missing. Load a modlist or set a Data folder.";
        }
        return report;
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"AssetAuditFailed", ex.what()});
    }
}

}
