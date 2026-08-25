#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>
#include <fo4ide/plugin/IPluginBackend.hpp>

namespace fo4ide::plugin {

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
    Result<nlohmann::json> compactToEsl(const nlohmann::json& arguments) override;
    Result<nlohmann::json> renumberPluginFormIds(const nlohmann::json& arguments) override;
    Result<nlohmann::json> deepCopyAsOverride(const nlohmann::json& arguments) override;
    Result<nlohmann::json> changeReferencingRecords(const nlohmann::json& arguments) override;
    Result<nlohmann::json> revertOverrides(const nlohmann::json& arguments) override;
    Result<nlohmann::json> createMergedPatch(const nlohmann::json& arguments) override;
    Result<nlohmann::json> checkCircularLeveledLists(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> createSeqFile(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> getPlacedReferences(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> getPlacedReferencesAtGrid(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> setPlacedReferenceTransform(const nlohmann::json& arguments) override;
    Result<nlohmann::json> conditionFunctionParams() const override {
        return nlohmann::json(ConditionFunctions::asJson());
    }

    void setDataDirectory(std::filesystem::path dataDirectory);

private:
    struct LoadedPlugin {
        std::string name;
        std::filesystem::path path;
        std::string source;
        std::filesystem::file_time_type loadedTime{};
        bool trusted{false};
        fo4::Plugin plugin;
    };

    Result<LoadedPlugin*> createPluginLocked(std::string fileName);
    Result<LoadedPlugin*> ensureTargetPluginLocked(std::string_view requestedName);

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

    Result<nlohmann::json> describeElement(const nlohmann::json& arguments) const override;
    Result<nlohmann::json> addElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> removeElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> moveElement(const nlohmann::json& arguments) override;
    Result<nlohmann::json> clearElement(const nlohmann::json& arguments) override;

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

    Result<nlohmann::json> getConditions(const nlohmann::json& arguments) const;
    Result<nlohmann::json> setConditions(const nlohmann::json& arguments);
    Result<nlohmann::json> getConditionFunctions() const;
    Result<nlohmann::json> getConditionRunOnTypes() const;

    LoadedPlugin* findPlugin(std::string_view name);
    const LoadedPlugin* findPlugin(std::string_view name) const;
    bool isEditable(const LoadedPlugin& plugin) const;
    bool fileUnchangedSinceLoad(const LoadedPlugin& plugin) const;
    std::optional<Error> writeGuard(const LoadedPlugin& plugin) const;
    std::optional<std::filesystem::path> allowedSavePath(
        const std::filesystem::path& path) const;
    void rebuildGraphLocked();

    mutable std::mutex mutex_;
    std::vector<LoadedPlugin> plugins_;
    fo4::ConflictGraph graph_;
    std::filesystem::path dataDirectory_;
    std::filesystem::path trustedNewPluginRoot_;
    std::vector<std::filesystem::path> trustedSaveRoots_;
};

}
