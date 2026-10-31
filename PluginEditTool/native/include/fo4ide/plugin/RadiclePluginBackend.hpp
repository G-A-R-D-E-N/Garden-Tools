#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <fo4/archive/assets.hpp>
#include <fo4/environment/strings.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace fo4ide::plugin {

#ifdef FO4IDE_TEST_HOOKS
using LocalizedQuestBeforeCommitHook = void (*)();
void setLocalizedQuestBeforeCommitHook(LocalizedQuestBeforeCommitHook hook);
using LocalizedQuestAfterTableParseHook = void (*)();
void setLocalizedQuestAfterTableParseHook(LocalizedQuestAfterTableParseHook hook);
using LoadMo2AfterPendingCheckHook = void (*)();
void setLoadMo2AfterPendingCheckHook(LoadMo2AfterPendingCheckHook hook);
using WriteOpLockedHook = void (*)();
void setWriteOpLockedHook(WriteOpLockedHook hook);
void setSaveIoLockedHook(WriteOpLockedHook hook);
using LoadReadWindowHook = void (*)();
void setLoadReadWindowHook(LoadReadWindowHook hook);
#endif

Result<void> requirePluginUnchangedSince(const std::filesystem::path& path,
                                         std::filesystem::file_time_type loadedTime);

class RadiclePluginBackend final : public IPluginBackend {
public:
    Capabilities capabilities() const noexcept override;
    Result<nlohmann::json> invoke(std::string_view operation,
                                  const nlohmann::json& arguments) override;
    Result<nlohmann::json> invokeBackend(std::string_view method,
                                          const nlohmann::json& arguments) override;
    Result<nlohmann::json> invokeLegacyBackend(
        std::string_view method,
        const nlohmann::json& arguments) override;
    Result<void> setTrustedNewPluginRoot(const std::filesystem::path& root) override;
    Result<nlohmann::json> createPlugin(const nlohmann::json& arguments) override;
    Result<void> discardCreatedPlugin(std::string_view name) override;
    Result<nlohmann::json> setFieldValue(const nlohmann::json& arguments) override;
    Result<nlohmann::json> cleanUnusedMasters(const nlohmann::json& arguments) override;
    Result<nlohmann::json> updateOnam(const nlohmann::json& arguments) override;
    Result<nlohmann::json> copyAsDisabledOverride(const nlohmann::json& arguments) override;
    Result<nlohmann::json> copyAsWrapper(const nlohmann::json& arguments) override;
    Result<nlohmann::json> compactToEsl(const nlohmann::json& arguments) override;
    Result<nlohmann::json> renumberPluginFormIds(const nlohmann::json& arguments) override;
    Result<nlohmann::json> injectFormsIntoMaster(const nlohmann::json& arguments) override;
    Result<nlohmann::json> deepCopyAsOverride(const nlohmann::json& arguments) override;
    Result<nlohmann::json> changeReferencingRecords(const nlohmann::json& arguments) override;
    Result<nlohmann::json> revertOverrides(const nlohmann::json& arguments) override;
    Result<nlohmann::json> createMergedPatch(const nlohmann::json& arguments) override;
    Result<nlohmann::json> checkCircularLeveledLists(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> createSeqFile(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> comparePluginTo(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> checkForErrors(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> findUnreachable(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> lootCleaningReport(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> getPlacedReferences(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> getPlacedReferencesAtGrid(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> setPlacedReferenceTransform(const nlohmann::json& arguments) override;
    Result<nlohmann::json> createPlacedObject(const nlohmann::json& arguments) override;
    Result<nlohmann::json> createInteriorCell(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getLeveledList(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> setLeveledListSettings(const nlohmann::json& arguments) override;
    Result<nlohmann::json> editLeveledListEntry(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getNpc(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> editNpcEntries(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addScriptProperty(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeScriptProperty(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getFaction(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> addSoundFile(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addPackageProcedure(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removePackageProcedure(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeSoundFile(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setSoundBase(const nlohmann::json& arguments) override;
    Result<nlohmann::json> createSound(const nlohmann::json& arguments) override;
    Result<nlohmann::json> editFactionEntries(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getWeaponDamageTypes(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> conditionFunctionParams() const override {
        return nlohmann::json(ConditionFunctions::asJson());
    }

    void setDataDirectory(std::filesystem::path dataDirectory);
    std::filesystem::path dataDirectory() const override { return dataDirectory_; }

private:
    struct StagedLocalization {
        std::filesystem::path dlstringsPath;
        std::vector<std::byte> dlstringsBytes;
        std::vector<std::byte> originalDlstringsBytes;
        std::filesystem::path stringsPath;
        std::vector<std::byte> originalStringsBytes;
        std::filesystem::path ilstringsPath;
        std::vector<std::byte> originalIlstringsBytes;
    };

    struct LoadedPlugin {
        std::string name;
        std::filesystem::path path;
        std::string source;
        std::filesystem::file_time_type loadedTime{};
        bool trusted{false};
        fo4::Plugin plugin;
        std::optional<fo4::Plugin> originalPlugin;
        bool dirty{false};
        std::optional<StagedLocalization> stagedLocalization;
        std::unique_ptr<std::mutex> writeMutex = std::make_unique<std::mutex>();
    };

    enum class StateHold { None, Shared, Exclusive };

    class OpLock {
    public:
        enum class Mode { Read, Write, Lifecycle };
        OpLock(const RadiclePluginBackend& backend, Mode mode,
               const std::vector<std::string_view>& owners = {});
        ~OpLock();
        OpLock(const OpLock&) = delete;
        OpLock& operator=(const OpLock&) = delete;

        const RadiclePluginBackend& backend;
        Mode mode;
        StateHold state{StateHold::None};
        std::vector<std::unique_lock<std::mutex>> owners;
        OpLock* previous;

        static thread_local OpLock* current;
    };

    class CommitScope {
    public:
        CommitScope();
        ~CommitScope();
        CommitScope(const CommitScope&) = delete;
        CommitScope& operator=(const CommitScope&) = delete;

    private:
        OpLock* op_;
        StateHold restore_;
    };

    class StateReleased {
    public:
        StateReleased();
        ~StateReleased();
        StateReleased(const StateReleased&) = delete;
        StateReleased& operator=(const StateReleased&) = delete;

    private:
        OpLock* op_;
        StateHold restore_;
    };

    Result<LoadedPlugin*> createPluginLocked(std::string fileName);
    void stagePluginMutationLocked(LoadedPlugin& loaded, fo4::Plugin mutated);
    std::vector<std::string> laterOverridesLocked(const LoadedPlugin& target,
                                                  const fo4::FormKey& key) const;
    void stageLocalizationMutationLocked(LoadedPlugin& loaded, StagedLocalization staged);
    Result<void> persistPluginLocked(LoadedPlugin& loaded, const std::filesystem::path& output);
    Result<nlohmann::json> discardPlugin(const nlohmann::json& arguments);

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
    Result<nlohmann::json> setFormLinkField(const nlohmann::json& arguments);
    Result<nlohmann::json> setArmorField(const nlohmann::json& arguments);
    Result<nlohmann::json> setFormLinkListField(const nlohmann::json& arguments);
    Result<nlohmann::json> insertFormLinkListElement(const nlohmann::json& arguments);
    Result<nlohmann::json> removeFormLinkListElement(const nlohmann::json& arguments);
    Result<nlohmann::json> addListItem(const nlohmann::json& arguments);
    Result<nlohmann::json> removeListItem(const nlohmann::json& arguments);
    Result<nlohmann::json> setComponents(const nlohmann::json& arguments);
    Result<nlohmann::json> setQuestAliases(const nlohmann::json& arguments);
    Result<nlohmann::json> setQuestStages(const nlohmann::json& arguments);
    // Resolves a list value (FormKey text or EditorID) to a FormKey against the
    // loaded plugins, mirroring WriteService.ResolveFk.
    std::optional<fo4::FormKey> resolveListValue(std::string_view value) const;
    void ensureOwnerMaster(fo4::Plugin& plugin, const fo4::PluginKey& owner) const;
    Result<fo4::FormKey> copyRecordAsNewLocked(fo4::Plugin& mutated,
                                               const LoadedPlugin& source,
                                               const fo4::FormKey& key,
                                               std::optional<std::string_view> newEditorId) const;
    Result<fo4::FormKey> createProjectileLocked(fo4::Plugin& mutated, const nlohmann::json& payload) const;
    Result<fo4::FormKey> createExplosionLocked(fo4::Plugin& mutated, const nlohmann::json& payload) const;
    Result<fo4::FormKey> createHazardLocked(fo4::Plugin& mutated, const nlohmann::json& payload) const;

    Result<nlohmann::json> describeElement(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> addElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> moveElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> clearElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> copyElementToSelected(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeElementFromSelected(const nlohmann::json& arguments) override;

    Result<nlohmann::json> copyAsOverride(const nlohmann::json& arguments);
    Result<nlohmann::json> copyAsOverrideLocked(const nlohmann::json& arguments, bool initiallyDisabled);
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
    Result<nlohmann::json> getPluginWorklist(const nlohmann::json& arguments) const;
    Result<nlohmann::json> searchRecords(const nlohmann::json& arguments) const;
    Result<nlohmann::json> scanConflicts() const;
    Result<nlohmann::json> scanBrokenRefs() const;
    Result<nlohmann::json> listMasters(const nlohmann::json& arguments) const;
    Result<nlohmann::json> reorderMasters(const nlohmann::json& arguments);
    Result<nlohmann::json> setLight(const nlohmann::json& arguments);
    Result<nlohmann::json> saveMastersPlugin(const nlohmann::json& arguments);

    Result<nlohmann::json> getConditions(const nlohmann::json& arguments) const;
    Result<nlohmann::json> setConditions(const nlohmann::json& arguments);
    Result<nlohmann::json> getConditionsAtPath(const nlohmann::json& arguments) const;
    Result<nlohmann::json> setConditionsAtPath(const nlohmann::json& arguments);
    Result<nlohmann::json> getConditionFunctions() const;
    Result<nlohmann::json> getConditionRunOnTypes() const;

    struct SkippedStringArchive {
        std::string path;
        std::string reason;
    };
    Result<nlohmann::json> listStringTables(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getStrings(const nlohmann::json& arguments) const;
    Result<nlohmann::json> lookupString(const nlohmann::json& arguments) const;
    Result<nlohmann::json> setString(const nlohmann::json& arguments);
    Result<nlohmann::json> addString(const nlohmann::json& arguments);
    Result<nlohmann::json> removeString(const nlohmann::json& arguments);
    Result<nlohmann::json> importStrings(const nlohmann::json& arguments);
    Result<nlohmann::json> exportStrings(const nlohmann::json& arguments) const;
    Result<nlohmann::json> saveStringTables(const nlohmann::json& arguments);
    Result<nlohmann::json> discardStringEdits(const nlohmann::json& arguments);
    Result<std::vector<fo4::env::PluginStringTable>> readStringTables(
        const LoadedPlugin& loaded,
        std::string_view language,
        std::vector<SkippedStringArchive>* skipped = nullptr) const;
    std::shared_ptr<const fo4::archive::AssetResolver> stringAssets(
        const LoadedPlugin& loaded,
        std::vector<SkippedStringArchive>* skipped) const;
    void overlayStagedStringTables(const LoadedPlugin& loaded,
                                   std::string_view language,
                                   std::vector<fo4::env::PluginStringTable>& tables) const;
    void stageStringTable(std::string_view plugin,
                          std::string_view language,
                          fo4::env::StringSource source,
                          fo4::localization::LocalizedStringTable table);
    void discardStagedStringTables(std::string_view plugin);
    bool hasStagedStringTables(std::string_view plugin) const;
    bool stringTablePending(std::string_view plugin,
                            std::string_view language,
                            fo4::env::StringSource source) const;

    LoadedPlugin* findPlugin(std::string_view name);
    const LoadedPlugin* findPlugin(std::string_view name) const;
    bool isEditable(const LoadedPlugin& plugin) const;
    bool fileUnchangedSinceLoad(const LoadedPlugin& plugin) const;
    std::optional<Error> writeGuard(const LoadedPlugin& plugin) const;
    Result<bool> stageTypedRecordEdit(std::string_view pluginName,
                                      const fo4::FormKey& key,
                                      std::string_view failureCode,
                                      const std::function<void(fo4::Plugin&)>& edit);
    Result<const fo4::Plugin*> versionPlugin(std::string_view pluginName,
                                             const fo4::FormKey& key) const;
    std::optional<Error> requireReferenceCoverage(const fo4::FormKey& key,
                                                  std::string_view operation) const;
    std::optional<std::filesystem::path> allowedSavePath(
        const std::filesystem::path& path) const;
    std::optional<std::filesystem::path> allowedLocalizationDirectory(
        const std::filesystem::path& path) const;
    void rebuildGraphLocked();
    std::string dirtyPluginListLocked() const;

    mutable std::shared_mutex lifecycleMutex_;
    mutable std::shared_mutex stateMutex_;
    std::mutex saveIoMutex_;
    std::vector<LoadedPlugin> plugins_;
    std::uint64_t mutationEpoch_{0};
    fo4::ConflictGraph graph_;
    std::filesystem::path dataDirectory_;
    using StringArchiveStamp = std::tuple<std::filesystem::path, std::uintmax_t, std::filesystem::file_time_type>;
    mutable std::mutex stringAssetsMutex_;
    mutable std::vector<StringArchiveStamp> stringArchives_;
    mutable std::shared_ptr<const fo4::archive::AssetResolver> stringAssets_;
    mutable std::vector<SkippedStringArchive> stringSkipped_;
    using StagedStringKey = std::tuple<std::string, std::string, fo4::env::StringSource>;
    mutable std::mutex stagedStringsMutex_;
    std::map<StagedStringKey, fo4::localization::LocalizedStringTable> stagedStrings_;
    std::filesystem::path trustedNewPluginRoot_;
    std::vector<std::filesystem::path> trustedSaveRoots_;
};

}
