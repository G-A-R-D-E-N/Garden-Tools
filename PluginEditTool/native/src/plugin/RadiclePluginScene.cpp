#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"

#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4/forms/scene.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/scene_writer.hpp>
#include <fo4/schema/condition_scopes.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using namespace questinternal;
using fo4::forms::SceneAuthorStatus;
using fo4::forms::SceneSpec;

namespace {

constexpr auto k_scen = fo4::Signature::from_string("SCEN");

std::string scalarText(std::uint16_t value) { return std::to_string(static_cast<std::uint32_t>(value)); }
std::string scalarText(std::uint32_t value) { return std::to_string(value); }
std::string scalarText(std::int32_t value) { return std::to_string(value); }
std::string scalarText(float value) { return std::to_string(value); }

std::optional<Error> requireObject(const nlohmann::json& value, std::string_view label) {
    if (value.is_object()) return std::nullopt;
    return Error{"InvalidSceneArgument", std::string(label) + " must be a JSON object"};
}

std::optional<Error> requireKnownKeys(const nlohmann::json& object,
                                      bool (*known)(std::string_view),
                                      std::string_view label) {
    if (auto error = requireObject(object, label)) return error;
    return rejectUnknownKeys(object, known, label, "InvalidSceneArgument");
}

std::optional<Error> textField(const nlohmann::json& object, const char* key, std::string& out) {
    std::optional<std::string> parsed;
    if (!questOptionalString(object, key, parsed)) {
        return Error{"InvalidSceneArgument", std::string(key) + " must be a string"};
    }
    out = parsed.value_or(std::string{});
    if (!fo4::text::utf8_to_windows1252(out)) {
        return Error{"InvalidSceneArgument", std::string(key) + " has characters that Windows-1252 cannot hold"};
    }
    return std::nullopt;
}

template <class T>
std::optional<Error> unsignedField(const nlohmann::json& object, const char* key, T defaultValue, T& out) {
    std::uint32_t parsed = 0;
    if (!questUnsigned(object, key, defaultValue, parsed) ||
        static_cast<std::uint32_t>(static_cast<T>(parsed)) != parsed) {
        return Error{"InvalidSceneArgument", std::string(key) + " must be an unsigned integer in range"};
    }
    out = static_cast<T>(parsed);
    return std::nullopt;
}

std::optional<Error> int32Field(const nlohmann::json& object, const char* key,
                                std::int32_t defaultValue, std::int32_t& out) {
    std::optional<std::int32_t> parsed;
    if (!questInt32(object, key, parsed)) {
        return Error{"InvalidSceneArgument", std::string(key) + " must be a 32-bit integer"};
    }
    out = parsed.value_or(defaultValue);
    return std::nullopt;
}

std::optional<Error> encodedFor(const fo4::Plugin& target, const fo4::FormKey& key,
                                std::string_view label) {
    try {
        fo4::encode_plugin_form_id(key, fo4::plugin_form_id_context(target));
        return std::nullopt;
    } catch (const std::exception&) {
        return Error{"InvalidFormKey", std::string(label) + " is not reachable from this plugin's masters"};
    }
}

std::optional<Error> keyField(const nlohmann::json& object, const char* key,
                              const fo4::Plugin& target, std::optional<fo4::FormKey>& out) {
    if (!jsonObjectPresent(object, key)) return std::nullopt;
    if (!object.at(key).is_string()) {
        return Error{"InvalidSceneArgument", std::string(key) + " must be a FormKey string"};
    }
    auto parsed = support::formKeyArgument(object.at(key).get<std::string>());
    if (!parsed) return parsed.error();
    if (auto error = encodedFor(target, *parsed, key)) return error;
    out = *parsed;
    return std::nullopt;
}

std::optional<Error> indexAndCount(std::size_t index, std::size_t count, std::string_view label) {
    if (index > count) {
        return Error{"SceneIndexOutOfRange", std::string(label) + " index " + std::to_string(index) +
                                                " is past the end of " + std::to_string(count) +
                                                " scene group members"};
    }
    return std::nullopt;
}

std::optional<Error> sceneAuthorFailure(SceneAuthorStatus status, std::string_view subject) {
    switch (status) {
    case SceneAuthorStatus::quest_missing:
        return Error{"RecordNotFound", "QUST record is not present: " + std::string(subject)};
    case SceneAuthorStatus::not_local:
        return Error{"RecordNotLocal", "Record is not owned by this plugin: " + std::string(subject)};
    case SceneAuthorStatus::invalid_argument:
        return Error{"InvalidSceneArgument", "Scene argument is invalid for " + std::string(subject)};
    case SceneAuthorStatus::undecodable:
        return Error{"RecordUndecodable", "Record is undecodable and cannot be edited: " + std::string(subject)};
    case SceneAuthorStatus::structure_invalid:
        return Error{"SceneStructureInvalid", "Scene structure does not allow this edit: " + std::string(subject)};
    case SceneAuthorStatus::ok:
        break;
    }
    return Error{"SceneAuthorFailed", "Scene authoring failed for " + std::string(subject)};
}

std::optional<Error> scalarFailure(fo4::ScalarMutationStatus status, std::string_view subject) {
    switch (status) {
    case fo4::ScalarMutationStatus::updated:
    case fo4::ScalarMutationStatus::unchanged:
        return std::nullopt;
    case fo4::ScalarMutationStatus::record_missing:
        return Error{"SceneMissing", "SCEN record is not present: " + std::string(subject)};
    case fo4::ScalarMutationStatus::subrecord_missing:
        return Error{"SceneStructureInvalid", "the scene does not hold that member: " + std::string(subject)};
    default:
        break;
    }
    return Error{"SetSceneFieldFailed", "Scene field could not be written: " + std::string(subject)};
}

std::optional<Error> groupFailure(fo4::SubrecordGroupMutationStatus status, std::string_view subject) {
    switch (status) {
    case fo4::SubrecordGroupMutationStatus::updated:
    case fo4::SubrecordGroupMutationStatus::unchanged:
        return std::nullopt;
    case fo4::SubrecordGroupMutationStatus::record_missing:
        return Error{"SceneMissing", "SCEN record is not present: " + std::string(subject)};
    case fo4::SubrecordGroupMutationStatus::index_out_of_range:
        return Error{"SceneIndexOutOfRange", "scene group index is out of range: " + std::string(subject)};
    case fo4::SubrecordGroupMutationStatus::subrecord_missing:
        return Error{"SceneStructureInvalid", "the scene does not hold that member: " + std::string(subject)};
    default:
        break;
    }
    return Error{"SceneGroupMutationFailed", "Scene group could not be written: " + std::string(subject)};
}

std::optional<Error> linkFailure(fo4::FormLinkFieldMutationStatus status, std::string_view subject) {
    switch (status) {
    case fo4::FormLinkFieldMutationStatus::updated:
    case fo4::FormLinkFieldMutationStatus::unchanged:
        return std::nullopt;
    case fo4::FormLinkFieldMutationStatus::record_missing:
        return Error{"SceneMissing", "SCEN record is not present: " + std::string(subject)};
    case fo4::FormLinkFieldMutationStatus::record_mismatch:
        return Error{"RecordTypeMismatch", "Scene edit requires a SCEN record: " + std::string(subject)};
    case fo4::FormLinkFieldMutationStatus::subrecord_missing:
        return Error{"SceneStructureInvalid", "the scene does not hold that link: " + std::string(subject)};
    default:
        break;
    }
    return Error{"SetSceneLinkFailed", "Scene link could not be written: " + std::string(subject)};
}

std::optional<Error> ensureMember(fo4::Plugin& plugin, const fo4::FormKey& scene,
                                  std::string_view path, std::size_t group) {
    const auto ensured = fo4::ensure_scene_member(plugin, scene, path, group);
    if (ensured == fo4::ScalarMutationStatus::record_missing) {
        return Error{"SceneMissing", "SCEN record is not present"};
    }
    if (ensured == fo4::ScalarMutationStatus::subrecord_missing) {
        return Error{"SceneStructureInvalid", std::string(path) + " cannot be held by this scene member"};
    }
    return std::nullopt;
}

std::optional<Error> applyScalar(fo4::Plugin& plugin, const fo4::FormKey& scene,
                                 std::string_view path, std::size_t group, const std::string& value) {
    if (auto error = ensureMember(plugin, scene, path, group)) return error;
    return scalarFailure(fo4::set_scene_scalar(plugin, scene, path, group, value), path);
}

std::optional<Error> applyTailLink(fo4::Plugin& plugin, const fo4::FormKey& scene,
                                   std::string_view path, const fo4::FormKey& target) {
    if (auto error = ensureMember(plugin, scene, path, 0)) return error;
    return linkFailure(fo4::set_scene_formlink(plugin, scene, path, target), path);
}

std::optional<Error> applyActionLink(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                     std::string_view path, const fo4::FormKey& target) {
    if (auto error = ensureMember(plugin, scene, path, index)) return error;
    return linkFailure(fo4::set_scene_action_formlink(plugin, scene, index, path, target), path);
}

struct PayloadField {
    const char* key;
    const char* path;
    bool link;
};

constexpr PayloadField k_payloadFields[] = {
    {"topic", "SCEN\\Action\\Topic", true},
    {"dialogueSubtype", "SCEN\\Action\\DialogueSubtype", true},
    {"animArchType", "SCEN\\Action\\AnimArchType", true},
    {"audioOutputOverride", "SCEN\\Action\\AudioOutputOverride", true},
    {"startSceneTopic", "SCEN\\Action\\StartSceneTopic", true},
    {"playSound", "SCEN\\Action\\PlaySound", true},
    {"playerPositiveResponse", "SCEN\\Action\\PlayerPositiveResponse", true},
    {"playerNegativeResponse", "SCEN\\Action\\PlayerNegativeResponse", true},
    {"playerNeutralResponse", "SCEN\\Action\\PlayerNeutralResponse", true},
    {"playerQuestionResponse", "SCEN\\Action\\PlayerQuestionResponse", true},
    {"npcPositiveResponse", "SCEN\\Action\\NpcPositiveResponse", true},
    {"npcNegativeResponse", "SCEN\\Action\\NpcNegativeResponse", true},
    {"npcNeutralResponse", "SCEN\\Action\\NpcNeutralResponse", true},
    {"npcQuestionResponse", "SCEN\\Action\\NpcQuestionResponse", true},
    {"loopingMax", "SCEN\\Action\\LoopingMax", false},
    {"loopingMin", "SCEN\\Action\\LoopingMin", false},
    {"cameraFovOnPlayerCamera", "SCEN\\Action\\CameraFovOnPlayerCamera", false},
    {"cameraRateOfChange", "SCEN\\Action\\CameraRateOfChange", false},
    {"emotionType", "SCEN\\Action\\EmotionType", false},
    {"emotionValue", "SCEN\\Action\\EmotionValue", false},
    {"timerMax", "SCEN\\Action\\TimerMax", false},
    {"timerMin", "SCEN\\Action\\TimerMin", false},
    {"dialogueTargetActor", "SCEN\\Action\\DialogueTargetActor", false},
};

const PayloadField* payloadField(std::string_view key) {
    for (const auto& field : k_payloadFields) {
        if (field.key == key) return &field;
    }
    return nullptr;
}

std::optional<Error> applyPayloadField(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                       const PayloadField& field, const nlohmann::json& value,
                                       std::string_view key) {
    if (field.link) {
        if (!value.is_string()) {
            return Error{"InvalidSceneArgument", std::string(key) + " must be a FormKey string"};
        }
        auto target = support::formKeyArgument(value.get<std::string>());
        if (!target) return target.error();
        return applyActionLink(plugin, scene, index, field.path, *target);
    }
    if (value.is_number_integer()) {
        const auto raw = value.get<std::int64_t>();
        if (raw < 0) {
            return Error{"InvalidSceneArgument", std::string(key) + " must not be negative"};
        }
        return applyScalar(plugin, scene, field.path, index, std::to_string(raw));
    }
    if (value.is_number_float()) {
        const auto raw = value.get<double>();
        return applyScalar(plugin, scene, field.path, index, std::to_string(static_cast<float>(raw)));
    }
    return Error{"InvalidSceneArgument", std::string(key) + " must be a number"};
}

std::optional<Error> applyActionHeader(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                       const nlohmann::json& spec, bool& changed) {
    if (jsonObjectPresent(spec, "name")) {
        std::string name;
        if (auto error = textField(spec, "name", name)) return error;
        const auto status = fo4::set_scene_action_name(plugin, scene, index, name);
        if (status == fo4::ScalarMutationStatus::record_missing) {
            return Error{"SceneMissing", "SCEN record is not present"};
        }
        if (status == fo4::ScalarMutationStatus::subrecord_missing) {
            return Error{"SceneIndexOutOfRange", "scene action index is out of range"};
        }
        changed = true;
    }
    if (jsonObjectPresent(spec, "actorAlias")) {
        std::int32_t value = -1;
        if (auto error = int32Field(spec, "actorAlias", -1, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Action\\ActorAlias", index,
                                     scalarText(value))) {
            return error;
        }
        changed = true;
    }
    if (jsonObjectPresent(spec, "flags")) {
        std::uint32_t value = 0;
        if (auto error = unsignedField<std::uint32_t>(spec, "flags", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Action\\Flags", index, scalarText(value))) {
            return error;
        }
        changed = true;
    }
    if (jsonObjectPresent(spec, "startPhase")) {
        std::uint32_t value = 0;
        if (auto error = unsignedField<std::uint32_t>(spec, "startPhase", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Action\\StartPhase", index,
                                     scalarText(value))) {
            return error;
        }
        changed = true;
    }
    if (jsonObjectPresent(spec, "endPhase")) {
        std::uint32_t value = 0;
        if (auto error = unsignedField<std::uint32_t>(spec, "endPhase", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Action\\EndPhase", index, scalarText(value))) {
            return error;
        }
        changed = true;
    }
    return std::nullopt;
}

std::optional<Error> applyPhaseConditions(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                          std::string_view member, const nlohmann::json& conditions,
                                          const EditorIdResolver& resolveEditorId, std::string& failure) {
    const auto path = "Phases[" + std::to_string(index) + "]." + std::string(member);
    const auto scoped = fo4::schema::scoped_condition_path(k_scen, path);
    if (!scoped) {
        return Error{"SceneStructureInvalid", "the scene does not expose that condition scope"};
    }
    const auto* record = fo4::find_record(plugin, scene);
    if (record == nullptr) return Error{"SceneMissing", "SCEN record is not present"};
    const auto existing = fo4::schema::read_scoped_conditions(*record, scoped->descriptor, index);
    if (!existing.ok) {
        return Error{"SceneStructureInvalid", "the scene conditions cannot be read for " + path};
    }
    auto built = buildConditions(conditions, plugin, existing.conditions, resolveEditorId, failure);
    if (!built) return Error{"InvalidConditionsJson", failure};
    const auto status = fo4::schema::set_record_scoped_conditions(
        plugin, scene, scoped->descriptor, index, *built);
    if (status == fo4::schema::ScopedConditionStatus::updated ||
        status == fo4::schema::ScopedConditionStatus::unchanged) {
        return std::nullopt;
    }
    return Error{"SetSceneConditionsFailed", "scene phase conditions could not be written for " + path};
}

std::optional<Error> applyPhaseSpec(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                    const nlohmann::json& spec, const EditorIdResolver& resolveEditorId,
                                    std::string& failure) {
    if (auto error = requireKnownKeys(spec, [](std::string_view key) {
            return key == "name" || key == "editorWidth" || key == "flags" || key == "questStageOnStart" ||
                   key == "questStageOnCompletion" || key == "startConditions" || key == "completionConditions";
        }, "scene phase")) {
        return error;
    }
    if (jsonObjectPresent(spec, "name")) {
        std::string name;
        if (auto error = textField(spec, "name", name)) return error;
        const auto status = fo4::set_scene_phase_name(plugin, scene, index, name);
        if (status == fo4::ScalarMutationStatus::record_missing) {
            return Error{"SceneMissing", "SCEN record is not present"};
        }
        if (status == fo4::ScalarMutationStatus::subrecord_missing) {
            return Error{"SceneIndexOutOfRange", "scene phase index is out of range"};
        }
    }
    if (jsonObjectPresent(spec, "editorWidth")) {
        std::uint32_t value = 0;
        if (auto error = unsignedField<std::uint32_t>(spec, "editorWidth", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Phase\\EditorWidth", index, scalarText(value))) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "flags")) {
        std::uint16_t value = 0;
        if (auto error = unsignedField<std::uint16_t>(spec, "flags", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Phase\\Flags", index, scalarText(value))) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "questStageOnStart")) {
        std::int32_t value = 0;
        if (auto error = int32Field(spec, "questStageOnStart", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Phase\\QuestStageOnStart", index,
                                     scalarText(value))) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "questStageOnCompletion")) {
        std::int32_t value = 0;
        if (auto error = int32Field(spec, "questStageOnCompletion", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Phase\\QuestStageOnCompletion", index,
                                     scalarText(value))) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "startConditions")) {
        if (auto error = applyPhaseConditions(plugin, scene, index, "Start Conditions",
                                              spec.at("startConditions"), resolveEditorId, failure)) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "completionConditions")) {
        if (auto error = applyPhaseConditions(plugin, scene, index, "Completion Conditions",
                                              spec.at("completionConditions"), resolveEditorId, failure)) {
            return error;
        }
    }
    return std::nullopt;
}

std::optional<Error> applyActorSpec(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                    const nlohmann::json& spec) {
    if (auto error = requireKnownKeys(spec, [](std::string_view key) {
            return key == "alias" || key == "flags" || key == "behaviorFlags";
        }, "scene actor")) {
        return error;
    }
    if (jsonObjectPresent(spec, "alias")) {
        std::int32_t value = -1;
        if (auto error = int32Field(spec, "alias", -1, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Actor\\Alias", index, scalarText(value))) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "flags")) {
        std::uint32_t value = 0;
        if (auto error = unsignedField<std::uint32_t>(spec, "flags", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Actor\\Flags", index, scalarText(value))) {
            return error;
        }
    }
    if (jsonObjectPresent(spec, "behaviorFlags")) {
        std::uint32_t value = 0;
        if (auto error = unsignedField<std::uint32_t>(spec, "behaviorFlags", 0, value)) return error;
        if (auto error = applyScalar(plugin, scene, "SCEN\\Actor\\BehaviorFlags", index, scalarText(value))) {
            return error;
        }
    }
    return std::nullopt;
}

std::optional<Error> applyActionSpec(fo4::Plugin& plugin, const fo4::FormKey& scene, std::size_t index,
                                     const nlohmann::json& spec) {
    if (auto error = requireKnownKeys(spec, [](std::string_view key) {
            if (key == "name" || key == "actorAlias" || key == "flags" || key == "startPhase" ||
                key == "endPhase") {
                return true;
            }
            return payloadField(key) != nullptr;
        }, "scene action")) {
        return error;
    }
    bool changed = false;
    if (auto error = applyActionHeader(plugin, scene, index, spec, changed)) return error;
    for (const auto& field : k_payloadFields) {
        if (!jsonObjectPresent(spec, field.key)) continue;
        if (auto error = applyPayloadField(plugin, scene, index, field, spec.at(field.key), field.key)) {
            return error;
        }
        changed = true;
    }
    if (!changed) {
        return Error{"InvalidSceneArgument", "scene action spec has no members to set"};
    }
    return std::nullopt;
}

std::optional<Error> unsignedArgument(const nlohmann::json& arguments, std::size_t index,
                                      std::string_view label, std::uint32_t& out) {
    const auto value = support::integerValue<std::uint32_t>(support::argumentAt(arguments, index));
    if (!value) return Error{"InvalidSceneArgument", std::string(label) + " must be an unsigned integer"};
    out = *value;
    return std::nullopt;
}

std::optional<Error> sceneKey(const nlohmann::json& arguments, std::size_t index, fo4::FormKey& out) {
    const auto text = questStringArg(arguments, index);
    if (text.empty()) return Error{"RecordMissing", "Record is empty"};
    auto parsed = support::formKeyArgument(text);
    if (!parsed) return parsed.error();
    out = *parsed;
    return std::nullopt;
}

}

Result<nlohmann::json> RadiclePluginBackend::createScene(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto questText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (questText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto quest = support::formKeyArgument(questText);
    if (!quest) return std::unexpected(quest.error());
    SceneSpec spec;
    const auto& document = support::argumentAt(arguments, 2);
    if (!document.is_null()) {
        if (auto error = requireKnownKeys(document, [](std::string_view key) {
                return key == "editorId" || key == "flags" || key == "index";
            }, "scene spec")) {
            return std::unexpected(*error);
        }
        if (auto error = textField(document, "editorId", spec.editor_id)) return std::unexpected(*error);
        if (auto error = unsignedField<std::uint32_t>(document, "flags", 0, spec.flags)) {
            return std::unexpected(*error);
        }
        if (auto error = unsignedField<std::uint32_t>(document, "index", 0, spec.index)) {
            return std::unexpected(*error);
        }
    }
    try {
        auto mutated = loaded->plugin;
        const auto created = fo4::forms::create_scene(mutated, *quest, spec);
        if (auto error = sceneAuthorFailure(created.status, questText)) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json{{"formKey", created.key.to_string()}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CreateSceneFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setScene(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    const auto& spec = support::argumentAt(arguments, 2);
    if (auto error = requireKnownKeys(spec, [](std::string_view key) {
            return key == "flags" || key == "parentQuest" || key == "templateScene" || key == "index";
        }, "scene spec")) {
        return std::unexpected(*error);
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        bool changed = false;
        if (jsonObjectPresent(spec, "flags")) {
            std::uint32_t value = 0;
            if (auto error = unsignedField<std::uint32_t>(spec, "flags", 0, value)) return std::unexpected(*error);
            if (auto error = applyScalar(mutated, scene, "SCEN\\Flags", 0, scalarText(value))) {
                return std::unexpected(*error);
            }
            changed = true;
        }
        if (jsonObjectPresent(spec, "index")) {
            std::uint32_t value = 0;
            if (auto error = unsignedField<std::uint32_t>(spec, "index", 0, value)) return std::unexpected(*error);
            if (auto error = applyScalar(mutated, scene, "SCEN\\Index", 0, scalarText(value))) {
                return std::unexpected(*error);
            }
            changed = true;
        }
        if (jsonObjectPresent(spec, "parentQuest")) {
            std::optional<fo4::FormKey> target;
            if (auto error = keyField(spec, "parentQuest", mutated, target)) return std::unexpected(*error);
            if (!target) return std::unexpected(Error{"InvalidSceneArgument", "parentQuest must be a FormKey"});
            if (auto error = applyTailLink(mutated, scene, "SCEN\\ParentQuest", *target)) {
                return std::unexpected(*error);
            }
            changed = true;
        }
        if (jsonObjectPresent(spec, "templateScene")) {
            std::optional<fo4::FormKey> target;
            if (auto error = keyField(spec, "templateScene", mutated, target)) return std::unexpected(*error);
            if (!target) return std::unexpected(Error{"InvalidSceneArgument", "templateScene must be a FormKey"});
            if (auto error = applyTailLink(mutated, scene, "SCEN\\TemplateScene", *target)) {
                return std::unexpected(*error);
            }
            changed = true;
        }
        if (!changed) return std::unexpected(Error{"InvalidSceneArgument", "scene spec has no members to set"});
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Set scene in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetSceneFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setScenePhase(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "phase index", index)) return std::unexpected(*error);
    const auto& spec = support::argumentAt(arguments, 3);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto& target = loaded->plugin;
        const EditorIdResolver resolveEditorId = [this, &target](std::string_view text) {
            return resolveDialogueEditorId(target, text);
        };
        std::string failure;
        if (auto error = applyPhaseSpec(mutated, scene, index, spec, resolveEditorId, failure)) {
            return std::unexpected(*error);
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Set scene phase in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetScenePhaseFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::addScenePhase(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "phase index", index)) return std::unexpected(*error);
    std::string name;
    const auto& document = support::argumentAt(arguments, 3);
    if (document.is_object()) {
        if (auto error = requireKnownKeys(document, [](std::string_view key) { return key == "name"; },
                                          "scene phase")) {
            return std::unexpected(*error);
        }
        if (auto error = textField(document, "name", name)) return std::unexpected(*error);
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto count = fo4::scene_group_count(mutated, scene, fo4::ScenePart::phase);
        if (!count) return std::unexpected(Error{"SceneMissing", "SCEN record is not present"});
        if (auto error = indexAndCount(index, *count, "phase")) return std::unexpected(*error);
        const auto status = fo4::insert_scene_phase(mutated, scene, index, name);
        if (auto error = groupFailure(status, "phase")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Added scene phase " + std::to_string(index) + " in " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AddScenePhaseFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeScenePhase(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "phase index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::remove_scene_phase(mutated, scene, index);
        if (auto error = groupFailure(status, "phase")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Removed scene phase " + std::to_string(index) + " from " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveScenePhaseFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::moveScenePhase(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t from = 0, to = 0;
    if (auto error = unsignedArgument(arguments, 2, "phase index", from)) return std::unexpected(*error);
    if (auto error = unsignedArgument(arguments, 3, "phase index", to)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::move_scene_phase(mutated, scene, from, to);
        if (auto error = groupFailure(status, "phase")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Moved scene phase " + std::to_string(from) + " to " + std::to_string(to) +
                              " in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"MoveScenePhaseFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setSceneActor(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "actor index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        if (auto error = applyActorSpec(mutated, scene, index, support::argumentAt(arguments, 3))) {
            return std::unexpected(*error);
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Set scene actor in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetSceneActorFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::addSceneActor(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "actor index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto count = fo4::scene_group_count(mutated, scene, fo4::ScenePart::actor);
        if (!count) return std::unexpected(Error{"SceneMissing", "SCEN record is not present"});
        if (auto error = indexAndCount(index, *count, "actor")) return std::unexpected(*error);
        const auto status = fo4::insert_scene_actor(mutated, scene, index);
        if (auto error = groupFailure(status, "actor")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Added scene actor " + std::to_string(index) + " in " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AddSceneActorFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeSceneActor(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "actor index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::remove_scene_actor(mutated, scene, index);
        if (auto error = groupFailure(status, "actor")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Removed scene actor " + std::to_string(index) + " from " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveSceneActorFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setSceneAction(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "action index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        if (auto error = applyActionSpec(mutated, scene, index, support::argumentAt(arguments, 3))) {
            return std::unexpected(*error);
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Set scene action in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetSceneActionFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::addSceneAction(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "action index", index)) return std::unexpected(*error);
    std::uint32_t type = 0;
    if (auto error = unsignedArgument(arguments, 3, "action type", type)) return std::unexpected(*error);
    if (type > 6) {
        return std::unexpected(Error{"InvalidSceneArgument", "action type must be between 0 and 6"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto count = fo4::scene_group_count(mutated, scene, fo4::ScenePart::action);
        if (!count) return std::unexpected(Error{"SceneMissing", "SCEN record is not present"});
        if (auto error = indexAndCount(index, *count, "action")) return std::unexpected(*error);
        const auto status = fo4::insert_scene_action(mutated, scene, index, static_cast<std::uint16_t>(type));
        if (auto error = groupFailure(status, "action")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Added scene action " + std::to_string(index) + " of type " +
                              std::to_string(type) + " in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"AddSceneActionFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeSceneAction(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t index = 0;
    if (auto error = unsignedArgument(arguments, 2, "action index", index)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::remove_scene_action(mutated, scene, index);
        if (auto error = groupFailure(status, "action")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Removed scene action " + std::to_string(index) + " from " + pluginName +
                              ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveSceneActionFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::moveSceneAction(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    fo4::FormKey scene;
    if (auto error = sceneKey(arguments, 1, scene)) return std::unexpected(*error);
    std::uint32_t from = 0, to = 0;
    if (auto error = unsignedArgument(arguments, 2, "action index", from)) return std::unexpected(*error);
    if (auto error = unsignedArgument(arguments, 3, "action index", to)) return std::unexpected(*error);

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::move_scene_action(mutated, scene, from, to);
        if (auto error = groupFailure(status, "action")) return std::unexpected(*error);
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Moved scene action " + std::to_string(from) + " to " + std::to_string(to) +
                              " in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"MoveSceneActionFailed", error.what()});
    }
}

}
