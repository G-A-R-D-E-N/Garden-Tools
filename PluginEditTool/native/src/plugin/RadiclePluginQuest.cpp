#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
namespace {

std::string questStringArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return {};
    if (args[index].is_string()) return args[index].get<std::string>();
    return {};
}

const nlohmann::json* questArrayArg(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size()) return nullptr;
    if (args[index].is_array()) return &args[index];
    if (args[index].is_string()) {
        static thread_local nlohmann::json parsed;
        try {
            parsed = nlohmann::json::parse(args[index].get<std::string>());
        } catch (const std::exception&) {
            return nullptr;
        }
        if (!parsed.is_array()) return nullptr;
        return &parsed;
    }
    return nullptr;
}

bool parseEntireUnsigned32(std::string_view text, std::uint32_t& value) {
    if (text.empty()) return false;
    if (text.front() == '+' || text.front() == '-') return false;
    int base = 10;
    auto view = text;
    if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
        base = 16;
        view.remove_prefix(2);
        if (view.empty()) return false;
    }
    std::uint64_t parsed = 0;
    const auto [ptr, error] = std::from_chars(
        view.data(), view.data() + view.size(), parsed, base);
    if (error != std::errc{} || ptr != view.data() + view.size()) return false;
    if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool questUnsigned(const nlohmann::json& object,
                   const char* key,
                   std::uint32_t defaultValue,
                   std::uint32_t& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = defaultValue;
        return true;
    }
    if (found->is_number_unsigned()) {
        const auto parsed = found->get<std::uint64_t>();
        if (parsed > std::numeric_limits<std::uint32_t>::max()) return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 ||
            parsed > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    if (found->is_string()) {
        return parseEntireUnsigned32(found->get<std::string>(), value);
    }
    return false;
}

bool questInt32(const nlohmann::json& object, const char* key, std::optional<std::int32_t>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < std::numeric_limits<std::int32_t>::min() ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        value = static_cast<std::int32_t>(parsed);
        return true;
    }
    return false;
}

bool questU8(const nlohmann::json& object, const char* key, std::optional<std::uint8_t>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (found->is_number_unsigned()) {
        const auto parsed = found->get<std::uint64_t>();
        if (parsed > 255) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }
    if (found->is_number_integer()) {
        const auto parsed = found->get<std::int64_t>();
        if (parsed < 0 || parsed > 255) return false;
        value = static_cast<std::uint8_t>(parsed);
        return true;
    }
    return false;
}

bool questOptionalString(const nlohmann::json& object,
                         const char* key,
                         std::optional<std::string>& value) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        value = std::nullopt;
        return true;
    }
    if (!found->is_string()) return false;
    value = found->get<std::string>();
    return true;
}

bool knownAliasKey(std::string_view key) {
    return key == "kind" || key == "id" || key == "name" || key == "flags" ||
           key == "forceIntoAlias" || key == "closestToAlias" ||
           key == "maxInitialFillCount" || key == "forcedReference" ||
           key == "uniqueActor" || key == "specificLocation";
}

bool knownStageKey(std::string_view key) {
    return key == "index" || key == "flags" || key == "logEntry" ||
           key == "logEntries" || key == "complete" || key == "fail";
}

bool knownLogEntryKey(std::string_view key) {
    return key == "flags" || key == "note" || key == "logText";
}

bool isPlacedSignature(fo4::Signature signature) {
    const auto text = signature.to_string();
    return text == "REFR" || text == "ACHR" || text == "PGRE" || text == "PHZD" ||
           text == "PMIS" || text == "PARW" || text == "PBAR" || text == "PBEA" ||
           text == "PCON" || text == "PFLA";
}

std::optional<Error> requireLoadedFill(
    const fo4::ConflictGraph& graph,
    const fo4::FormKey& key,
    std::initializer_list<std::string_view> accepted,
    bool placedTargets,
    std::string_view slotName) {
    const auto* winner = graph.winner(key);
    if (winner == nullptr) {
        return Error{
            "InvalidTypedFormKey",
            std::string(slotName) + " must resolve to a loaded record: " + key.to_string()
        };
    }
    const auto signature = winner->signature.to_string();
    if (placedTargets && isPlacedSignature(winner->signature)) return std::nullopt;
    for (const auto name : accepted) {
        if (signature == name) return std::nullopt;
    }
    std::string expected;
    if (placedTargets) expected = "placed reference";
    for (const auto name : accepted) {
        if (!expected.empty()) expected += " or ";
        expected += std::string(name);
    }
    return Error{
        "InvalidTypedFormKey",
        std::string(slotName) + " " + key.to_string() + " is a " + signature +
            ", but this slot holds a " + expected
    };
}

std::optional<Error> rejectUnknownKeys(const nlohmann::json& object,
                                       bool (*known)(std::string_view),
                                       std::string_view label) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!known(it.key())) {
            return Error{
                "InvalidQuestStagePayload",
                "Unsupported " + std::string(label) + " field '" + it.key() + "'"
            };
        }
    }
    return std::nullopt;
}

std::optional<Error> parseFillFormKey(const nlohmann::json& object,
                                      const char* key,
                                      std::optional<fo4::FormKey>& out) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        out = std::nullopt;
        return std::nullopt;
    }
    if (!found->is_string()) {
        return Error{"InvalidQuestAliasPayload", std::string(key) + " must be a FormKey string"};
    }
    const auto text = found->get<std::string>();
    if (text.empty()) {
        out = std::nullopt;
        return std::nullopt;
    }
    try {
        out = fo4::FormKey::parse(text);
        return std::nullopt;
    } catch (const std::exception& error) {
        return Error{"InvalidFormKey", std::string(key) + ": " + error.what()};
    }
}

bool allowedAliasMember(fo4::forms::QuestAliasKind kind, std::string_view signature) {
    if (kind == fo4::forms::QuestAliasKind::collection) {
        return signature == "ALCS" || signature == "ALMI";
    }
    if (kind == fo4::forms::QuestAliasKind::location) {
        return signature == "ALLS" || signature == "ALID" || signature == "FNAM" ||
               signature == "ALFI" || signature == "ALFL" || signature == "ALCC" ||
               signature == "ALED";
    }
    return signature == "ALST" || signature == "ALID" || signature == "FNAM" ||
           signature == "ALFI" || signature == "ALFR" || signature == "ALUA" ||
           signature == "ALED";
}

bool supportedAliasFill(const fo4::forms::QuestAliasFill& fill) {
    return std::holds_alternative<std::monostate>(fill) ||
           std::holds_alternative<fo4::forms::QuestSpecificReferenceFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestUniqueActorFill>(fill) ||
           std::holds_alternative<fo4::forms::QuestSpecificLocationFill>(fill);
}

std::optional<Error> existingStagesUnrepresentable(const fo4::Plugin& plugin,
                                                   const fo4::FormKey& quest) {
    const auto decoded = fo4::forms::decode_quest_stages(plugin, quest);
    for (const auto& stage : decoded.stages) {
        if (stage.unknown != 0) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST stage has a non-zero INDX unknown byte"
            };
        }
        if (!stage.leading_members.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST stage has members before the first QSDT"
            };
        }
        for (const auto& entry : stage.log_entries) {
            if (entry.log_text.has_value() && entry.log_text->localized()) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has a localized CNAM that this adapter cannot reconstruct"
                };
            }
            bool sawNam2 = false;
            bool sawCnam = false;
            for (const auto& member : entry.members) {
                const auto signature = member.signature.to_string();
                if (signature == "NAM2") {
                    if (sawNam2 || sawCnam) {
                        return Error{
                            "QuestAuthoringCoverageIncomplete",
                            "existing QUST log entry NAM2/CNAM order cannot be reconstructed"
                        };
                    }
                    sawNam2 = true;
                    continue;
                }
                if (signature == "CNAM") {
                    if (sawCnam) {
                        return Error{
                            "QuestAuthoringCoverageIncomplete",
                            "existing QUST log entry has duplicate CNAM"
                        };
                    }
                    sawCnam = true;
                    continue;
                }
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has unsupported member " + signature
                };
            }
        }
    }
    return std::nullopt;
}

std::optional<Error> existingAliasesUnrepresentable(const fo4::Plugin& plugin,
                                                    const fo4::FormKey& quest) {
    const auto decoded = fo4::forms::decode_quest_aliases(plugin, quest);
    for (const auto& alias : decoded.aliases) {
        if (!alias.unresolved_links.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias has an unresolved FormLink"
            };
        }
        if (!supportedAliasFill(alias.fill)) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST alias uses a Fill Type this adapter cannot reconstruct"
            };
        }
        if (alias.kind == fo4::forms::QuestAliasKind::collection &&
            alias.collection_entries.size() > 1) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST collection alias has multiple ALCS entries"
            };
        }
        for (const auto& member : alias.subrecords) {
            const auto signature = member.signature.to_string();
            if (!allowedAliasMember(alias.kind, signature)) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST alias has unsupported member " + signature
                };
            }
        }
    }
    return std::nullopt;
}

std::vector<std::byte> zstringBytes(std::string_view value) {
    std::vector<std::byte> data;
    data.reserve(value.size() + 1);
    for (const char ch : value) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    data.push_back(std::byte{0});
    return data;
}

}

Result<nlohmann::json> RadiclePluginBackend::setQuestAliases(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    const auto* aliasesJson = questArrayArg(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }
    if (aliasesJson == nullptr) {
        return std::unexpected(Error{
            "InvalidQuestAliasPayload",
            "SetQuestAliases requires a JSON array of alias objects"
        });
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey questKey;
    try {
        questKey = fo4::FormKey::parse(recordText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    const auto* record = fo4::find_record(loaded->plugin, questKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + recordText
        });
    }
    if (record->header.signature != fo4::Signature::from_string("QUST")) {
        return std::unexpected(Error{
            "RecordTypeMismatch",
            "SetQuestAliases requires a QUST record"
        });
    }
    try {
        if (const auto error = existingAliasesUnrepresentable(loaded->plugin, questKey)) {
            return std::unexpected(*error);
        }
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    }

    std::vector<fo4::forms::QuestAliasCreation> creations;
    std::vector<std::string> nameStorage;
    creations.reserve(aliasesJson->size());
    nameStorage.reserve(aliasesJson->size());
    std::uint32_t autoId = 0;
    std::vector<fo4::FormKey> fillKeys;

    for (const auto& element : *aliasesJson) {
        if (!element.is_object()) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "Every quest alias must be an object"
            });
        }
        for (auto it = element.begin(); it != element.end(); ++it) {
            if (!knownAliasKey(it.key())) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "Unsupported quest alias field '" + it.key() + "'"
                });
            }
        }

        fo4::forms::QuestAliasCreation creation;
        std::string kindText{"reference"};
        const auto kindField = element.find("kind");
        if (kindField != element.end() && !kindField->is_null()) {
            if (!kindField->is_string()) {
                return std::unexpected(Error{
                    "InvalidQuestAliasPayload",
                    "kind must be a string"
                });
            }
            kindText = kindField->get<std::string>();
        }
        if (kindText == "reference") creation.kind = fo4::forms::QuestAliasKind::reference;
        else if (kindText == "location") creation.kind = fo4::forms::QuestAliasKind::location;
        else if (kindText == "collection") creation.kind = fo4::forms::QuestAliasKind::collection;
        else {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "kind must be reference, location, or collection"
            });
        }

        if (!questUnsigned(element, "id", autoId, creation.id)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "alias id must be an unsigned 32-bit value"
            });
        }
        autoId = creation.id + 1;

        std::optional<std::string> name;
        if (!questOptionalString(element, "name", name)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "name must be a string or null"
            });
        }
        if (creation.kind == fo4::forms::QuestAliasKind::reference && !name.has_value()) {
            name = "Alias" + std::to_string(creation.id);
        }
        if (name.has_value()) {
            nameStorage.push_back(*name);
            creation.name = nameStorage.back();
        }

        std::uint32_t flags = 0;
        const bool hasFlags = element.contains("flags") && !element["flags"].is_null();
        if (!questUnsigned(element, "flags", 0, flags)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "flags must be an unsigned 32-bit value"
            });
        }
        if (creation.kind != fo4::forms::QuestAliasKind::collection) {
            creation.flags = flags;
        } else if (hasFlags) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "collection aliases do not use flags"
            });
        }

        if (!questInt32(element, "forceIntoAlias", creation.force_into_alias) ||
            !questInt32(element, "closestToAlias", creation.closest_to_alias) ||
            !questU8(element, "maxInitialFillCount", creation.max_initial_fill_count)) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "forceIntoAlias, closestToAlias, and maxInitialFillCount have invalid types"
            });
        }

        std::optional<fo4::FormKey> forced;
        std::optional<fo4::FormKey> actor;
        std::optional<fo4::FormKey> location;
        if (const auto error = parseFillFormKey(element, "forcedReference", forced)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseFillFormKey(element, "uniqueActor", actor)) {
            return std::unexpected(*error);
        }
        if (const auto error = parseFillFormKey(element, "specificLocation", location)) {
            return std::unexpected(*error);
        }
        const int fillCount =
            (forced.has_value() ? 1 : 0) + (actor.has_value() ? 1 : 0) +
            (location.has_value() ? 1 : 0);
        if (fillCount > 1) {
            return std::unexpected(Error{
                "InvalidQuestAliasPayload",
                "an alias may set only one Fill Type"
            });
        }
        if (forced.has_value()) {
            if (const auto error = requireLoadedFill(
                    graph_, *forced, {}, true, "forcedReference")) {
                return std::unexpected(*error);
            }
            creation.fill = fo4::forms::QuestSpecificReferenceFill{forced};
            fillKeys.push_back(*forced);
        } else if (actor.has_value()) {
            if (const auto error = requireLoadedFill(
                    graph_, *actor, {"NPC_"}, false, "uniqueActor")) {
                return std::unexpected(*error);
            }
            creation.fill = fo4::forms::QuestUniqueActorFill{*actor};
            fillKeys.push_back(*actor);
        } else if (location.has_value()) {
            if (const auto error = requireLoadedFill(
                    graph_, *location, {"LCTN"}, false, "specificLocation")) {
                return std::unexpected(*error);
            }
            creation.fill = fo4::forms::QuestSpecificLocationFill{*location};
            fillKeys.push_back(*location);
        }
        creations.push_back(std::move(creation));
    }

    try {
        auto mutated = loaded->plugin;
        for (const auto& key : fillKeys) ensureOwnerMaster(mutated, key.owner());
        fo4::forms::write_quest_aliases(mutated, questKey, creations);
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Set " + std::to_string(creations.size()) + " quest alias(es) on " +
            recordText + " in " + pluginName + ". Written to disk.");
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"SetQuestAliasesFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetQuestAliasesFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setQuestStages(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    const auto* stagesJson = questArrayArg(arguments, 2);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }
    if (recordText.empty()) {
        return std::unexpected(Error{"RecordMissing", "Record is empty"});
    }
    if (stagesJson == nullptr) {
        return std::unexpected(Error{
            "InvalidQuestStagePayload",
            "SetQuestStages requires a JSON array of stage objects"
        });
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey questKey;
    try {
        questKey = fo4::FormKey::parse(recordText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    const auto* record = fo4::find_record(loaded->plugin, questKey);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + pluginName + ": " + recordText
        });
    }
    if (record->header.signature != fo4::Signature::from_string("QUST")) {
        return std::unexpected(Error{
            "RecordTypeMismatch",
            "SetQuestStages requires a QUST record"
        });
    }
    try {
        if (const auto error = existingStagesUnrepresentable(loaded->plugin, questKey)) {
            return std::unexpected(*error);
        }
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    }

    const bool localized = loaded->plugin.header.tes4_flags.is_localized();
    fo4::forms::QuestStages stages;
    std::set<std::uint16_t> indexes;

    for (const auto& element : *stagesJson) {
        if (!element.is_object()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "Every quest stage must be an object"
            });
        }
        if (const auto error = rejectUnknownKeys(element, knownStageKey, "quest stage")) {
            return std::unexpected(*error);
        }
        fo4::forms::QuestStage stage;
        std::uint32_t index = 0;
        std::uint32_t flags = 0;
        if (!questUnsigned(element, "index", 0, index) ||
            index > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "stage index must be 0..65535"
            });
        }
        if (!questUnsigned(element, "flags", 0, flags) || flags > 255) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "stage flags must be 0..255"
            });
        }
        stage.index = static_cast<std::uint16_t>(index);
        stage.flags = static_cast<std::uint8_t>(flags);
        if (!indexes.insert(stage.index).second) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "duplicate stage index " + std::to_string(stage.index)
            });
        }

        const auto completeField = element.find("complete");
        const auto failField = element.find("fail");
        if (completeField != element.end() && !completeField->is_null() &&
            !completeField->is_boolean()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "complete must be a boolean"
            });
        }
        if (failField != element.end() && !failField->is_null() &&
            !failField->is_boolean()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "fail must be a boolean"
            });
        }
        const bool hasComplete = completeField != element.end() &&
                                 completeField->is_boolean();
        const bool hasFail = failField != element.end() && failField->is_boolean();
        const bool hasLogEntries =
            element.contains("logEntries") && !element["logEntries"].is_null();
        const bool hasLogEntry =
            element.contains("logEntry") && !element["logEntry"].is_null();
        if (hasLogEntry && !element["logEntry"].is_string()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "logEntry must be a string"
            });
        }
        if (hasLogEntries && hasLogEntry) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "a stage may set logEntry or logEntries, not both"
            });
        }
        if (hasLogEntries && (hasComplete || hasFail)) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "complete/fail apply only to the logEntry sugar path"
            });
        }
        if ((hasComplete || hasFail) && !hasLogEntry) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "complete/fail require a logEntry string"
            });
        }

        nlohmann::json entries = nlohmann::json::array();
        if (hasLogEntries) {
            if (!element["logEntries"].is_array()) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "logEntries must be an array"
                });
            }
            entries = element["logEntries"];
        } else if (hasLogEntry) {
            nlohmann::json one = nlohmann::json::object();
            one["logText"] = element["logEntry"];
            std::uint32_t sugarFlags = 0;
            if (hasComplete && completeField->get<bool>()) sugarFlags |= 1u;
            if (hasFail && failField->get<bool>()) sugarFlags |= 2u;
            if (sugarFlags != 0) one["flags"] = sugarFlags;
            entries.push_back(std::move(one));
        }

        for (const auto& entryJson : entries) {
            if (!entryJson.is_object()) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "every log entry must be an object"
                });
            }
            if (const auto error =
                    rejectUnknownKeys(entryJson, knownLogEntryKey, "log entry")) {
                return std::unexpected(*error);
            }
            fo4::forms::QuestLogEntry entry;
            std::uint32_t entryFlags = 0;
            if (!questUnsigned(entryJson, "flags", 0, entryFlags) || entryFlags > 255) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "log entry flags must be 0..255"
                });
            }
            entry.flags = static_cast<std::uint8_t>(entryFlags);

            std::optional<std::string> note;
            std::optional<std::string> logText;
            if (!questOptionalString(entryJson, "note", note) ||
                !questOptionalString(entryJson, "logText", logText)) {
                return std::unexpected(Error{
                    "InvalidQuestStagePayload",
                    "note and logText must be strings"
                });
            }
            if (localized && logText.has_value()) {
                return std::unexpected(Error{
                    "LocalizedLogTextUnsupported",
                    "new QUST CNAM log text in a localized plugin needs a string-table ID"
                });
            }
            if (note.has_value()) {
                if (note->find('\0') != std::string::npos) {
                    return std::unexpected(Error{
                        "InvalidQuestStagePayload",
                        "note cannot contain an embedded NUL"
                    });
                }
                entry.note = note;
                fo4::Subrecord nam2;
                nam2.signature = fo4::Signature::from_string("NAM2");
                nam2.data = zstringBytes(*note);
                entry.members.push_back(std::move(nam2));
            }
            if (logText.has_value()) {
                if (logText->find('\0') != std::string::npos) {
                    return std::unexpected(Error{
                        "InvalidQuestStagePayload",
                        "logText cannot contain an embedded NUL"
                    });
                }
                fo4::schema::LocalizedString value;
                value.text = logText;
                entry.log_text = value;
                fo4::Subrecord cnam;
                cnam.signature = fo4::Signature::from_string("CNAM");
                cnam.data = fo4::schema::write_localized_string(value);
                entry.members.push_back(std::move(cnam));
            }
            stage.log_entries.push_back(std::move(entry));
        }
        stages.stages.push_back(std::move(stage));
    }

    try {
        auto mutated = loaded->plugin;
        fo4::forms::write_quest_stages(mutated, questKey, stages);
        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        {
            std::error_code ec;
            loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        }
        rebuildGraphLocked();
        return nlohmann::json(
            "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
            recordText + " in " + pluginName + ". Written to disk.");
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"SetQuestStagesFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetQuestStagesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
