#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace fo4ide::plugin {

class RadiclePluginBackend final : public IPluginBackend {
public:
    Capabilities capabilities() const noexcept override;
    Result<nlohmann::json> invoke(std::string_view operation,
                                  const nlohmann::json& arguments) override;
    Result<nlohmann::json> invokeBackend(std::string_view method,
                                          const nlohmann::json& arguments) override;
    Result<nlohmann::json> setFieldValue(const nlohmann::json& arguments) override;

    // Registers the game's Data directory. Any plugin whose file resolves
    // beneath it is treated as read-only regardless of its reported MO2
    // source. Call once during setup, before the backend serves requests.
    void setDataDirectory(std::filesystem::path dataDirectory);
private:
    struct LoadedPlugin {
        std::string name;
        std::filesystem::path path;
        std::string source;
        std::filesystem::file_time_type loadedTime{};
        // True only for plugins that arrived through the trusted
        // environment.LoadMo2 snapshot. Plugins loaded ad hoc via
        // backend.OpenPlugin are never trusted: their directories do not
        // become Save-As roots.
        bool trusted{false};
        fo4::Plugin plugin;
    };

    Result<nlohmann::json> loadEnvironment(const nlohmann::json& arguments);
    Result<nlohmann::json> getPlugins() const;
    Result<nlohmann::json> getChildren(const nlohmann::json& arguments) const;
    Result<nlohmann::json> openRecord(const nlohmann::json& arguments) const;
    Result<nlohmann::json> openPlugin(const nlohmann::json& arguments);
    Result<nlohmann::json> savePlugin(const nlohmann::json& arguments);
    Result<nlohmann::json> createRecord(const nlohmann::json& arguments);
    Result<nlohmann::json> deleteRecord(const nlohmann::json& arguments);
    Result<nlohmann::json> renumberRecord(const nlohmann::json& arguments);
    Result<nlohmann::json> setField(const nlohmann::json& arguments);
    Result<nlohmann::json> setArmorField(const nlohmann::json& arguments);
    Result<nlohmann::json> setFormLinkListField(const nlohmann::json& arguments);
    Result<nlohmann::json> insertFormLinkListElement(const nlohmann::json& arguments);
    Result<nlohmann::json> removeFormLinkListElement(const nlohmann::json& arguments);
    Result<nlohmann::json> copyAsOverride(const nlohmann::json& arguments);
    Result<nlohmann::json> copyAsOverrideMany(const nlohmann::json& arguments);
    Result<nlohmann::json> addMasters(const nlohmann::json& arguments);
    Result<nlohmann::json> checkEslEligibility(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getEditablePlugins() const;
    Result<nlohmann::json> getActivePlugins() const;
    Result<nlohmann::json> getRecordTypeIndex() const;
    Result<nlohmann::json> getPluginRecordTypes(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getRecordsOfType(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getRecordsGrid(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getRecordDetails(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getRecordPluginMatrix(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getContainmentPath(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getLoadOrderSummary() const;
    Result<nlohmann::json> getConflicts() const;
    Result<nlohmann::json> getConflictMatrix(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getReferencedBy(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getDependencies(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getProblems(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getHistory(const nlohmann::json& arguments) const;
    Result<nlohmann::json> searchRecords(const nlohmann::json& arguments) const;
    Result<nlohmann::json> scanConflicts() const;
    Result<nlohmann::json> scanBrokenRefs() const;
    Result<nlohmann::json> listMasters(const nlohmann::json& arguments) const;
    Result<nlohmann::json> reorderMasters(const nlohmann::json& arguments);
    Result<nlohmann::json> setLight(const nlohmann::json& arguments);
    Result<nlohmann::json> saveMastersPlugin(const nlohmann::json& arguments);

    LoadedPlugin* findPlugin(std::string_view name);
    const LoadedPlugin* findPlugin(std::string_view name) const;
    bool isEditable(const LoadedPlugin& plugin) const;

    // True when the plugin's file on disk still carries the modification time
    // observed when it was loaded into the cache. Callers must refuse to save
    // when this returns false so external edits are not clobbered.
    bool fileUnchangedSinceLoad(const LoadedPlugin& plugin) const;
    // Returns an error when `plugin` must not be written: read-only source,
    // or modified on disk since it was cached. Caller holds mutex_.
    std::optional<Error> writeGuard(const LoadedPlugin& plugin) const;
    // Returns the canonical target when `path` resolves inside an authorized
    // Save-As root, otherwise nullopt. The writer must use this exact path so
    // its destination is the one that was containment-checked.
    std::optional<std::filesystem::path> allowedSavePath(
        const std::filesystem::path& path) const;
    void rebuildGraphLocked();

    mutable std::mutex mutex_;
    std::vector<LoadedPlugin> plugins_;
    fo4::ConflictGraph graph_;
    // Written once during setup via setDataDirectory, read-only afterwards.
    std::filesystem::path dataDirectory_;
    // Immutable snapshot of the writable Save-As roots, captured from the
    // environment.LoadMo2 plugin list only. Never derived from mutable
    // LoadedPlugin.path: an ad hoc OpenPlugin must not be able to retarget
    // a trusted entry's path and thereby relocate a save root.
    std::vector<std::filesystem::path> trustedSaveRoots_;
};

}
