#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"

#include <fo4ide/plugin/ConditionCodec.hpp>

#include <fo4/forms/dialogue.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/text/encoding.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin {

using namespace questinternal;
using fo4::forms::DialogueAuthorStatus;
using fo4::forms::DialogueBranchSpec;
using fo4::forms::DialogueInfoPatch;
using fo4::forms::DialogueInfoSpec;
using fo4::forms::DialogueResponseSpec;
using fo4::forms::DialogueTopicSpec;

namespace {

std::optional<Error> requireObject(const nlohmann::json& value, std::string_view label) {
    if (value.is_object()) return std::nullopt;
    return Error{"InvalidDialogueArgument", std::string(label) + " must be a JSON object"};
}

std::optional<Error> requireKnownKeys(const nlohmann::json& object,
                                      bool (*known)(std::string_view),
                                      std::string_view label) {
    if (auto error = requireObject(object, label)) return error;
    return rejectUnknownKeys(object, known, label, "InvalidDialogueArgument");
}

std::optional<Error> textField(const nlohmann::json& object, const char* key, std::string& out) {
    std::optional<std::string> parsed;
    if (!questOptionalString(object, key, parsed)) {
        return Error{"InvalidDialogueArgument", std::string(key) + " must be a string"};
    }
    out = parsed.value_or(std::string{});
    if (!fo4::text::utf8_to_windows1252(out)) {
        return Error{"InvalidDialogueArgument", std::string(key) + " has characters that Windows-1252 cannot hold"};
    }
    return std::nullopt;
}

template <class T>
std::optional<Error> unsignedField(const nlohmann::json& object, const char* key, T defaultValue, T& out) {
    std::uint32_t parsed = 0;
    if (!questUnsigned(object, key, defaultValue, parsed) ||
        static_cast<std::uint32_t>(static_cast<T>(parsed)) != parsed) {
        return Error{"InvalidDialogueArgument", std::string(key) + " must be an unsigned integer in range"};
    }
    out = static_cast<T>(parsed);
    return std::nullopt;
}

template <class T>
std::optional<Error> optionalUnsignedField(const nlohmann::json& object, const char* key, std::optional<T>& out) {
    if (!jsonObjectPresent(object, key)) return std::nullopt;
    T value = 0;
    if (auto error = unsignedField<T>(object, key, 0, value)) return error;
    out = value;
    return std::nullopt;
}

std::optional<Error> int32Field(const nlohmann::json& object, const char* key,
                                std::int32_t defaultValue, std::int32_t& out) {
    std::optional<std::int32_t> parsed;
    if (!questInt32(object, key, parsed)) {
        return Error{"InvalidDialogueArgument", std::string(key) + " must be a 32-bit integer"};
    }
    out = parsed.value_or(defaultValue);
    return std::nullopt;
}

std::optional<Error> floatField(const nlohmann::json& object, const char* key,
                                float defaultValue, float& out) {
    if (!questFloat(object, key, defaultValue, out)) {
        return Error{"InvalidDialogueArgument", std::string(key) + " must be a finite number"};
    }
    return std::nullopt;
}

std::optional<Error> resetHoursField(const nlohmann::json& object, std::optional<std::uint16_t>& out) {
    if (!jsonObjectPresent(object, "resetHours")) return std::nullopt;
    float hours = 0.0f;
    if (!questFloat(object, "resetHours", 0.0f, hours)) {
        return Error{"InvalidDialogueArgument", "resetHours must be a finite number of hours"};
    }
    const auto raw = std::round(static_cast<double>(hours) * fo4::forms::k_info_reset_units_per_hour);
    if (hours < 0.0f || raw > std::numeric_limits<std::uint16_t>::max()) {
        return Error{"InvalidDialogueArgument", "resetHours must be between 0 and 24 hours"};
    }
    out = static_cast<std::uint16_t>(raw);
    return std::nullopt;
}

std::optional<fo4::RawFormId> encodedFor(const fo4::Plugin& target, const fo4::FormKey& key) {
    try {
        return fo4::encode_plugin_form_id(key, fo4::plugin_form_id_context(target));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<Error> keyField(const nlohmann::json& object, const char* key,
                              const fo4::Plugin& target, std::optional<fo4::FormKey>& out) {
    if (!jsonObjectPresent(object, key)) return std::nullopt;
    if (!object.at(key).is_string()) {
        return Error{"InvalidDialogueArgument", std::string(key) + " must be a FormKey string"};
    }
    auto parsed = support::formKeyArgument(object.at(key).get<std::string>());
    if (!parsed) return parsed.error();
    if (!encodedFor(target, *parsed)) {
        return Error{"InvalidFormKey", std::string(key) + " is not reachable from this plugin's masters"};
    }
    out = *parsed;
    return std::nullopt;
}

std::optional<Error> parseBranchSpec(const nlohmann::json& object, DialogueBranchSpec& out) {
    if (auto error = requireKnownKeys(object, [](std::string_view key) {
            return key == "editorId" || key == "category" || key == "flags";
        }, "dialogue branch")) {
        return error;
    }
    if (auto error = textField(object, "editorId", out.editor_id)) return error;
    if (auto error = unsignedField<std::uint32_t>(object, "category", 0, out.category)) return error;
    return unsignedField<std::uint32_t>(object, "flags", 0, out.flags);
}

std::optional<Error> parseTopicSpec(const nlohmann::json& object, const fo4::Plugin& target,
                                    DialogueTopicSpec& out) {
    if (auto error = requireKnownKeys(object, [](std::string_view key) {
            return key == "editorId" || key == "name" || key == "priority" || key == "branch" ||
                   key == "keyword" || key == "topicFlags" || key == "category" || key == "subtype" ||
                   key == "subtypeName";
        }, "dialogue topic")) {
        return error;
    }
    if (auto error = textField(object, "editorId", out.editor_id)) return error;
    if (auto error = textField(object, "name", out.name)) return error;
    if (auto error = floatField(object, "priority", 50.0f, out.priority)) return error;
    if (auto error = keyField(object, "branch", target, out.branch)) return error;
    if (auto error = keyField(object, "keyword", target, out.keyword)) return error;
    if (auto error = unsignedField<std::uint8_t>(object, "topicFlags", 0, out.topic_flags)) return error;
    if (auto error = unsignedField<std::uint8_t>(object, "category", 0, out.category)) return error;
    if (auto error = unsignedField<std::uint16_t>(object, "subtype", 0, out.subtype)) return error;
    std::string subtypeName;
    if (auto error = textField(object, "subtypeName", subtypeName)) return error;
    out.subtype_name = fo4::text::utf8_to_windows1252(subtypeName).value_or(std::string{});
    return std::nullopt;
}

std::optional<Error> parseResponseSpec(const nlohmann::json& object, const fo4::Plugin& target,
                                       DialogueResponseSpec& out) {
    if (auto error = requireKnownKeys(object, [](std::string_view key) {
            return key == "text" || key == "scriptNotes" || key == "emotion" || key == "sound" ||
                   key == "number" || key == "interruptPercent" || key == "cameraTargetAlias" ||
                   key == "cameraLocationAlias";
        }, "dialogue response")) {
        return error;
    }
    if (auto error = textField(object, "text", out.text)) return error;
    if (auto error = textField(object, "scriptNotes", out.script_notes)) return error;
    if (auto error = keyField(object, "emotion", target, out.emotion)) return error;
    if (auto error = keyField(object, "sound", target, out.sound)) return error;
    if (auto error = unsignedField<std::uint8_t>(object, "number", 1, out.number)) return error;
    if (auto error = unsignedField<std::uint16_t>(object, "interruptPercent", 0, out.interrupt_percent)) {
        return error;
    }
    if (auto error = int32Field(object, "cameraTargetAlias", -1, out.camera_target_alias)) return error;
    return int32Field(object, "cameraLocationAlias", -1, out.camera_location_alias);
}

std::optional<Error> parseInfoPatch(const nlohmann::json& object, const fo4::Plugin& target,
                                    std::span<const fo4::CtdaCondition> existing,
                                    const EditorIdResolver& resolveEditorId, DialogueInfoPatch& out) {
    if (auto error = requireKnownKeys(object, [](std::string_view key) {
            return key == "flags" || key == "resetHours" || key == "responses" || key == "conditions";
        }, "dialogue INFO")) {
        return error;
    }
    if (auto error = optionalUnsignedField<std::uint16_t>(object, "flags", out.flags)) return error;
    if (auto error = resetHoursField(object, out.reset_hours)) return error;
    if (jsonObjectPresent(object, "responses")) {
        const auto& responses = object.at("responses");
        if (!responses.is_array()) return Error{"InvalidDialogueArgument", "responses must be an array"};
        if (responses.size() > 255) {
            return Error{"InvalidDialogueArgument", "responses holds more than 255 entries"};
        }
        std::vector<DialogueResponseSpec> parsed;
        for (std::size_t index = 0; index < responses.size(); ++index) {
            const auto& element = responses.at(index);
            DialogueResponseSpec response;
            if (auto error = parseResponseSpec(element, target, response)) return error;
            if (!jsonObjectPresent(element, "number")) response.number = static_cast<std::uint8_t>(index + 1);
            parsed.push_back(std::move(response));
        }
        out.responses = std::move(parsed);
    }
    if (!jsonObjectPresent(object, "conditions")) return std::nullopt;
    std::string failure;
    auto conditions = buildConditions(object.at("conditions"), target, existing, resolveEditorId, failure);
    if (!conditions) return Error{"InvalidConditionsJson", failure};
    out.conditions = std::move(*conditions);
    return std::nullopt;
}

Error dialogueFailure(DialogueAuthorStatus status, const std::string& subject) {
    switch (status) {
    case DialogueAuthorStatus::quest_missing:
        return Error{"RecordNotFound", "QUST record is not present: " + subject};
    case DialogueAuthorStatus::branch_missing:
        return Error{"BranchMissing", "DLBR record is not present: " + subject};
    case DialogueAuthorStatus::topic_missing:
        return Error{"TopicMissing", "DIAL record is not present: " + subject};
    case DialogueAuthorStatus::info_missing:
        return Error{"InfoMissing", "INFO record is not present: " + subject};
    case DialogueAuthorStatus::not_local:
        return Error{"RecordNotLocal", "Record is not owned by this plugin: " + subject};
    case DialogueAuthorStatus::invalid_argument:
        return Error{"InvalidDialogueArgument", "Dialogue argument is invalid for " + subject};
    case DialogueAuthorStatus::localized_unsupported:
        return Error{"LocalizedDialogueUnsupported", "Response text cannot be written to a localized plugin"};
    case DialogueAuthorStatus::undecodable:
        return Error{"RecordUndecodable", "Record is undecodable and cannot be edited: " + subject};
    case DialogueAuthorStatus::structure_invalid:
        return Error{"DialogueStructureInvalid", "Dialogue structure does not allow this edit: " + subject};
    case DialogueAuthorStatus::ok:
        break;
    }
    return Error{"DialogueAuthorFailed", "Dialogue authoring failed for " + subject};
}

}

Result<nlohmann::json> RadiclePluginBackend::createDialogueBranch(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (recordText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto quest = support::formKeyArgument(recordText);
    if (!quest) return std::unexpected(quest.error());
    DialogueBranchSpec spec;
    if (const auto error = parseBranchSpec(support::argumentAt(arguments, 2), spec)) {
        return std::unexpected(*error);
    }
    try {
        auto mutated = loaded->plugin;
        const auto created = fo4::forms::create_dialogue_branch(mutated, *quest, spec);
        if (created.status != DialogueAuthorStatus::ok) {
            return std::unexpected(dialogueFailure(created.status, recordText));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json{{"formKey", created.key.to_string()}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CreateDialogueBranchFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::createDialogueTopic(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (recordText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto quest = support::formKeyArgument(recordText);
    if (!quest) return std::unexpected(quest.error());
    DialogueTopicSpec spec;
    if (const auto error = parseTopicSpec(support::argumentAt(arguments, 2), loaded->plugin, spec)) {
        return std::unexpected(*error);
    }
    try {
        auto mutated = loaded->plugin;
        const auto created = fo4::forms::create_dialogue_topic(mutated, *quest, spec);
        if (created.status != DialogueAuthorStatus::ok) {
            return std::unexpected(dialogueFailure(created.status, recordText));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json{{"formKey", created.key.to_string()}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CreateDialogueTopicFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setDialogueBranchStart(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto branchText = questStringArg(arguments, 1);
    const auto topicText = questStringArg(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (branchText.empty()) return std::unexpected(Error{"RecordMissing", "Branch record is empty"});
    if (topicText.empty()) return std::unexpected(Error{"RecordMissing", "Topic record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto branch = support::formKeyArgument(branchText);
    if (!branch) return std::unexpected(branch.error());
    auto topic = support::formKeyArgument(topicText);
    if (!topic) return std::unexpected(topic.error());
    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::forms::set_dialogue_branch_start(mutated, *branch, *topic);
        if (status != DialogueAuthorStatus::ok) {
            return std::unexpected(dialogueFailure(status, branchText + " -> " + topicText));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Set the start topic of " + branchText + " to " + topicText + " in " +
                              pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetDialogueBranchStartFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::createInfo(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto topicText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (topicText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto topic = support::formKeyArgument(topicText);
    if (!topic) return std::unexpected(topic.error());
    const auto& target = loaded->plugin;
    const EditorIdResolver resolver = [this, &target](std::string_view text) {
        return resolveDialogueEditorId(target, text);
    };
    DialogueInfoPatch patch;
    if (const auto error = parseInfoPatch(support::argumentAt(arguments, 2), target, {}, resolver, patch)) {
        return std::unexpected(*error);
    }
    DialogueInfoSpec spec;
    spec.flags = patch.flags.value_or(0);
    spec.reset_hours = patch.reset_hours.value_or(0);
    spec.responses = patch.responses.value_or(std::vector<DialogueResponseSpec>{});
    spec.conditions = patch.conditions.value_or(std::vector<fo4::CtdaCondition>{});
    try {
        auto mutated = target;
        const auto created = fo4::forms::create_dialogue_info(mutated, *topic, spec);
        if (created.status != DialogueAuthorStatus::ok) {
            return std::unexpected(dialogueFailure(created.status, topicText));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json{{"formKey", created.key.to_string()}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CreateInfoFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setInfo(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto infoText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (infoText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto info = support::formKeyArgument(infoText);
    if (!info) return std::unexpected(info.error());
    const auto& target = loaded->plugin;
    const EditorIdResolver resolver = [this, &target](std::string_view text) {
        return resolveDialogueEditorId(target, text);
    };
    try {
        std::vector<fo4::CtdaCondition> existing;
        const auto* record = fo4::find_record(target, *info);
        if (record != nullptr && record->header.signature == fo4::sig_info) {
            existing = fo4::read_ctda_conditions(*record);
        }
        DialogueInfoPatch patch;
        if (const auto error = parseInfoPatch(support::argumentAt(arguments, 2), target, existing, resolver, patch)) {
            return std::unexpected(*error);
        }
        auto mutated = target;
        const auto status = fo4::forms::update_dialogue_info(mutated, *info, patch);
        if (status != DialogueAuthorStatus::ok) {
            return std::unexpected(dialogueFailure(status, infoText));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Set INFO " + infoText + " in " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetInfoFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::removeInfo(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto infoText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (infoText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto info = support::formKeyArgument(infoText);
    if (!info) return std::unexpected(info.error());
    try {
        auto mutated = loaded->plugin;
        const auto status = fo4::forms::remove_dialogue_info(mutated, *info);
        if (status != DialogueAuthorStatus::ok) {
            return std::unexpected(dialogueFailure(status, infoText));
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json("Removed INFO " + infoText + " from " + pluginName + ". SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"RemoveInfoFailed", error.what()});
    }
}

std::optional<fo4::RawFormId> RadiclePluginBackend::resolveDialogueEditorId(const fo4::Plugin& target,
                                                                             std::string_view text) const {
    const auto key = resolveListValue(text);
    if (!key) return std::nullopt;
    return encodedFor(target, *key);
}

}
