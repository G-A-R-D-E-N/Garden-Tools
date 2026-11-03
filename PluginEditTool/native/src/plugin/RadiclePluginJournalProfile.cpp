#include "RadiclePluginJournalProfile.hpp"
#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginSupport.hpp"

#include <fo4/io/path_text.hpp>
#include <fo4/plugin/reader.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace fo4ide::plugin::detail {
void prepareJournalProfile(const nlohmann::json& arguments, const std::filesystem::path& dataDirectory,
                           const std::shared_ptr<const Ba2ReadRoot>& dataRoot,
                           const std::map<std::filesystem::path, std::shared_ptr<const Ba2ReadRoot>>& profileRoots,
                           const JournalManifest& manifest, const std::vector<std::byte>& finalPlugin,
                           JournalReadConfiguration& config) {
    if (manifest.version < 2) return;
    if (!dataDirectory.empty()) {
        if (!dataRoot || dataRoot->path() != std::filesystem::absolute(dataDirectory).lexically_normal())
            throw JournalRecoveryReadRootChanged("Data read root is unavailable or changed");
        try { static_cast<void>(dataRoot->list({}, 4096)); }
        catch (const std::exception&) { throw JournalRecoveryReadRootChanged("Data read root changed"); }
    }
    try { static_cast<void>(fo4::read_plugin(finalPlugin, manifest.plugin)); }
    catch (const std::exception&) { throw JournalRecoveryProfileInvalid("invalid journal plugin: " + fo4::io::utf8(manifest.plugin.filename())); }
    std::vector<std::string> names;
    std::vector<JournalPeerInput> peers;
    std::shared_ptr<const Ba2ReadRoot> modRoot;
    std::string ownName;
    for (const auto& entry : arguments.at("plugins")) {
        if (!entry.is_object() || !entry.value("resolved", false)) continue;
        const auto name = entry.value("name", std::string{}), pathText = entry.value("path", std::string{});
        if (name.empty() || pathText.empty()) continue;
        std::filesystem::path path;
        try { path = std::filesystem::absolute(fo4::io::from_utf8(pathText)).lexically_normal(); }
        catch (const std::exception&) { throw JournalRecoveryProfileInvalid("invalid profile plugin path: " + name); }
        if (support::asciiLower(fo4::io::utf8(path.filename())) != support::asciiLower(name))
            throw JournalRecoveryProfileInvalid("invalid profile plugin identity: " + name);
        names.push_back(name);
        if (names.size() > 4097) throw JournalRecoveryProfileInvalid("too many profile plugins");
        const auto existing = profileRoots.find(path.parent_path());
        if (existing == profileRoots.end() || !existing->second) throw JournalRecoveryProfileInvalid("unavailable profile plugin: " + name);
        auto root = existing->second;
        if (sameJournalIdentity(path, manifest.plugin)) {
            if (modRoot) throw JournalRecoveryProfileInvalid("duplicate profile plugin: " + name);
            modRoot = std::move(root); ownName = name;
        } else {
            peers.push_back({std::move(root), path.filename()});
        }
    }
    if (!modRoot) throw JournalRecoveryProfileInvalid("journal plugin is absent from profile");
    config.peers = snapshotJournalPeers(peers, 512ull * 1024 * 1024, 4096);
    config.approvedReadRoots.clear(); config.archiveDirectories.clear();
    if (dataRoot) {
        config.approvedReadRoots.push_back({dataRoot->path(), dataRoot});
        config.archiveDirectories.push_back({dataRoot->path(), {}});
        if (manifest.version == 3) config.readStringDirectories.push_back({dataRoot->path(), {"Strings"}, {}});
    }
    if (!dataRoot || !dataRoot->sameRoot(*modRoot)) {
        config.approvedReadRoots.push_back({modRoot->path(), modRoot});
        config.archiveDirectories.push_back({modRoot->path(), {}});
    }
    config.archiveProviderOrder = journalArchiveOrder(dataRoot, modRoot, names, ownName);
}
}
