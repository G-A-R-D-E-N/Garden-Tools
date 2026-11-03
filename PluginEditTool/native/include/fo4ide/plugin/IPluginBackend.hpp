#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::plugin {

struct Capabilities {
    bool available{false};
    bool readPlugins{false};
    bool writePlugins{false};
    bool resolveReferences{false};
};

class IPluginBackend {
public:
    virtual ~IPluginBackend() = default;

    virtual Capabilities capabilities() const noexcept = 0;
    virtual Result<nlohmann::json> invoke(std::string_view operation,
                                          const nlohmann::json& arguments) = 0;

    virtual Result<nlohmann::json> invokeBackend(std::string_view method,
                                                 const nlohmann::json& arguments) {
        return invoke("backend." + std::string(method), arguments);
    }

    virtual Result<nlohmann::json> invokeLegacyBackend(
        std::string_view method,
        const nlohmann::json& arguments) {
        return invokeBackend(method, arguments);
    }

    virtual Result<void> setTrustedNewPluginRoot(const std::filesystem::path&) {
        return {};
    }

    virtual Result<nlohmann::json> createPlugin(const nlohmann::json& arguments) {
        return invokeLegacyBackend("CreatePlugin", arguments);
    }

    virtual Result<void> discardCreatedPlugin(std::string_view) {
        return std::unexpected(Error{
            "DiscardCreatedPluginUnavailable",
            "This backend cannot roll back an auto-created plugin"
        });
    }

    virtual Result<nlohmann::json> setFieldValue(const nlohmann::json& arguments) {
        return invokeBackend("SetField", arguments);
    }

    virtual Result<nlohmann::json> copyAsDisabledOverride(const nlohmann::json&) {
        return std::unexpected(Error{
            "CopyAsDisabledOverrideUnavailable",
            "Native copy as disabled override is unavailable for this backend"
        });
    }
    virtual Result<nlohmann::json> copyAsWrapper(const nlohmann::json&) {
        return std::unexpected(Error{
            "CopyAsWrapperUnavailable",
            "Native copy as wrapper is unavailable for this backend"
        });
    }
    virtual Result<nlohmann::json> cleanUnusedMasters(const nlohmann::json&) {
        return std::unexpected(Error{
            "CleanMastersUnavailable",
            "Native clean-masters is unavailable for this backend"
        });
    }
    virtual Result<nlohmann::json> updateOnam(const nlohmann::json&) {
        return std::unexpected(Error{
            "UpdateOnamUnavailable",
            "Native ONAM update is unavailable for this backend"
        });
    }
    virtual Result<nlohmann::json> compactToEsl(const nlohmann::json&) {
        return std::unexpected(Error{
            "CompactToEslUnavailable",
            "Native compact-to-ESL is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> renumberPluginFormIds(const nlohmann::json&) {
        return std::unexpected(Error{
            "RenumberPluginUnavailable",
            "Native plugin-wide FormID renumbering is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> injectFormsIntoMaster(const nlohmann::json&) {
        return std::unexpected(Error{
            "InjectFormsIntoMasterUnavailable",
            "Native inject-forms-into-master is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> deepCopyAsOverride(const nlohmann::json&) {
        return std::unexpected(Error{
            "DeepCopyUnavailable",
            "Native deep-copy-as-override is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> changeReferencingRecords(const nlohmann::json&) {
        return std::unexpected(Error{
            "ReferenceRemapUnavailable",
            "Native referencing-record remapping is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> revertOverrides(const nlohmann::json&) {
        return std::unexpected(Error{
            "RevertOverridesUnavailable",
            "Native override reverting is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> createMergedPatch(const nlohmann::json&) {
        return std::unexpected(Error{
            "MergedPatchUnavailable",
            "Native merged-patch creation is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> checkCircularLeveledLists(const nlohmann::json&) const {
        return std::unexpected(Error{
            "CircularLeveledListCheckUnavailable",
            "Native circular leveled-list checking is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> lootCleaningReport(const nlohmann::json&) const {
        return std::unexpected(Error{
            "LootCleaningReportUnavailable",
            "Native LOOT cleaning report is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> checkForErrors(const nlohmann::json&) const {
        return std::unexpected(Error{
            "CheckForErrorsUnavailable",
            "Native check for errors is unavailable for this backend"
        });
    }
    virtual Result<nlohmann::json> findUnreachable(const nlohmann::json&) const {
        return std::unexpected(Error{
            "FindUnreachableUnavailable",
            "Native reachable-info analysis is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> comparePluginTo(const nlohmann::json&) const {
        return std::unexpected(Error{
            "ComparePluginUnavailable",
            "Native plugin comparison is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> createSeqFile(const nlohmann::json&) const {
        return std::unexpected(Error{
            "SeqGenerationUnavailable",
            "Native SEQ generation is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> conditionFunctionParams() const {
        return std::unexpected(Error{
            "ConditionMetadataUnavailable",
            "Condition parameter metadata is not provided by this backend"
        });
    }

    virtual Result<nlohmann::json> describeElement(const nlohmann::json&) const {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element queries are unavailable"});
    }
    virtual Result<nlohmann::json> addElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element insertion is unavailable"});
    }
    virtual Result<nlohmann::json> removeElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element removal is unavailable"});
    }
    virtual Result<nlohmann::json> moveElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element movement is unavailable"});
    }
    virtual Result<nlohmann::json> clearElement(const nlohmann::json&) {
        return std::unexpected(Error{"ElementBackendUnavailable", "Generic element clearing is unavailable"});
    }
    virtual Result<nlohmann::json> copyElementToSelected(const nlohmann::json&) {
        return std::unexpected(Error{
            "CopyElementToSelectedUnavailable",
            "Bulk element copy is unavailable for this backend"
        });
    }
    virtual Result<nlohmann::json> removeElementFromSelected(const nlohmann::json&) {
        return std::unexpected(Error{
            "RemoveElementFromSelectedUnavailable",
            "Bulk element removal is unavailable for this backend"
        });
    }

    virtual Result<nlohmann::json> getPlacedReferences(const nlohmann::json&) const {
        return std::unexpected(Error{"CellBackendUnavailable", "Placed-reference queries are unavailable"});
    }
    virtual Result<nlohmann::json> getPlacedReferencesAtGrid(const nlohmann::json&) const {
        return std::unexpected(Error{"CellBackendUnavailable", "Exterior-cell queries are unavailable"});
    }
    virtual Result<nlohmann::json> setPlacedReferenceTransform(const nlohmann::json&) {
        return std::unexpected(Error{"CellBackendUnavailable", "Placed-reference writes are unavailable"});
    }
    virtual Result<nlohmann::json> createPlacedObject(const nlohmann::json&) {
        return std::unexpected(Error{"CellBackendUnavailable", "Placed-reference creation is unavailable"});
    }
    virtual Result<nlohmann::json> createInteriorCell(const nlohmann::json&) {
        return std::unexpected(Error{"CellBackendUnavailable", "Interior-cell creation is unavailable"});
    }

    virtual Result<nlohmann::json> getLeveledList(const nlohmann::json&) const {
        return std::unexpected(Error{"LeveledListBackendUnavailable", "Leveled list queries are unavailable"});
    }
    virtual Result<nlohmann::json> setLeveledListSettings(const nlohmann::json&) {
        return std::unexpected(Error{"LeveledListBackendUnavailable", "Leveled list writes are unavailable"});
    }
    virtual Result<nlohmann::json> editLeveledListEntry(const nlohmann::json&) {
        return std::unexpected(Error{"LeveledListBackendUnavailable", "Leveled list writes are unavailable"});
    }
    virtual Result<nlohmann::json> getNpc(const nlohmann::json&) const {
        return std::unexpected(Error{"NpcBackendUnavailable", "NPC queries are unavailable"});
    }
    virtual Result<nlohmann::json> editNpcEntries(const nlohmann::json&) {
        return std::unexpected(Error{"NpcBackendUnavailable", "NPC writes are unavailable"});
    }
    virtual Result<nlohmann::json> getPerkEffects(const nlohmann::json&) const {
        return std::unexpected(Error{"PerkBackendUnavailable", "PERK effect queries are unavailable"});
    }
    virtual Result<nlohmann::json> setPerkEffects(const nlohmann::json&) {
        return std::unexpected(Error{"PerkBackendUnavailable", "PERK effect writes are unavailable"});
    }
    virtual Result<nlohmann::json> getPerkLinks(const nlohmann::json&) const {
        return std::unexpected(Error{"PerkBackendUnavailable", "PERK link queries are unavailable"});
    }
    virtual Result<nlohmann::json> setPerkNext(const nlohmann::json&) {
        return std::unexpected(Error{"PerkBackendUnavailable", "PERK next writes are unavailable"});
    }
    virtual Result<nlohmann::json> setPerkSound(const nlohmann::json&) {
        return std::unexpected(Error{"PerkBackendUnavailable", "PERK sound writes are unavailable"});
    }
    virtual Result<nlohmann::json> setPerkSwf(const nlohmann::json&) {
        return std::unexpected(Error{"PerkBackendUnavailable", "PERK swf writes are unavailable"});
    }
    // Constraint: sound structure writes keep the in-memory plugin unchanged on refusal.
    // Constraint: package tree writes keep the in-memory plugin unchanged on refusal.
    virtual Result<nlohmann::json> addPackageProcedure(const nlohmann::json&) {
        return std::unexpected(Error{"PackageBackendUnavailable", "Package tree writes are unavailable"});
    }
    virtual Result<nlohmann::json> removePackageProcedure(const nlohmann::json&) {
        return std::unexpected(Error{"PackageBackendUnavailable", "Package tree writes are unavailable"});
    }
    virtual Result<nlohmann::json> addSoundFile(const nlohmann::json&) {
        return std::unexpected(Error{"SoundBackendUnavailable", "Sound file writes are unavailable"});
    }
    virtual Result<nlohmann::json> removeSoundFile(const nlohmann::json&) {
        return std::unexpected(Error{"SoundBackendUnavailable", "Sound file writes are unavailable"});
    }
    virtual Result<nlohmann::json> setSoundBase(const nlohmann::json&) {
        return std::unexpected(Error{"SoundBackendUnavailable", "Sound base-link writes are unavailable"});
    }
    virtual Result<nlohmann::json> createSound(const nlohmann::json&) {
        return std::unexpected(Error{"SoundBackendUnavailable", "Sound record creation is unavailable"});
    }
    // Constraint: memory trimming only pages the process out; it never drops plugin data.
    virtual Result<nlohmann::json> releaseMemory(const nlohmann::json&) {
        return std::unexpected(Error{"MemoryBackendUnavailable", "Memory trimming is unavailable"});
    }

    // Constraint: script value writes must keep the in-memory plugin unchanged on refusal.
    virtual Result<nlohmann::json> setScriptValue(const nlohmann::json&) {
        return std::unexpected(Error{"ScriptBackendUnavailable", "Script value writes are unavailable"});
    }

    // Constraint: script structure writes keep the in-memory plugin unchanged on refusal.
    virtual Result<nlohmann::json> addScript(const nlohmann::json&) {
        return std::unexpected(Error{"ScriptBackendUnavailable", "Script writes are unavailable"});
    }
    virtual Result<nlohmann::json> removeScript(const nlohmann::json&) {
        return std::unexpected(Error{"ScriptBackendUnavailable", "Script writes are unavailable"});
    }
    virtual Result<nlohmann::json> addScriptProperty(const nlohmann::json&) {
        return std::unexpected(Error{"ScriptBackendUnavailable", "Script property writes are unavailable"});
    }
    virtual Result<nlohmann::json> removeScriptProperty(const nlohmann::json&) {
        return std::unexpected(Error{"ScriptBackendUnavailable", "Script property writes are unavailable"});
    }
    virtual Result<nlohmann::json> getFaction(const nlohmann::json&) const {
        return std::unexpected(Error{"FactionBackendUnavailable", "Faction queries are unavailable"});
    }
    virtual Result<nlohmann::json> editFactionEntries(const nlohmann::json&) {
        return std::unexpected(Error{"FactionBackendUnavailable", "Faction writes are unavailable"});
    }
    virtual Result<nlohmann::json> getWeaponDamageTypes(const nlohmann::json&) const {
        return std::unexpected(Error{"WeaponBackendUnavailable", "Weapon queries are unavailable"});
    }
    virtual Result<nlohmann::json> getMagicEffects(const nlohmann::json&) const {
        return std::unexpected(Error{"MagicEffectBackendUnavailable", "Magic effect queries are unavailable"});
    }
    virtual Result<nlohmann::json> addMagicEffect(const nlohmann::json&) {
        return std::unexpected(Error{"MagicEffectBackendUnavailable", "Magic effect writes are unavailable"});
    }
    virtual Result<nlohmann::json> setMagicEffect(const nlohmann::json&) {
        return std::unexpected(Error{"MagicEffectBackendUnavailable", "Magic effect writes are unavailable"});
    }
    virtual Result<nlohmann::json> removeMagicEffect(const nlohmann::json&) {
        return std::unexpected(Error{"MagicEffectBackendUnavailable", "Magic effect writes are unavailable"});
    }
};

class NullPluginBackend final : public IPluginBackend {
public:
    Capabilities capabilities() const noexcept override {
        return {};
    }

    Result<nlohmann::json> invoke(std::string_view,
                                  const nlohmann::json&) override {
        return std::unexpected(Error{
            "PluginBackendUnavailable",
            "Native plugin backend is not connected yet"
        });
    }
};

}
