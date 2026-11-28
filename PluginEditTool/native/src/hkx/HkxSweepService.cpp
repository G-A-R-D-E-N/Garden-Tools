#include <fo4ide/hkx/HkxService.hpp>

#include <fo4/hkx/animation_sources.hpp>
#include <fo4/hkx/modlist_scan.hpp>
#include <fo4/hkx/subgraph_sweep.hpp>
#include <fo4/hkx/weapon_subgraph.hpp>

#include <cstdint>
#include <exception>
#include <system_error>
#include <string>
#include <utility>
#include <vector>

namespace fo4ide::hkx {
namespace {

Result<fo4::hkx::AnimationSourceRequest> sourceRequest(const std::filesystem::path& dataFolder,
                                                       const std::filesystem::path& mo2InstanceDir,
                                                       std::string_view mo2Profile) {
    if (mo2InstanceDir.empty()) {
        if (!mo2Profile.empty()) return std::unexpected(Error{"InvalidMo2", "mo2_profile needs mo2_instance_dir"});
        std::error_code ec;
        if (!std::filesystem::is_directory(dataFolder, ec)) {
            return std::unexpected(Error{"DataFolderNotFound", "data folder not found: " + dataFolder.string()});
        }
    }
    fo4::hkx::AnimationSourceRequest request;
    request.data_dir = dataFolder;
    request.mo2_instance = mo2InstanceDir;
    request.profile = std::string(mo2Profile);
    return request;
}

Error sourceError(const fo4::hkx::AnimationSourceError& error) { return Error{"InvalidSources", error.what()}; }

Error failedError(const std::exception& error) { return Error{"SweepFailed", error.what()}; }

// Invariant: ids leave as decimal strings, because a JSON number loses the low bits of a 64-bit id.
std::string idText(std::uint64_t id) { return std::to_string(id); }

nlohmann::json gapsJson(const std::vector<fo4::hkx::WeaponGap>& gaps) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& gap : gaps) {
        out.push_back({{"leaf", gap.leaf}, {"message", gap.message}, {"crash", gap.crash}});
    }
    return out;
}

nlohmann::json idsJson(const std::vector<std::uint64_t>& ids) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto id : ids) out.push_back(idText(id));
    return out;
}

}  // namespace

Result<nlohmann::json> HkxService::subgraphSweep(const std::filesystem::path& dataFolder,
                                                 const std::filesystem::path& mo2InstanceDir,
                                                 std::string_view mo2Profile) const {
    const auto request = sourceRequest(dataFolder, mo2InstanceDir, mo2Profile);
    if (!request) return std::unexpected(request.error());
    try {
        const auto sweep = fo4::hkx::run_subgraph_sweep(*request, {});
        nlohmann::json failures = nlohmann::json::array();
        for (const auto& failure : sweep.failures) {
            failures.push_back({{"id", idText(failure.id)},
                                {"behavior", failure.behavior},
                                {"gap_total", failure.gap_total},
                                {"crash_gaps", failure.crash_gaps},
                                {"gaps", gapsJson(failure.gaps)}});
        }
        return nlohmann::json{
            {"ok", sweep.ok()},
            {"complete", sweep.complete()},
            {"manifests", sweep.manifests},
            {"duplicate_ids", sweep.duplicate_ids},
            {"archives", sweep.archives},
            {"roots", sweep.roots},
            {"weapon_checked", sweep.weapon_checked},
            {"behaviors_read", sweep.behaviors_read},
            {"behaviors_missing", sweep.behaviors_missing},
            {"behaviors_unreadable", sweep.behaviors_unreadable},
            {"behaviors_skipped", sweep.behaviors_skipped},
            {"manifests_unread", sweep.manifests_unread},
            {"unchecked", sweep.unchecked},
            {"failures_total", sweep.failures_total},
            {"failures", std::move(failures)},
            {"truncated_manifests", sweep.truncated_manifests},
            {"truncated_behaviors", sweep.truncated_behaviors},
            {"truncated_failures", sweep.truncated_failures},
            {"incomplete", sweep.incomplete},
            {"weapon_sets", sweep.weapon_sets},
            {"weapon_sets_reason", sweep.weapon_sets_reason},
            {"notes", sweep.notes},
        };
    } catch (const fo4::hkx::AnimationSourceError& error) {
        return std::unexpected(sourceError(error));
    } catch (const std::exception& error) {
        return std::unexpected(failedError(error));
    }
}

Result<nlohmann::json> HkxService::subgraphDiff(const std::filesystem::path& dataFolder,
                                                const std::filesystem::path& mo2InstanceDir,
                                                std::string_view mo2Profile) const {
    const auto request = sourceRequest(dataFolder, mo2InstanceDir, mo2Profile);
    if (!request) return std::unexpected(request.error());
    try {
        const auto diff = fo4::hkx::run_subgraph_diff(*request, {});
        nlohmann::json clusters = nlohmann::json::array();
        for (const auto& cluster : diff.clusters) clusters.push_back({{"behavior", cluster.first}, {"new_manifests", cluster.second}});
        return nlohmann::json{
            {"complete", diff.complete()},
            {"vanilla_manifests", diff.vanilla_manifests},
            {"modded_manifests", diff.modded_manifests},
            {"new_ids_total", diff.new_ids_total},
            {"gone_ids_total", diff.gone_ids_total},
            {"new_behaviors_total", diff.new_behaviors_total},
            {"clusters_total", diff.clusters_total},
            {"new_weapon_total", diff.new_weapon_total},
            {"weapon_unread", diff.weapon_unread},
            {"weapon_missing", diff.weapon_missing},
            {"weapon_unreadable", diff.weapon_unreadable},
            {"weapon_skipped", diff.weapon_skipped},
            {"manifests_unread", diff.manifests_unread},
            {"new_ids", idsJson(diff.new_ids)},
            {"gone_ids", idsJson(diff.gone_ids)},
            {"new_behaviors", diff.new_behaviors},
            {"clusters", std::move(clusters)},
            {"new_weapon_behaviors", diff.new_weapon_behaviors},
            {"truncated_manifests", diff.truncated_manifests},
            {"truncated_lists", diff.truncated_lists},
            {"truncated_weapon_reads", diff.truncated_weapon_reads},
            {"incomplete", diff.incomplete},
            {"notes", diff.notes},
        };
    } catch (const fo4::hkx::AnimationSourceError& error) {
        return std::unexpected(sourceError(error));
    } catch (const std::exception& error) {
        return std::unexpected(failedError(error));
    }
}

Result<nlohmann::json> HkxService::crashSubgraph(const std::filesystem::path& logPath, std::string_view text,
                                                 const std::filesystem::path& dataFolder,
                                                 const std::filesystem::path& mo2InstanceDir,
                                                 std::string_view mo2Profile) const {
    const auto request = sourceRequest(dataFolder, mo2InstanceDir, mo2Profile);
    if (!request) return std::unexpected(request.error());
    try {
        fo4::hkx::CrashInput input;
        input.log = logPath;
        input.text = std::string(text);
        const auto crash = fo4::hkx::analyze_crash_subgraph(*request, input, {});
        return nlohmann::json{
            {"refusal", crash.refusal},
            {"id_found", crash.id_found},
            {"id", crash.id_found ? nlohmann::json(idText(crash.id)) : nlohmann::json(nullptr)},
            {"matched", crash.matched},
            {"confirmed", crash.confirmed},
            {"manifest_found", crash.manifest_found},
            {"manifest", crash.manifest},
            {"source", crash.source},
            {"behavior_paths", crash.behavior_paths},
            {"animation_total", crash.animation_total},
            {"animation_present", crash.animation_present},
            {"missing_total", crash.missing_total},
            {"missing", crash.missing},
            {"offsets_found", crash.offsets_found},
            {"offsets_source", crash.offsets_source},
            {"offsets_bytes", crash.offsets_bytes},
            {"offsets_hint", crash.offsets_hint},
            {"behaviors_checked", crash.behaviors_checked},
            {"unchecked_behaviors", crash.unchecked_behaviors},
            {"incomplete", crash.incomplete},
            {"weapon_subgraph", crash.weapon_subgraph},
            {"gap_total", crash.gap_total},
            {"gaps", gapsJson(crash.gaps)},
            {"manifests", crash.manifests},
            {"notes", crash.notes},
        };
    } catch (const fo4::hkx::AnimationSourceError& error) {
        return std::unexpected(sourceError(error));
    } catch (const std::exception& error) {
        return std::unexpected(failedError(error));
    }
}

Result<nlohmann::json> HkxService::modlistScan(std::string_view mode, const std::filesystem::path& dataFolder,
                                               const std::filesystem::path& mo2InstanceDir,
                                               std::string_view mo2Profile) const {
    fo4::hkx::ModlistScanMode scanMode = fo4::hkx::ModlistScanMode::characters;
    if (mode == "clips") {
        scanMode = fo4::hkx::ModlistScanMode::clips;
    } else if (mode != "characters") {
        return std::unexpected(Error{"InvalidMode", "mode must be characters or clips"});
    }
    const auto request = sourceRequest(dataFolder, mo2InstanceDir, mo2Profile);
    if (!request) return std::unexpected(request.error());
    try {
        const auto scan = fo4::hkx::run_modlist_scan(*request, scanMode, {});
        nlohmann::json findings = nlohmann::json::array();
        for (const auto& finding : scan.findings) {
            findings.push_back({{"kind", finding.kind},
                                {"path", finding.path},
                                {"source", finding.source},
                                {"details", finding.details},
                                {"detail_total", finding.detail_total}});
        }
        return nlohmann::json{
            {"mode", std::string(mode)},
            {"ok", scan.ok()},
            {"complete", scan.complete()},
            {"mod_roots", scan.mod_roots},
            {"candidates", scan.candidates},
            {"scanned", scan.scanned},
            {"checked", scan.checked},
            {"broken", scan.broken},
            {"missing", scan.missing},
            {"unreadable", scan.unreadable},
            {"round_trip_losses", scan.round_trip_losses},
            {"findings_total", scan.findings_total},
            {"findings", std::move(findings)},
            {"truncated_files", scan.truncated_files},
            {"truncated_findings", scan.truncated_findings},
            {"truncated_animations", scan.truncated_animations},
            {"incomplete", scan.incomplete},
            {"notes", scan.notes},
        };
    } catch (const fo4::hkx::AnimationSourceError& error) {
        return std::unexpected(sourceError(error));
    } catch (const std::exception& error) {
        return std::unexpected(failedError(error));
    }
}

}  // namespace fo4ide::hkx
