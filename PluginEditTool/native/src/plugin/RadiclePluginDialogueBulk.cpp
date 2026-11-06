#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"

#include <fo4/audio/voice_path.hpp>
#include <fo4/forms/dialogue.hpp>
#include <fo4/plugin/info_responses.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin {

using namespace questinternal;
using fo4::forms::DialogueAuthorStatus;
using fo4::forms::DialogueInfoSpec;
using fo4::forms::DialogueResponseSpec;
using fo4::forms::DialogueTopicSpec;

namespace {

constexpr std::size_t k_default_response_limit = 160;
constexpr std::size_t k_max_response_limit = 4000;

constexpr auto k_info = fo4::Signature::from_string("INFO");

std::optional<Error> requireKnownKeys(const nlohmann::json& object,
                                      bool (*known)(std::string_view),
                                      std::string_view label) {
    if (!object.is_object()) {
        return Error{"InvalidDialogueArgument", std::string(label) + " must be a JSON object"};
    }
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

std::optional<Error> keyField(const nlohmann::json& object, const char* key,
                              const fo4::Plugin& target, std::optional<fo4::FormKey>& out) {
    if (!jsonObjectPresent(object, key)) return std::nullopt;
    if (!object.at(key).is_string()) {
        return Error{"InvalidDialogueArgument", std::string(key) + " must be a FormKey string"};
    }
    auto parsed = support::formKeyArgument(object.at(key).get<std::string>());
    if (!parsed) return parsed.error();
    try {
        fo4::encode_plugin_form_id(*parsed, fo4::plugin_form_id_context(target));
    } catch (const std::exception&) {
        return Error{"InvalidFormKey", std::string(key) + " is not reachable from this plugin's masters"};
    }
    out = *parsed;
    return std::nullopt;
}

void trimFront(std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' ||
                                   text[start] == '\r')) {
        ++start;
    }
    if (start != 0) text.erase(0, start);
}

void trimBack(std::string& text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r')) {
        text.pop_back();
    }
}

void trim(std::string& text) {
    trimFront(text);
    trimBack(text);
}

// Constraint: one Windows-1252 byte per character, so the limit counts stored bytes and never splits a character.
std::vector<std::string> splitResponseLine(const std::string& line, std::size_t limit) {
    std::vector<std::string> out;
    std::string rest = line;
    trim(rest);
    while (!rest.empty()) {
        if (rest.size() <= limit) {
            out.push_back(rest);
            break;
        }
        std::size_t cut = std::string::npos;
        for (std::size_t at = limit; at > 0; --at) {
            const char ch = rest[at - 1];
            if (ch == '.' || ch == '!' || ch == '?') {
                cut = at;
                break;
            }
        }
        if (cut == std::string::npos) {
            for (std::size_t at = limit; at > 0; --at) {
                if (rest[at - 1] == ' ') {
                    cut = at;
                    break;
                }
            }
        }
        if (cut == std::string::npos) cut = limit;
        std::string segment = rest.substr(0, cut);
        trim(segment);
        if (!segment.empty()) out.push_back(segment);
        rest = rest.substr(cut);
        trimFront(rest);
    }
    return out;
}

std::optional<Error> dialogueFailure(DialogueAuthorStatus status, const std::string& subject) {
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
        return std::nullopt;
    }
    return Error{"DialogueAuthorFailed", "Dialogue authoring failed for " + subject};
}

std::string cp1252(const std::string& utf8) {
    return fo4::text::utf8_to_windows1252(utf8).value_or(std::string{});
}

std::vector<std::uint8_t> responseNumbers(const fo4::Record& record) {
    std::vector<std::uint8_t> numbers;
    for (const auto& response : fo4::read_info_responses(record)) {
        if (response.malformed) continue;
        numbers.push_back(response.number);
    }
    return numbers;
}

}

Result<nlohmann::json> RadiclePluginBackend::createDialogueTopicBulk(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto questText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (questText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});
    const auto& spec = support::argumentAt(arguments, 2);
    if (auto error = requireKnownKeys(spec, [](std::string_view key) {
            return key == "editorId" || key == "name" || key == "priority" || key == "branch" ||
                   key == "keyword" || key == "topicFlags" || key == "category" || key == "subtype" ||
                   key == "subtypeName" || key == "lines" || key == "maxChars";
        }, "bulk dialogue topic")) {
        return std::unexpected(*error);
    }
    if (!jsonObjectPresent(spec, "lines") || !spec.at("lines").is_array() || spec.at("lines").empty()) {
        return std::unexpected(Error{"InvalidDialogueArgument", "lines must be a non-empty array of strings"});
    }
    std::uint32_t limit = static_cast<std::uint32_t>(k_default_response_limit);
    if (jsonObjectPresent(spec, "maxChars")) {
        if (auto error = unsignedField<std::uint32_t>(spec, "maxChars", 160, limit)) {
            return std::unexpected(*error);
        }
        if (limit == 0 || limit > k_max_response_limit) {
            return std::unexpected(Error{"InvalidDialogueArgument", "maxChars must be between 1 and 4000"});
        }
    }
    std::vector<std::string> lines;
    lines.reserve(spec.at("lines").size());
    for (const auto& entry : spec.at("lines")) {
        if (!entry.is_string()) {
            return std::unexpected(Error{"InvalidDialogueArgument", "every line must be a string"});
        }
        auto stored = fo4::text::utf8_to_windows1252(entry.get<std::string>());
        if (!stored) {
            return std::unexpected(Error{"InvalidDialogueArgument",
                                         "a line has characters that Windows-1252 cannot hold"});
        }
        lines.push_back(std::move(*stored));
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    auto quest = support::formKeyArgument(questText);
    if (!quest) return std::unexpected(quest.error());
    DialogueTopicSpec topicSpec;
    if (auto error = textField(spec, "editorId", topicSpec.editor_id)) return std::unexpected(*error);
    if (auto error = textField(spec, "name", topicSpec.name)) return std::unexpected(*error);
    if (jsonObjectPresent(spec, "priority")) {
        float priority = 50.0f;
        if (!questFloat(spec, "priority", 50.0f, priority)) {
            return std::unexpected(Error{"InvalidDialogueArgument", "priority must be a finite number"});
        }
        topicSpec.priority = priority;
    }
    if (auto error = keyField(spec, "branch", loaded->plugin, topicSpec.branch)) {
        return std::unexpected(*error);
    }
    if (auto error = keyField(spec, "keyword", loaded->plugin, topicSpec.keyword)) {
        return std::unexpected(*error);
    }
    if (auto error = unsignedField<std::uint8_t>(spec, "topicFlags", 0, topicSpec.topic_flags)) {
        return std::unexpected(*error);
    }
    if (auto error = unsignedField<std::uint8_t>(spec, "category", 0, topicSpec.category)) {
        return std::unexpected(*error);
    }
    if (auto error = unsignedField<std::uint16_t>(spec, "subtype", 0, topicSpec.subtype)) {
        return std::unexpected(*error);
    }
    std::string subtypeName;
    if (auto error = textField(spec, "subtypeName", subtypeName)) return std::unexpected(*error);
    topicSpec.subtype_name = cp1252(subtypeName);

    try {
        auto mutated = loaded->plugin;
        const auto createdTopic = fo4::forms::create_dialogue_topic(mutated, *quest, topicSpec);
        if (auto error = dialogueFailure(createdTopic.status, questText)) return std::unexpected(*error);
        nlohmann::json infos = nlohmann::json::array();
        for (const auto& line : lines) {
            auto segments = splitResponseLine(line, limit);
            // Constraint: Radicle takes UTF-8 and converts on write, so segments go back to UTF-8 after the byte-count split.
            for (auto& segment : segments) segment = fo4::text::windows1252_to_utf8(segment);
            if (segments.empty()) {
                return std::unexpected(Error{"InvalidDialogueArgument", "a line is empty once trimmed"});
            }
            if (segments.size() > 255) {
                return std::unexpected(Error{"InvalidDialogueArgument",
                                             "a line needs more than 255 responses; raise maxChars"});
            }
            DialogueInfoSpec infoSpec;
            for (std::size_t index = 0; index < segments.size(); ++index) {
                DialogueResponseSpec response;
                response.text = segments[index];
                response.number = static_cast<std::uint8_t>(index + 1);
                infoSpec.responses.push_back(std::move(response));
            }
            const auto createdInfo = fo4::forms::create_dialogue_info(mutated, createdTopic.key, infoSpec);
            if (auto error = dialogueFailure(createdInfo.status, questText)) return std::unexpected(*error);
            infos.push_back({{"formKey", createdInfo.key.to_string()}, {"responses", segments}});
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json{{"formKey", createdTopic.key.to_string()}, {"infos", std::move(infos)}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CreateDialogueTopicBulkFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getVoicePath(const nlohmann::json& arguments) const {
    const auto pluginName = questStringArg(arguments, 0);
    const auto infoText = questStringArg(arguments, 1);
    const auto voiceType = questStringArg(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (infoText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});
    if (voiceType.empty()) {
        return std::unexpected(Error{"InvalidVoicePathArgument", "voicetype must be a non-empty VTYP editor id"});
    }
    const auto language = questStringArg(arguments, 3);
    std::string extension = questStringArg(arguments, 4);
    if (extension.empty()) extension = "fuz";
    if (extension != "fuz" && extension != "hkx") {
        return std::unexpected(Error{"InvalidVoicePathArgument", "extension must be fuz or hkx"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});

    auto info = support::formKeyArgument(infoText);
    if (!info) return std::unexpected(info.error());
    const auto* record = fo4::find_record(loaded->plugin, *info);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present in " + pluginName + ": " + infoText});
    }
    if (record->header.signature != k_info) {
        return std::unexpected(Error{"RecordTypeMismatch", "GetVoicePath requires an INFO record"});
    }
    try {
        const auto numbers = responseNumbers(*record);
        nlohmann::json paths = nlohmann::json::array();
        const auto pluginFile = std::string(loaded->plugin.key.filename());
        for (const auto number : numbers) {
            paths.push_back(fo4::audio::format_voice_path(pluginFile, voiceType,
                                                          info->local().value(), number, language,
                                                          {}, extension));
        }
        return nlohmann::json{{"plugin", pluginFile},
                              {"voicetype", voiceType},
                              {"infoFormKey", info->to_string()},
                              {"paths", std::move(paths)}};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetVoicePathFailed", error.what()});
    }
}

}
