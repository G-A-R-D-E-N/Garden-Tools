#include <fo4ide/plugin/ConditionFunctions.hpp>

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace fo4ide::plugin {

namespace {

std::string toLower(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

// Port of the Table array: "Function=Type1,Type2" rows.
const std::vector<std::pair<std::string, std::vector<std::string>>> kTable = {
    {"CanPayCrimeGold", {"Faction"}},
    {"CurrentFurnitureHasKeyword", {"Keyword"}},
    {"DialogueGetAv", {"ActorValue"}},
    {"DialogueGetItemCount", {"BaseObject"}},
    {"DialogueHasPerk", {"Perk"}},
    {"EPAlchemyEffectHasKeyword", {"Keyword"}},
    {"EPIsDamageType", {"DamageType"}},
    {"EPMagic_SpellHasKeyword", {"Keyword"}},
    {"EPMagic_SpellHasSkill", {"ActorValue"}},
    {"Exists", {"Reference"}},
    {"GetActionDataByte", {"Integer"}},
    {"GetActionDataFlag", {"Integer"}},
    {"GetActionDataShort", {"Integer"}},
    {"GetAngle", {"Axis"}},
    {"GetBaseValue", {"ActorValue"}},
    {"GetCombatTargetHasKeyword", {"Keyword"}},
    {"GetComponentCount", {"BaseObject"}},
    {"GetCrime", {"Actor", "CrimeType"}},
    {"GetCrimeGold", {"FactionNull"}},
    {"GetCrimeGoldNonviolent", {"FactionNull"}},
    {"GetCrimeGoldViolent", {"FactionNull"}},
    {"GetCurrentCastingType", {"CastingSource"}},
    {"GetCurrentDeliveryType", {"CastingSource"}},
    {"GetDeadCount", {"ActorBase"}},
    {"GetDetected", {"Actor"}},
    {"GetDistance", {"Reference"}},
    {"GetEquipped", {"BaseObject"}},
    {"GetEquippedItemType", {"CastingSource"}},
    {"GetEventData", {"Event", "EventData"}},
    {"GetFactionCombatReaction", {"Faction", "Faction"}},
    {"GetFactionRank", {"Faction"}},
    {"GetFactionRankDifference", {"Faction", "Actor"}},
    {"GetFactionRelation", {"Actor"}},
    {"GetGlobalValue", {"Global"}},
    {"GetGraphVariableFloat", {"String"}},
    {"GetGraphVariableInt", {"String"}},
    {"GetHeadingAngle", {"Reference"}},
    {"GetInCell", {"Cell"}},
    {"GetInCellParam", {"Cell", "Reference"}},
    {"GetInContainer", {"Reference"}},
    {"GetInCurrentLocation", {"Location"}},
    {"GetInCurrentLocationAlias", {"Alias"}},
    {"GetInCurrentLocationFormList", {"FormList"}},
    {"GetInFaction", {"Faction"}},
    {"GetInIronSights", {"Reference"}},
    {"GetInSameCell", {"Reference"}},
    {"GetInSharedCrimeFaction", {"Reference"}},
    {"GetInWorldspace", {"WorldSpace"}},
    {"GetInZone", {"EncounterZone"}},
    {"GetInfamy", {"FactionNull"}},
    {"GetInfamyNonViolent", {"FactionNull"}},
    {"GetInfamyViolent", {"FactionNull"}},
    {"GetIsAliasRef", {"Alias"}},
    {"GetIsAlignment", {"Alignment"}},
    {"GetIsClass", {"Class"}},
    {"GetIsClassDefault", {"Class"}},
    {"GetIsCreatureType", {"Integer"}},
    {"GetIsCrimeFaction", {"Faction"}},
    {"GetIsCurrentPackage", {"Package"}},
    {"GetIsCurrentWeather", {"Weather"}},
    {"GetIsEditorLocation", {"Location"}},
    {"GetIsEditorLocationAlias", {"Alias"}},
    {"GetIsID", {"BaseObject"}},
    {"GetIsObjectType", {"FormType"}},
    {"GetIsRace", {"Race"}},
    {"GetIsReference", {"Reference"}},
    {"GetIsSex", {"Sex"}},
    {"GetIsUsedItem", {"BaseObject"}},
    {"GetIsUsedItemEquipType", {"EquipType"}},
    {"GetIsUsedItemType", {"FormType"}},
    {"GetIsVoiceType", {"VoiceType"}},
    {"GetItemCount", {"BaseObject"}},
    {"GetKeywordDataForAlias", {"Alias", "Keyword"}},
    {"GetKeywordDataForCurrentLocation", {"Keyword"}},
    {"GetKeywordDataForLocation", {"Location", "Keyword"}},
    {"GetKeywordItemCount", {"Keyword"}},
    {"GetLineOfSight", {"Reference"}},
    {"GetLocationAliasCleared", {"Alias"}},
    {"GetLocationAliasRefTypeAliveCount", {"Alias", "LocationRefType"}},
    {"GetLocationAliasRefTypeDeadCount", {"Alias", "LocationRefType"}},
    {"GetLocationCleared", {"Location"}},
    {"GetNumericPackageData", {"Packdata"}},
    {"GetPCEnemyofFaction", {"Faction"}},
    {"GetPCExpelled", {"Faction"}},
    {"GetPCFactionAttack", {"Faction"}},
    {"GetPCFactionMurder", {"Faction"}},
    {"GetPCMiscStat", {"MiscStat"}},
    {"GetPathingCurrentSpeedAngle", {"Axis"}},
    {"GetPathingTargetAngleOffset", {"Axis"}},
    {"GetPathingTargetOffset", {"Axis"}},
    {"GetPathingTargetSpeedAngle", {"Axis"}},
    {"GetPermanentValue", {"ActorValue"}},
    {"GetPlayerControlsDisabled", {"Integer", "Integer", "Integer"}},
    {"GetPos", {"Axis"}},
    {"GetQuestCompleted", {"Quest"}},
    {"GetQuestRunning", {"Quest"}},
    {"GetRefTypeAliveCount", {"Location", "LocationRefType"}},
    {"GetRefTypeDeadCount", {"Location", "LocationRefType"}},
    {"GetRelationshipRank", {"Actor"}},
    {"GetRelativeAngle", {"Reference", "Axis"}},
    {"GetSceneActionPercent", {"Scene", "Integer"}},
    {"GetShouldAttack", {"Actor"}},
    {"GetShouldHelp", {"Actor"}},
    {"GetSpellUsageNum", {"EffectItem"}},
    {"GetStage", {"Quest"}},
    {"GetStageDone", {"Quest", "QuestStage"}},
    {"GetStartingAngle", {"Axis"}},
    {"GetStartingPos", {"Axis"}},
    {"GetStolenItemValue", {"Faction"}},
    {"GetStolenItemValueNoCrime", {"Faction"}},
    {"GetTalkedToPCParam", {"Actor"}},
    {"GetTargetHeight", {"Reference"}},
    {"GetThreatRatio", {"Actor"}},
    {"GetVATSBackAreaFree", {"Reference"}},
    {"GetVATSBackLeftAreaFree", {"Reference"}},
    {"GetVATSBackLeftTargetVisible", {"Reference"}},
    {"GetVATSBackRightAreaFree", {"Reference"}},
    {"GetVATSBackRightTargetVisible", {"Reference"}},
    {"GetVATSBackTargetVisible", {"Reference"}},
    {"GetVATSFrontAreaFree", {"Reference"}},
    {"GetVATSFrontTargetVisible", {"Reference"}},
    {"GetVATSLeftAreaFree", {"Reference"}},
    {"GetVATSLeftTargetVisible", {"Reference"}},
    {"GetVATSRightAreaFree", {"Reference"}},
    {"GetVATSRightMinusLeftAreaFree", {"Reference"}},
    {"GetVATSRightTargetVisible", {"Reference"}},
    {"GetVATSTargetLimbVisible", {"Reference"}},
    {"GetVATSValue", {"Integer", "Integer"}},
    {"GetVMQuestVariable", {"Quest", "String"}},
    {"GetVMScriptVariable", {"String", "String"}},
    {"GetValue", {"ActorValue"}},
    {"GetValuePercent", {"ActorValue"}},
    {"GetVelocity", {"Axis"}},
    {"GetWithinActivateDistance", {"Reference"}},
    {"GetWithinDistance", {"Reference", "Float"}},
    {"GetWithinPackageLocation", {"Packdata"}},
    {"GetWorkshopObjectCount", {"BaseObject"}},
    {"GetWorkshopResourceDamage", {"ActorValue"}},
    {"HasActorRefOwner", {"Actor"}},
    {"HasAssociationType", {"Actor", "AssociationType"}},
    {"HasAssociationTypeAny", {"AssociationType"}},
    {"HasBoundWeaponEquipped", {"CastingSource"}},
    {"HasEquippedSpell", {"CastingSource"}},
    {"HasFamilyRelationship", {"Actor"}},
    {"HasKey", {"Reference"}},
    {"HasKeyword", {"Keyword"}},
    {"HasLinkedRef", {"Keyword"}},
    {"HasMagicEffect", {"BaseEffect"}},
    {"HasMagicEffectKeyword", {"Keyword"}},
    {"HasParentRelationship", {"Actor"}},
    {"HasPerk", {"Perk"}},
    {"HasRefType", {"LocationRefType"}},
    {"HasSameEditorLocationAsRef", {"Reference", "Keyword"}},
    {"HasSameEditorLocationAsRefAlias", {"Alias", "Keyword"}},
    {"HasSharedPowerGrid", {"Reference"}},
    {"HasSpell", {"EffectItem"}},
    {"HasVMScript", {"String"}},
    {"HasValidRumorTopic", {"Quest"}},
    {"IsActorRefOwner", {"Actor"}},
    {"IsAttackType", {"Keyword"}},
    {"IsCellOwner", {"Cell", "Owner"}},
    {"IsCloserToAThanB", {"Reference", "Reference"}},
    {"IsCombatTarget", {"Actor"}},
    {"IsCurrentFurnitureObj", {"Furniture"}},
    {"IsCurrentFurnitureRef", {"Reference"}},
    {"IsCurrentSpell", {"EffectItem", "CastingSource"}},
    {"IsFurnitureEntryType", {"BaseObject"}},
    {"IsFurnitureExitType", {"BaseObject"}},
    {"IsHostileToActor", {"Actor"}},
    {"IsInCombat", {"Integer"}},
    {"IsInCriticalStage", {"CriticalStage"}},
    {"IsInList", {"FormList"}},
    {"IsInSameCurrentLocationAsRef", {"Reference", "Keyword"}},
    {"IsInSameCurrentLocationAsRefAlias", {"Alias", "Keyword"}},
    {"IsInSameSpace", {"Reference"}},
    {"IsKiller", {"Actor"}},
    {"IsKillerObject", {"FormList"}},
    {"IsLastIdlePlayed", {"IdleForm"}},
    {"IsLimbGone", {"Integer"}},
    {"IsLinkedTo", {"Reference", "Keyword"}},
    {"IsLocationAliasLoaded", {"Alias"}},
    {"IsLocationLoaded", {"Location"}},
    {"IsNullPackageData", {"Packdata"}},
    {"IsOwnedBy", {"Actor"}},
    {"IsOwner", {"Owner"}},
    {"IsPlayerActionActive", {"Integer"}},
    {"IsPlayerGrabbedRef", {"Reference"}},
    {"IsPlayerInRegion", {"Region"}},
    {"IsPlayerListening", {"Float"}},
    {"IsSceneActionComplete", {"Scene", "Integer"}},
    {"IsScenePlaying", {"Scene"}},
    {"IsSeatOccupied", {"Keyword"}},
    {"IsSpellTarget", {"EffectItem"}},
    {"IsTalkingActivatorActor", {"Actor"}},
    {"IsWardState", {"WardState"}},
    {"IsWarningAbout", {"FormList"}},
    {"IsWeaponInList", {"FormList"}},
    {"IsWeaponSkillType", {"ActorValue"}},
    {"LastCrippledCondition", {"ActorValue"}},
    {"LocationAliasHasKeyword", {"Alias", "Keyword"}},
    {"LocationAliasIsLocation", {"Alias", "Location"}},
    {"LocationHasKeyword", {"Keyword"}},
    {"LocationHasRefType", {"LocationRefType"}},
    {"MenuMode", {"Integer"}},
    {"ModdedItemHasKeyword", {"Keyword"}},
    {"MovementIdleMatches", {"Integer", "Integer"}},
    {"ObjectTemplateItem_HasKeyword", {"Keyword"}},
    {"ObjectTemplateItem_HasUniqueKeyword", {"Keyword"}},
    {"PlayerKnows", {"BaseObject"}},
    {"SameFaction", {"Actor"}},
    {"SameRace", {"Actor"}},
    {"SameSex", {"Actor"}},
    {"ShouldAttackKill", {"Actor"}},
    {"SpellHasCastingPerk", {"Perk"}},
    {"SpellHasKeyword", {"CastingSource", "Keyword"}},
    {"WornApparelHasKeywordCount", {"Keyword"}},
    {"WornCoversBipedSlot", {"Integer"}},
    {"WornHasKeyword", {"Keyword"}},
    {"WouldBeStealing", {"Reference"}},
    {"WouldRefuseCommand", {"Reference"}},
};

}  // namespace

std::size_t ConditionFunctions::count() { return kTable.size(); }

const std::vector<std::pair<std::string, std::vector<std::string>>>&
ConditionFunctions::table() {
    return kTable;
}

std::vector<std::string> ConditionFunctions::paramsOf(std::string_view function) {
    const auto wanted = toLower(function);
    for (const auto& [name, params] : table()) {
        if (toLower(name) == wanted) return params;
    }
    return {};
}

std::pair<std::string, std::string> ConditionFunctions::editorFor(
    std::string_view paramType) {
    // Port of the EditorFor switch; the default is a bare number.
    struct Row { const char* type; const char* kinds; };
    static const Row kRecords[] = {
        {"Actor", "PlacedNpc"},
        {"ActorBase", "Npc"},
        {"ActorValue", "ActorValueInformation"},
        {"AssociationType", "AssociationType"},
        {"BaseEffect", "MagicEffect"},
        {"BaseObject", ""},
        {"Cell", "Cell"},
        {"Class", "Class"},
        {"DamageType", "DamageType"},
        {"EffectItem", "Ingestible,ObjectEffect,Ingredient,Spell"},
        {"EncounterZone", "EncounterZone"},
        {"EquipType", "EquipType"},
        {"EventData", "Location,Keyword,FormList"},
        {"Faction", "Faction"},
        {"FactionNull", "Faction"},
        {"FormList", "FormList"},
        {"Furniture", "Furniture"},
        {"Global", "Global"},
        {"IdleForm", "IdleAnimation"},
        {"Keyword", "Keyword"},
        {"Location", "Location"},
        {"LocationRefType", "LocationReferenceType"},
        {"Owner", "Faction,Npc"},
        {"Package", "Package"},
        {"Perk", "Perk"},
        {"Quest", "Quest"},
        {"Race", "Race"},
        {"Reference", "PlacedObject,PlacedNpc"},
        {"Region", "Region"},
        {"Scene", "Scene"},
        {"VoiceType", "VoiceType"},
        {"Weather", "Weather"},
        {"WorldSpace", "Worldspace"},
    };
    for (const auto& row : kRecords) {
        if (toLower(paramType) == toLower(row.type)) {
            return {std::string("record"), row.kinds};
        }
    }
    if (toLower(paramType) == "string" || toLower(paramType) == "event") {
        return {"text", ""};
    }
    return {"number", ""};
}

std::string ConditionFunctions::spaced(std::string_view pascal) {
    std::string out;
    out.reserve(pascal.size() + 4);
    for (std::size_t i = 0; i < pascal.size(); i++) {
        if (i > 0 && std::isupper(static_cast<unsigned char>(pascal[i])) &&
            !std::isupper(static_cast<unsigned char>(pascal[i - 1]))) {
            out.push_back(' ');
        }
        out.push_back(pascal[i]);
    }
    return out;
}

std::string ConditionFunctions::asJson() {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, params] : table()) {
        auto& entry = out[name];
        entry = nlohmann::json::array();
        for (const auto& param : params) {
            const auto [kind, types] = editorFor(param);
            nlohmann::json descriptor = {
                {"label", spaced(param)},
                {"kind", kind},
                {"types", types},
            };
            entry.push_back(std::move(descriptor));
        }
    }
    return out.dump();
}

std::string conditionFunctionNames() {
    // The Condition.Function enum names, case-insensitively sorted like
    // Enum.GetNames<Condition.Function>().OrderBy(n => n, OrdinalIgnoreCase).
    // Generated from the Mutagen enum (479 members); regenerate with the
    // extraction script, never hand-edit.
    static const char* kNames[] = {
#include "fo4ide/plugin/condition_function_names.inc"
    };
    nlohmann::json out = nlohmann::json::array();
    for (const auto* name : kNames) out.push_back(name);
    return out.dump();
}

std::string conditionRunOnNames() {
    // The Condition.RunOnType enum names in declaration order.
    static const char* kNames[] = {
        "Subject",
        "Target",
        "Reference",
        "CombatTarget",
        "LinkedReference",
        "QuestAlias",
        "PackageData",
        "EventData",
        "CommandTarget",
        "EventCameraRef",
        "MyKiller",
    };
    nlohmann::json out = nlohmann::json::array();
    for (const auto* name : kNames) out.push_back(name);
    return out.dump();
}

// Port of the Condition.Function enum index<->name mapping. The table is
// generated from the Mutagen enum and is kept as {name, index} pairs.
namespace {
struct ConditionFunctionEntry {
    const char* name;
    std::uint16_t index;
};
constexpr ConditionFunctionEntry kFunctionIndex[] = {
#include "fo4ide/plugin/condition_function_index.inc"
};
constexpr std::pair<std::string_view, std::uint32_t> kRunOnValues[] = {
    {"Subject", 0}, {"Target", 1}, {"Reference", 2}, {"CombatTarget", 3},
    {"LinkedReference", 4}, {"QuestAlias", 5}, {"PackageData", 6},
    {"EventData", 7}, {"CommandTarget", 8}, {"EventCameraRef", 9},
    {"MyKiller", 10},
};
}  // namespace

std::string functionName(std::uint16_t index) {
    for (const auto& entry : kFunctionIndex) {
        if (entry.index == index) return entry.name;
    }
    return {};
}

std::optional<std::uint16_t> functionIndex(std::string_view name) {
    const auto needle = toLower(name);
    for (const auto& entry : kFunctionIndex) {
        if (toLower(entry.name) == needle) return entry.index;
    }
    return std::nullopt;
}

std::string runOnName(std::uint32_t value) {
    for (const auto& [name, v] : kRunOnValues) {
        if (v == value) return std::string{name};
    }
    return {};
}

std::optional<std::uint32_t> runOnValue(std::string_view name) {
    const auto needle = toLower(name);
    for (const auto& [n, v] : kRunOnValues) {
        if (toLower(n) == needle) return v;
    }
    return std::nullopt;
}

}  // namespace fo4ide::plugin
