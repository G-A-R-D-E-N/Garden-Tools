#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <fo4/archive/assets.hpp>
#include <fo4/environment/strings.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace fo4ide::plugin {

#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
namespace detail { class Ba2ReadRoot; class HeldLocalizationGeneration; class JournalSession; struct JournalRoots; }
#endif

#ifdef FO4IDE_TEST_HOOKS
using LocalizedQuestBeforeCommitHook = void (*)();
void setLocalizedQuestBeforeCommitHook(LocalizedQuestBeforeCommitHook hook);
using LocalizedStringBatchAfterInsertHook = void (*)();
void setLocalizedStringBatchAfterInsertHook(LocalizedStringBatchAfterInsertHook hook);
using LocalizedAllocationAfterReadHook = void (*)();
void setLocalizedAllocationAfterReadHook(LocalizedAllocationAfterReadHook hook);
using LocalizedStringAfterReadHook = void (*)();
void setLocalizedStringAfterReadHook(LocalizedStringAfterReadHook hook);
using LegacyStringResolverHook = void (*)();
void setLegacyStringResolverHook(LegacyStringResolverHook hook);
using LocalizedQuestAfterTableParseHook = void (*)();
void setLocalizedQuestAfterTableParseHook(LocalizedQuestAfterTableParseHook hook);
using LoadMo2AfterPendingCheckHook = void (*)();
void setLoadMo2AfterPendingCheckHook(LoadMo2AfterPendingCheckHook hook);
using WriteOpLockedHook = void (*)();
void setWriteOpLockedHook(WriteOpLockedHook hook);
void setWriteOpPendingHook(WriteOpLockedHook hook);
void setSaveIoLockedHook(WriteOpLockedHook hook);
using LoadReadWindowHook = void (*)();
void setLoadReadWindowHook(LoadReadWindowHook hook);
#endif

Result<void> requirePluginUnchangedSince(const std::filesystem::path& path,
                                         std::filesystem::file_time_type loadedTime);

struct LocalizedDirectorySnapshot {
    std::filesystem::path directory{};
    std::map<std::string, std::vector<fo4::env::PluginStringTableSnapshot>> languages{};
    std::uint32_t maximum{0};
};

struct LocalizedStringAllocation {
#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
    std::shared_ptr<detail::HeldLocalizationGeneration> heldGeneration;
    std::shared_ptr<const detail::Ba2ReadRoot> heldDataRoot{};
#endif
    std::string language;
    std::uint32_t nextId{1};
    std::map<fo4::env::StringSource, fo4::localization::LocalizedStringTable> tables{};
    std::set<fo4::env::StringSource> touched{};
    std::map<fo4::env::StringSource, std::set<std::uint32_t>> allocatedIds{};
    std::vector<fo4::env::PluginStringTableSnapshot> sourceSnapshots{};
    std::map<std::string, std::vector<fo4::env::PluginStringTableSnapshot>> allocationSources{};
    bool defaultDirectoryExists{false};
    std::vector<LocalizedDirectorySnapshot> additionalSources{};
};

using LocalizedReferenceKey = std::tuple<std::string, fo4::env::StringSource, std::filesystem::path>;
using LocalizedReferences = std::map<LocalizedReferenceKey, std::set<std::uint32_t>>;

class RadiclePluginBackend final : public IPluginBackend {
public:
    RadiclePluginBackend();
    ~RadiclePluginBackend() override;
    RadiclePluginBackend(const RadiclePluginBackend&) = delete;
    RadiclePluginBackend& operator=(const RadiclePluginBackend&) = delete;
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
    Result<nlohmann::json> repairHedrCount(const nlohmann::json& arguments) override;
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
    Result<nlohmann::json> getPerkEffects(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> setPerkEffects(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getPerkLinks(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> setPerkNext(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setPerkSound(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setPerkSwf(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setScriptValue(const nlohmann::json& arguments) override;
    Result<nlohmann::json> releaseMemory(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addScriptProperty(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeScriptProperty(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addAliasScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeAliasScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addAliasScriptProperty(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeAliasScriptProperty(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setAliasScriptValue(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getFaction(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> removeQuestFragment(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getQuestFragments(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> getQuestScript(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> editQuestScript(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addSoundFile(const nlohmann::json& arguments) override;
    Result<nlohmann::json> addPackageProcedure(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removePackageProcedure(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeSoundFile(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setSoundBase(const nlohmann::json& arguments) override;
    Result<nlohmann::json> createSound(const nlohmann::json& arguments) override;
    Result<nlohmann::json> editFactionEntries(const nlohmann::json& arguments) override;
    Result<nlohmann::json> getWeaponDamageTypes(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> getMagicEffects(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> addMagicEffect(const nlohmann::json& arguments) override;
    Result<nlohmann::json> setMagicEffect(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeMagicEffect(const nlohmann::json& arguments) override;
    Result<nlohmann::json> conditionFunctionParams() const override {
        return nlohmann::json(ConditionFunctions::asJson());
    }

    void setDataDirectory(std::filesystem::path dataDirectory);

#ifdef FO4IDE_TEST_HOOKS
    std::weak_ptr<const fo4::ConflictGraph> cachedGraphForTest() const;
    Result<nlohmann::json> testSetQuestFragment(const nlohmann::json& arguments);
    std::optional<std::pair<std::string, std::vector<std::uint8_t>>> testStagedQuestScript(std::string_view plugin) const;
    void testSetJournalRoot(std::filesystem::path root);
#ifdef __linux__
    bool testHasReadAuthority(const std::filesystem::path& root) const;
    Result<std::uint32_t> testHeldMaximumStringId(std::string_view plugin, std::string_view language) const;
#endif
#endif

private:
#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
    using TestReadRoots = std::map<std::filesystem::path, std::shared_ptr<const detail::Ba2ReadRoot>>;
    std::shared_ptr<const detail::Ba2ReadRoot> testDataReadRoot_;
    std::string dataReadRootError_;
    TestReadRoots testReadRoots_;
    bool testRecoverJournal(detail::JournalSession& session, const std::vector<detail::JournalRoots>& approved,
                            const nlohmann::json& arguments, TestReadRoots& readRoots) const;
#endif
    static std::filesystem::path defaultJournalRoot();
    static void ensureJournalParent(const std::filesystem::path& root);
    std::filesystem::path testJournalRoot_{defaultJournalRoot()};
    std::string recoveryPlugin_;
    std::optional<Error> testRecoveryGate(std::string_view operation, const nlohmann::json& arguments) const;
    struct StagedQuestFragments {
        fo4::FormKey questKey{};
        std::filesystem::path sourcePath{};
        std::filesystem::path pexPath{};
        std::string source{};
        std::vector<std::uint8_t> pex{};
        std::optional<std::string> originalSource{};
        std::optional<std::string> originalPex{};
    };

    struct StagedLocalizationFile {
        std::filesystem::path path{};
        std::filesystem::path rootSelector{};
        std::vector<std::string> directories{};
        std::string filename{};
        std::string kind{};
        std::string language{};
        bool originalExists{false};
        std::string originalHash{};
        std::uint64_t originalSize{0};
        std::vector<std::byte> finalBytes{};
        bool verifyOnly{false};
    };

    struct StagedStringTable {
        fo4::localization::LocalizedStringTable table{};
        StagedLocalizationFile file{};
    };

    struct StagedLocalization {
        std::vector<StagedLocalizationFile> participants;
    };

    struct LoadedPlugin {
        std::string name{};
        std::filesystem::path path{};
        std::string source{};
        std::filesystem::file_time_type loadedTime{};
        bool trusted{false};
        fo4::Plugin plugin{};
        bool dirty{false};
        std::optional<StagedLocalization> stagedLocalization{};
        std::unique_ptr<LocalizedStringAllocation> stagedAllocation{};
        std::set<std::string> stagedPluginLanguages{};
        LocalizedReferences stagedPluginStringIds{};
        std::optional<StagedQuestFragments> stagedQuestFragments{};
        std::unique_ptr<std::mutex> writeMutex = std::make_unique<std::mutex>();
        std::shared_ptr<const std::vector<std::byte>> diskBaseline = plugin.original_data;
    };

#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
    static TestReadRoots testFilterReadRoots(const TestReadRoots& roots, const std::vector<LoadedPlugin>& loaded);
    static void testAuthorizeProfileReadRoots(const nlohmann::json& arguments, TestReadRoots& readRoots);
    Result<std::shared_ptr<detail::HeldLocalizationGeneration>> testCaptureHeldGeneration(
        const LoadedPlugin& loaded, std::string_view language) const;
    Result<LocalizedStringAllocation> testOpenHeldLocalizedStrings(const LoadedPlugin& loaded, std::string_view language) const;
    Result<std::uint32_t> testHeldMaximum(const LoadedPlugin& loaded, detail::HeldLocalizationGeneration& generation) const;
    void testReserveHeldStagedCopy(detail::HeldLocalizationGeneration& generation) const;
    static void testReserveHeldWriter(detail::HeldLocalizationGeneration& generation,
                                      const fo4::localization::LocalizedStringTable& table);
#endif

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

    // Constraint: plugins_ is replaced by lifecycle operations, so this snapshot takes a read OpLock.
    std::vector<std::string> archivePluginNames() const;
    // Requires: caller holds an OpLock.
    std::vector<std::string> archivePluginNamesLocked() const;
    Result<LoadedPlugin*> createPluginLocked(std::string fileName);
    void stagePluginMutationLocked(LoadedPlugin& loaded, fo4::Plugin mutated);
    bool stageQuestFragmentMutationLocked(LoadedPlugin& loaded, fo4::Plugin mutated,
                                          StagedQuestFragments fragments);
    std::vector<std::string> laterOverridesLocked(const LoadedPlugin& target,
                                                  const fo4::FormKey& key) const;
    void stageLocalizationMutationLocked(LoadedPlugin& loaded, StagedLocalization staged);
    Result<void> persistPluginLocked(LoadedPlugin& loaded, const std::filesystem::path& output);
    Result<void> persistJournalLocked(LoadedPlugin& loaded, const std::filesystem::path& output,
                                      std::optional<std::string_view> stringsOnly = std::nullopt);
    Result<nlohmann::json> discardPlugin(const nlohmann::json& arguments);

    Result<nlohmann::json> loadEnvironment(const nlohmann::json& arguments);
    Result<nlohmann::json> getPlugins() const;
    Result<nlohmann::json> getChildren(const nlohmann::json& arguments) const;
    Result<nlohmann::json> openRecord(const nlohmann::json& arguments) const;
    Result<nlohmann::json> openPlugin(const nlohmann::json& arguments);
    Result<nlohmann::json> savePlugin(const nlohmann::json& arguments);
    Result<nlohmann::json> createRecord(const nlohmann::json& arguments);
    Result<nlohmann::json> setQuestFragment(const nlohmann::json& arguments);
    struct QuestFragmentWrite {
        fo4::Plugin mutated{};
        fo4::FormKey key{};
        std::string scriptName{};
        std::filesystem::path sourcePath{};
        std::filesystem::path pexPath{};
        std::filesystem::path imports{};
        std::string function{};
        std::string source{};
        std::optional<std::string> originalSource{};
        std::optional<std::string> originalPex{};
        bool hasSource{false};
        bool hasHeader{false};
        bool hasBinding{false};
    };
    Result<QuestFragmentWrite> prepareQuestFragmentLocked(const LoadedPlugin& loaded, const fo4::FormKey& key,
                                                          std::uint16_t stage, std::uint32_t index,
                                                          std::string_view scriptName, std::string_view outputDirectory,
                                                          std::string_view importsDirectory) const;
    Result<nlohmann::json> stageQuestFragmentWriteLocked(LoadedPlugin& loaded, QuestFragmentWrite write,
                                                         const std::string& source);
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
    Result<nlohmann::json> setQuestObjectives(const nlohmann::json& arguments);
    Result<nlohmann::json> getQuestObjectives(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getQuestStages(const nlohmann::json& arguments) const;
    Result<nlohmann::json> getQuestAliases(const nlohmann::json& arguments) const;
    Result<nlohmann::json> createDialogueBranch(const nlohmann::json& arguments);
    Result<nlohmann::json> createDialogueTopic(const nlohmann::json& arguments);
    Result<nlohmann::json> setDialogueBranchStart(const nlohmann::json& arguments);
    Result<nlohmann::json> createInfo(const nlohmann::json& arguments);
    Result<nlohmann::json> setInfo(const nlohmann::json& arguments);
    Result<nlohmann::json> removeInfo(const nlohmann::json& arguments);
    Result<nlohmann::json> getDialogue(const nlohmann::json& arguments) const;
    Result<nlohmann::json> createScene(const nlohmann::json& arguments);
    Result<nlohmann::json> getScene(const nlohmann::json& arguments) const;
    Result<nlohmann::json> setScene(const nlohmann::json& arguments);
    Result<nlohmann::json> setScenePhase(const nlohmann::json& arguments);
    Result<nlohmann::json> addScenePhase(const nlohmann::json& arguments);
    Result<nlohmann::json> removeScenePhase(const nlohmann::json& arguments);
    Result<nlohmann::json> moveScenePhase(const nlohmann::json& arguments);
    Result<nlohmann::json> setSceneActor(const nlohmann::json& arguments);
    Result<nlohmann::json> addSceneActor(const nlohmann::json& arguments);
    Result<nlohmann::json> removeSceneActor(const nlohmann::json& arguments);
    Result<nlohmann::json> setSceneAction(const nlohmann::json& arguments);
    Result<nlohmann::json> addSceneAction(const nlohmann::json& arguments);
    Result<nlohmann::json> removeSceneAction(const nlohmann::json& arguments);
    Result<nlohmann::json> moveSceneAction(const nlohmann::json& arguments);
    Result<nlohmann::json> setInfoFragment(const nlohmann::json& arguments);
    Result<nlohmann::json> removeInfoFragment(const nlohmann::json& arguments);
    Result<nlohmann::json> setScenePhaseFragment(const nlohmann::json& arguments);
    Result<nlohmann::json> removeScenePhaseFragment(const nlohmann::json& arguments);
    Result<nlohmann::json> createDialogueTopicBulk(const nlohmann::json& arguments);
    Result<nlohmann::json> getVoicePath(const nlohmann::json& arguments) const;
    std::optional<fo4::RawFormId> resolveDialogueEditorId(const fo4::Plugin& target, std::string_view text) const;
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
    Result<fo4::FormKey> createPerkLocked(const LoadedPlugin& loaded,
                                          fo4::Plugin& mutated,
                                          const nlohmann::json& payload,
                                          LocalizedStringAllocation& strings) const;
    Result<LocalizedStringAllocation> openLocalizedStrings(const LoadedPlugin& loaded,
                                                           std::string_view language) const;
    Result<std::set<std::string>> localizationLanguages(const LoadedPlugin& loaded) const;
    Result<std::uint32_t> nextLocalizedStringId(const LoadedPlugin& loaded,
                                                LocalizedStringAllocation* allocation = nullptr) const;
    Result<std::uint32_t> allocateLocalizedString(LocalizedStringAllocation& allocation,
                                                  fo4::env::StringSource source,
                                                  std::string_view text) const;
    Result<void> stageLocalizedStringsAndPlugin(std::string_view plugin,
                                                LocalizedStringAllocation allocation,
                                                fo4::Plugin mutated);

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
        std::vector<SkippedStringArchive>* skipped = nullptr,
        std::vector<fo4::env::PluginStringTableSnapshot>* snapshots = nullptr) const;
    Result<std::vector<fo4::env::PluginStringTable>> readQuestStringTables(
        const LoadedPlugin& loaded, std::string_view language,
        const std::optional<std::filesystem::path>& stringsDirectory) const;
    std::shared_ptr<const fo4::archive::AssetResolver> stringAssets(
        const LoadedPlugin& loaded,
        std::vector<SkippedStringArchive>* skipped) const;
    void overlayStagedStringTables(const LoadedPlugin& loaded,
                                   std::string_view language,
                                   std::vector<fo4::env::PluginStringTable>& tables) const;
    Result<void> stageStringTable(std::string_view plugin,
                                  std::string_view language,
                                  fo4::env::StringSource source,
                                  fo4::localization::LocalizedStringTable table,
                                  const std::filesystem::path& directory = {},
                                  const StagedLocalizationFile* baseline = nullptr,
                                  const std::vector<fo4::env::PluginStringTableSnapshot>* snapshots = nullptr);
    Result<StagedStringTable> prepareStringTable(std::string_view plugin,
                                                 std::string_view language,
                                                 fo4::env::StringSource source,
                                                 fo4::localization::LocalizedStringTable table,
                                                 const std::filesystem::path& directory = {},
                                                 const StagedLocalizationFile* baseline = nullptr,
                                                 bool heldVerified = false);
    Result<StagedStringTable> prepareAllocatedStringTable(std::string_view plugin,
                                                          LocalizedStringAllocation& allocation,
                                                          fo4::env::StringSource source);
    Result<void> stageAllocatedStringTable(std::string_view plugin,
                                           LocalizedStringAllocation allocation,
                                           fo4::env::StringSource source);
    Result<void> validateLocalizedAllocation(const LoadedPlugin& loaded,
                                              const LocalizedStringAllocation& allocation) const;
    Result<bool> localStringDirectoryExists(const LoadedPlugin& loaded) const;
    Result<LocalizedDirectorySnapshot> captureLocalizedDirectory(const LoadedPlugin& loaded,
                                                                  const std::filesystem::path& directory) const;
    Result<std::uint32_t> includeLocalizedDirectory(const LoadedPlugin& loaded,
                                                     LocalizedStringAllocation& allocation,
                                                     const std::filesystem::path& directory,
                                                     std::uint32_t next) const;
    Result<void> validateStringIdsRetained(const LoadedPlugin& loaded, std::string_view language,
                                            fo4::env::StringSource source,
                                            const fo4::localization::LocalizedStringTable& table) const;
    LocalizedReferences questStringReferences(const LoadedPlugin& loaded, const fo4::Plugin& mutated,
                                               std::string_view language, fo4::env::StringSource source,
                                               const std::filesystem::path& destination) const;
    Result<void> stageQuestLocalizationMutationLocked(LoadedPlugin& loaded,
                                                       StagedLocalization localization,
                                                       fo4::Plugin mutated,
                                                       fo4::env::StringSource source,
                                                       StagedStringTable table,
                                                       LocalizedStringAllocation allocation);
    std::optional<StagedStringTable> stagedStringTable(std::string_view plugin,
                                                       std::string_view language,
                                                       fo4::env::StringSource source,
                                                       const std::filesystem::path& path) const;
    std::optional<StagedLocalizationFile> localizationFile(
        const std::filesystem::path& directory,
        std::string_view filename,
        std::string_view kind,
        std::string_view language,
        std::span<const std::byte> originalBytes,
        bool originalExists = true) const;
    void discardStagedStringTables(std::string_view plugin);
    bool hasStagedStringTables(std::string_view plugin) const;
    bool stringTablePending(std::string_view plugin,
                            std::string_view language,
                            fo4::env::StringSource source) const;

    LoadedPlugin* findPlugin(std::string_view name);
    const LoadedPlugin* findPlugin(std::string_view name) const;
    bool isEditable(const LoadedPlugin& plugin) const;
    bool fileUnchangedSinceLoad(const LoadedPlugin& plugin) const;
    bool diskMatchesBaseline(const LoadedPlugin& plugin) const;
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
        const std::filesystem::path& path, bool allowAbsent = false) const;
    std::shared_ptr<const fo4::ConflictGraph> conflictGraph() const;
    void markGraphDirtyLocked() const;
    void reapIdleConflictGraph(std::stop_token stop_token);
    void dropConflictGraphLocked() const;
    std::string dirtyPluginListLocked() const;

    // Constraint: references from this proxy must not be held across a stage, because the next access may rebuild the graph.
    class ConflictGraphProxy {
    public:
        explicit ConflictGraphProxy(RadiclePluginBackend* owner) : owner_(owner) {}
        const fo4::ConflictGraph* operator->() const {
            return owner_->conflictGraph().get();
        }
        const fo4::ConflictGraph& operator*() const {
            return *owner_->conflictGraph();
        }
        operator const fo4::ConflictGraph&() const { return **this; }
    private:
        RadiclePluginBackend* owner_{nullptr};
    };


    mutable std::shared_mutex lifecycleMutex_;
    mutable std::shared_mutex stateMutex_;
    std::mutex saveIoMutex_;
    std::vector<LoadedPlugin> plugins_;
    std::uint64_t mutationEpoch_{0};
    ConflictGraphProxy graph_{this};
    mutable std::mutex graphMutex_;
    mutable std::shared_ptr<const fo4::ConflictGraph> graphStorage_;
    mutable bool graphDirty_{true};
    mutable std::chrono::steady_clock::time_point graphLastUse_{};
    mutable std::chrono::seconds graphIdleLimit_{60};
    std::filesystem::path dataDirectory_;
    using StringArchiveStamp = std::tuple<std::filesystem::path, std::uintmax_t, std::filesystem::file_time_type>;
    mutable std::mutex stringAssetsMutex_;
    mutable std::vector<StringArchiveStamp> stringArchives_;
    mutable std::shared_ptr<const fo4::archive::AssetResolver> stringAssets_;
    mutable std::vector<SkippedStringArchive> stringSkipped_;
    using StagedStringKey = std::tuple<std::string, std::string, fo4::env::StringSource,
                                       std::filesystem::path>;
    mutable std::mutex stagedStringsMutex_;
    std::map<StagedStringKey, StagedStringTable> stagedStrings_;
    std::filesystem::path trustedNewPluginRoot_;
    std::vector<std::filesystem::path> trustedSaveRoots_;
    std::jthread graphReaper_;
};

}
