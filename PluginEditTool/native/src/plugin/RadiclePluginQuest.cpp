#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>
#include <fo4ide/plugin/ConditionFunctions.hpp>

#include "RadiclePluginQuestInternal.hpp"

namespace fo4ide::plugin {

using namespace questinternal;
using fo4::env::StringSource;

#ifdef FO4IDE_TEST_HOOKS
void setLocalizedQuestBeforeCommitHook(LocalizedQuestBeforeCommitHook hook) {
    localizedQuestBeforeCommitHook = hook;
}

void setLocalizedQuestAfterTableParseHook(LocalizedQuestAfterTableParseHook hook) {
    localizedQuestAfterTableParseHook = hook;
}
#endif

namespace {

bool knownStageKey(std::string_view key) {
    return key == "index" || key == "flags" || key == "logEntry" ||
           key == "logEntries" || key == "complete" || key == "fail" ||
           key == "startUp";
}

bool knownLogEntryKey(std::string_view key) {
    return key == "flags" || key == "note" || key == "logText" || key == "conditions";
}

std::optional<Error> existingStagesUnrepresentable(const fo4::Plugin& plugin,
                                                   const fo4::FormKey& quest,
                                                   const fo4::ConflictGraph& graph,
                                                   bool allowLocalized) {
    const auto decoded = fo4::forms::decode_quest_stages(plugin, quest);
    for (const auto& stage : decoded.stages) {
        if (!stage.leading_members.empty()) {
            return Error{
                "QuestAuthoringCoverageIncomplete",
                "existing QUST stage has members before the first QSDT"
            };
        }
        for (const auto& entry : stage.log_entries) {
            if (!allowLocalized && entry.log_text.has_value() && entry.log_text->localized()) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has a localized CNAM that this adapter cannot reconstruct"
                };
            }
            std::vector<fo4::CtdaCondition> conditions;
            std::size_t consumed = 0;
            if (!fo4::forms::lossless_quest_log_entry_conditions(
                    entry.members, conditions, consumed)) {
                return Error{
                    "QuestAuthoringCoverageIncomplete",
                    "existing QUST log entry has a CTDA/CIS run this adapter cannot reconstruct"
                };
            }
            for (const auto& condition : conditions) {
                if (const auto error =
                        adapterConditionUnrepresentable(plugin, graph, condition)) {
                    return error;
                }
            }
            bool sawNam2 = false;
            bool sawCnam = false;
            for (std::size_t i = consumed; i < entry.members.size(); ++i) {
                const auto signature = entry.members[i].signature.to_string();
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

std::vector<fo4::CtdaCondition> previousLogEntryConditions(const fo4::forms::QuestStages& previous,
                                                           std::uint16_t index,
                                                           std::size_t ordinal) {
    const auto sameIndex = [index](const fo4::forms::QuestStage& stage) {
        return stage.index == index;
    };
    std::vector<fo4::CtdaCondition> conditions;
    if (std::count_if(previous.stages.begin(), previous.stages.end(), sameIndex) != 1) {
        return conditions;
    }
    const auto found = std::find_if(previous.stages.begin(), previous.stages.end(), sameIndex);
    if (ordinal >= found->log_entries.size()) return conditions;
    std::size_t consumed = 0;
    if (!fo4::forms::lossless_quest_log_entry_conditions(
            found->log_entries[ordinal].members, conditions, consumed)) {
        conditions.clear();
    }
    return conditions;
}

std::vector<std::byte> zstringBytes(std::string_view value) {
    std::vector<std::byte> data;
    data.reserve(value.size() + 1);
    for (const char ch : value) data.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    data.push_back(std::byte{0});
    return data;
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

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
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
    if (const auto error = existingQuestVmadUnrepresentable(*record, false, true)) {
        return std::unexpected(*error);
    }
    const bool localized = loaded->plugin.header.tes4_flags.is_localized();
    fo4::forms::QuestStages previousStages;
    std::optional<LocalizedQuestTables> localizedTables;
    std::optional<StagedLocalization> localizationState;
    std::optional<LocalizedStringAllocation> sourceAllocation;
    if (localized && arguments.is_array() && arguments.size() > 3) {
        localizedTables.emplace();
        std::filesystem::path stringsDirectory;
        if (arguments[3].is_object() && arguments[3].contains("stringsDirectory") &&
            arguments[3]["stringsDirectory"].is_string()) {
            stringsDirectory = fo4::io::from_utf8(arguments[3]["stringsDirectory"].get<std::string>());
        }
        bool allowAbsent = false;
#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
        allowAbsent = !stringsDirectory.empty() && !testJournalRoot_.empty() && !dataDirectory_.empty() &&
            support::sameLocalizedPath(stringsDirectory, loaded->path.parent_path() / "Strings");
#endif
        const auto allowedDirectory =
            stringsDirectory.empty() ? std::nullopt
                                     : allowedLocalizationDirectory(stringsDirectory, allowAbsent);
        if (!stringsDirectory.empty() && !allowedDirectory.has_value()) {
            return std::unexpected(Error{
                "LocalizedSidecarPathNotAllowed",
                "Refusing to write localized sidecars outside a trusted writable root: " +
                    stringsDirectory.string()
            });
        }
        if (const auto error = loadLocalizedQuestTables(
                arguments, loaded->path,
                allowedDirectory.value_or(std::filesystem::path{}), *localizedTables, allowAbsent)) {
            return std::unexpected(*error);
        }
        auto loadedLocalization = loaded->stagedLocalization.value_or(StagedLocalization{});
        const auto overlay = [&](const std::filesystem::path& path,
                                 std::string_view kind,
                                 StringSource source,
                                 fo4::localization::LocalizedStringTable& table,
                                 const std::vector<std::byte>& original, bool originalExists)
            -> std::optional<Error> {
            auto file = localizationFile(path.parent_path(), fo4::io::utf8(path.filename()),
                                         kind, localizedTables->language, original, originalExists);
            if (!file) return Error{"LocalizedSidecarPathNotAllowed", "localized table is outside an approved writable root"};
            if (auto staged = stagedStringTable(loaded->name, localizedTables->language, source, path)) {
                if (staged->file.originalExists != file->originalExists ||
                    staged->file.originalSize != file->originalSize ||
                    staged->file.originalHash != file->originalHash) {
                    return Error{"LocalizedSidecarChangedOnDisk", "localized sibling table changed after it was staged; reload before writing"};
                }
                table = std::move(staged->table);
                *file = std::move(staged->file);
            }
            file->finalBytes.clear();
            file->verifyOnly = true;
            const auto previous = std::find_if(loadedLocalization.participants.begin(), loadedLocalization.participants.end(),
                [&](const StagedLocalizationFile& item) { return support::sameLocalizedPath(item.path, file->path); });
            if (previous == loadedLocalization.participants.end()) loadedLocalization.participants.push_back(std::move(*file));
            else if (previous->originalExists != file->originalExists ||
                     previous->originalSize != file->originalSize ||
                     previous->originalHash != file->originalHash) {
                return Error{"LocalizedSidecarChangedOnDisk", "localized sibling table baseline changed while editing"};
            }
            return std::nullopt;
        };
        if (const auto error = overlay(localizedTables->stringsPath, "strings", StringSource::normal,
                                       localizedTables->strings, localizedTables->originalStringsBytes, localizedTables->stringsExist)) return std::unexpected(*error);
        if (const auto error = overlay(localizedTables->dlstringsPath, "dlstrings", StringSource::dl,
                                       localizedTables->dlstrings, localizedTables->originalDlstringsBytes, localizedTables->dlstringsExist)) return std::unexpected(*error);
        if (const auto error = overlay(localizedTables->ilstringsPath, "ilstrings", StringSource::il,
                                       localizedTables->ilstrings, localizedTables->originalIlstringsBytes, localizedTables->ilstringsExist)) return std::unexpected(*error);
        std::uint32_t maximum = 0;
        for (const auto* table : {&localizedTables->strings, &localizedTables->dlstrings, &localizedTables->ilstrings}) {
            for (const auto& entry : table->entries) maximum = std::max(maximum, entry.id);
        }
        if (maximum == std::numeric_limits<std::uint32_t>::max()) return std::unexpected(Error{"LocalizedLogTableFull", "localized string-table ID space is exhausted"});
        localizedTables->nextId = maximum + 1;
        bool heldSources = false;
#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
        if (!testJournalRoot_.empty() && (!dataDirectory_.empty() ||
            (loaded->stagedAllocation && loaded->stagedAllocation->heldGeneration))) {
            if (!support::sameLocalizedPath(localizedTables->stringsPath.parent_path(), loaded->path.parent_path() / "Strings"))
                return std::unexpected(Error{"LocalizedSourcesUnavailable", "custom quest string roots require held-source authorization"});
            auto held = testOpenHeldLocalizedStrings(*loaded, localizedTables->language);
            if (!held) return std::unexpected(held.error());
            for (const auto& snapshot : held->sourceSnapshots) {
                if ((snapshot.resolved.source == StringSource::normal && !localizedTables->stringsExist) ||
                    (snapshot.resolved.source == StringSource::dl && !localizedTables->dlstringsExist) ||
                    (snapshot.resolved.source == StringSource::il && !localizedTables->ilstringsExist))
                    return std::unexpected(Error{"LocalizedAllocationSnapshotUnsupported", "a missing destination resolves to another source"});
            }
            localizedTables->nextId = std::max(localizedTables->nextId, held->nextId);
            sourceAllocation.emplace(std::move(*held)); heldSources = true;
        }
#endif
        if (!heldSources) {
            sourceAllocation.emplace(); sourceAllocation->language = localizedTables->language;
            const auto globalNext = nextLocalizedStringId(*loaded, &*sourceAllocation);
            if (!globalNext) return std::unexpected(globalNext.error());
            sourceAllocation->sourceSnapshots = sourceAllocation->allocationSources.at(sourceAllocation->language);
            const auto combined = includeLocalizedDirectory(*loaded, *sourceAllocation, localizedTables->stringsPath.parent_path(), *globalNext);
            if (!combined) return std::unexpected(combined.error());
            localizedTables->nextId = std::max(localizedTables->nextId, *combined);
        }
        localizationState = std::move(loadedLocalization);
    }
    try {
        previousStages = fo4::forms::decode_quest_stages(loaded->plugin, questKey);
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    }
    try {
        if (const auto error = existingStagesUnrepresentable(
                loaded->plugin, questKey, graph_, localizedTables.has_value())) {
            return std::unexpected(*error);
        }
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    }

    fo4::forms::QuestStages stages;
    std::vector<std::vector<std::vector<PendingQuestCondition>>> pendingConditions;
    std::vector<fo4::FormKey> conditionKeys;
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
        pendingConditions.emplace_back();
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
        const auto startUpField = element.find("startUp");
        if (startUpField != element.end() && !startUpField->is_null() &&
            !startUpField->is_boolean()) {
            return std::unexpected(Error{
                "InvalidQuestStagePayload",
                "startUp must be a boolean"
            });
        }
        stage.index = static_cast<std::uint16_t>(index);
        const bool hasRawFlags = element.contains("flags") && !element["flags"].is_null();
        const bool hasStartUp = startUpField != element.end() && startUpField->is_boolean();
        for (const auto& previous : previousStages.stages) {
            if (previous.index == stage.index) {
                stage.unknown = previous.unknown;
                if (hasStartUp && !hasRawFlags) flags = previous.flags;
                break;
            }
        }
        if (hasStartUp) {
            flags = startUpField->get<bool>() ? (flags | 0x02u) : (flags & ~0x02u);
        }
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
            if (localized && logText.has_value() && !localizedTables.has_value()) {
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
                const auto encoded = fo4::text::utf8_to_windows1252(*note);
                if (!encoded) return std::unexpected(Error{"InvalidQuestStagePayload", "note must be representable in Windows-1252"});
                note = *encoded;
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
                if (!localized) {
                    const auto encoded = fo4::text::utf8_to_windows1252(*logText);
                    if (!encoded) return std::unexpected(Error{"InvalidQuestStagePayload", "logText must be representable in Windows-1252"});
                    logText = *encoded;
                }
                fo4::schema::LocalizedString value;
                value.text = logText;
                entry.log_text = value;
                fo4::Subrecord cnam;
                cnam.signature = fo4::Signature::from_string("CNAM");
                cnam.data = fo4::schema::write_localized_string(value);
                entry.members.push_back(std::move(cnam));
            }
            std::vector<PendingQuestCondition> parsedConditions;
            if (entryJson.contains("conditions") && !entryJson["conditions"].is_null()) {
                if (!entryJson["conditions"].is_array()) {
                    return std::unexpected(Error{
                        "InvalidQuestStagePayload",
                        "conditions must be an array"
                    });
                }
                for (const auto& conditionJson : entryJson["conditions"]) {
                    PendingQuestCondition pending;
                    if (const auto error = parseQuestCondition(
                            conditionJson, graph_, pending, conditionKeys)) {
                        return std::unexpected(*error);
                    }
                    parsedConditions.push_back(std::move(pending));
                }
            }
            stage.log_entries.push_back(std::move(entry));
            pendingConditions.back().push_back(std::move(parsedConditions));
        }
        stages.stages.push_back(std::move(stage));
    }

    try {
        auto mutated = loaded->plugin;
        bool localizedTableChanged = false;
        if (localizedTables.has_value()) {
            for (std::size_t stageIndex = 0; stageIndex < stages.stages.size(); ++stageIndex) {
                auto& stage = stages.stages[stageIndex];
                for (std::size_t entryIndex = 0; entryIndex < stage.log_entries.size(); ++entryIndex) {
                    auto& entry = stage.log_entries[entryIndex];
                    if (!entry.log_text.has_value() || !entry.log_text->text.has_value()) continue;

                    std::optional<std::uint32_t> previousId;
                    for (const auto& previous : previousStages.stages) {
                        if (previous.index != stage.index ||
                            entryIndex >= previous.log_entries.size()) {
                            continue;
                        }
                        const auto& previousEntry = previous.log_entries[entryIndex];
                        if (previousEntry.log_text.has_value() &&
                            previousEntry.log_text->id.has_value()) {
                            previousId = previousEntry.log_text->id;
                        }
                        break;
                    }

                    std::optional<std::uint32_t> id;
                    std::vector<std::byte> text;
                    if (const auto error = encodeLocalizedText(
                            localizedTables->language, *entry.log_text->text, text)) {
                        return std::unexpected(*error);
                    }
                    if (previousId.has_value()) {
                        const auto* previousText = fo4::localization::find_string(
                            localizedTables->dlstrings, *previousId);
                        if (previousText == nullptr) {
                            return std::unexpected(Error{
                                "LocalizedLogTableInvalid",
                                "DLSTRINGS is missing existing QUST CNAM ID " +
                                    std::to_string(*previousId)
                            });
                        }
                        if (previousText->text == text) id = *previousId;
                    }
                    if (!id.has_value()) {
                        if (localizedTables->nextId == std::numeric_limits<std::uint32_t>::max()) {
                            return std::unexpected(Error{
                                "LocalizedLogTableFull",
                                "localized string-table ID space is exhausted"
                            });
                        }
                        id = localizedTables->nextId++;
                        localizedTables->dlstrings.entries.push_back({*id, text});
                        localizedTableChanged = true;
                    }
                    entry.log_text->id = id;
                    entry.log_text->text.reset();
                    for (auto& member : entry.members) {
                        if (member.signature == fo4::Signature::from_string("CNAM")) {
                            member.data = fo4::schema::write_localized_string(*entry.log_text);
                        }
                    }
                }
            }
        }
        for (const auto& key : conditionKeys) ensureOwnerMaster(mutated, key.owner());
        const auto context = fo4::plugin_form_id_context(mutated);
        const auto encodeKey = [&](const std::optional<fo4::FormKey>& key) -> std::uint32_t {
            if (!key.has_value()) return 0;
            return fo4::encode_plugin_form_id(*key, context).value();
        };
        for (std::size_t stageIndex = 0; stageIndex < stages.stages.size(); ++stageIndex) {
            auto& stage = stages.stages[stageIndex];
            for (std::size_t entryIndex = 0; entryIndex < stage.log_entries.size(); ++entryIndex) {
                const auto& pending = pendingConditions[stageIndex][entryIndex];
                if (pending.empty()) continue;
                std::vector<fo4::CtdaCondition> encoded;
                encoded.reserve(pending.size());
                for (auto condition : pending) {
                    if (condition.param1.has_value()) {
                        condition.condition.param1 =
                            static_cast<std::int32_t>(encodeKey(condition.param1));
                    }
                    if (condition.param2.has_value()) {
                        condition.condition.param2 =
                            static_cast<std::int32_t>(encodeKey(condition.param2));
                    }
                    if (condition.reference.has_value()) {
                        condition.condition.reference = encodeKey(condition.reference);
                    }
                    if (condition.compareGlobal.has_value()) {
                        condition.condition.comparison_raw = encodeKey(condition.compareGlobal);
                    }
                    encoded.push_back(std::move(condition.condition));
                }
                carryConditionPadding(
                    previousLogEntryConditions(previousStages, stage.index, entryIndex), encoded);
                auto run = fo4::serialize_ctda_run(encoded);
                auto& members = stage.log_entries[entryIndex].members;
                members.insert(members.begin(), run.begin(), run.end());
            }
        }
        fo4::forms::write_quest_stages(mutated, questKey, stages);
        if (localizedTableChanged) {
#ifdef FO4IDE_TEST_HOOKS
            if (localizedQuestBeforeCommitHook != nullptr) localizedQuestBeforeCommitHook();
#endif
            const auto baseline = std::find_if(
                localizationState->participants.begin(), localizationState->participants.end(),
                [&](const StagedLocalizationFile& file) { return support::sameLocalizedPath(file.path, localizedTables->dlstringsPath); });
            if (baseline == localizationState->participants.end()) {
                return std::unexpected(Error{"LocalizedLogTableInvalid", "DLSTRINGS baseline is missing"});
            }
            auto staged = prepareStringTable(
                loaded->name, localizedTables->language, StringSource::dl,
                localizedTables->dlstrings, localizedTables->dlstringsPath.parent_path(), &*baseline);
            if (!staged) return std::unexpected(staged.error());
            const auto participant = std::find_if(
                localizationState->participants.begin(), localizationState->participants.end(),
                [&](const StagedLocalizationFile& file) { return support::sameLocalizedPath(file.path, staged->file.path); });
            if (participant == localizationState->participants.end()) {
                return std::unexpected(Error{"LocalizedLogTableInvalid", "DLSTRINGS baseline is missing"});
            }
            *participant = staged->file;
            auto stagedResult = stageQuestLocalizationMutationLocked(
                *loaded, std::move(*localizationState), std::move(mutated), StringSource::dl,
                std::move(*staged), std::move(*sourceAllocation));
            if (!stagedResult) return std::unexpected(stagedResult.error());
            return nlohmann::json(
                "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
                recordText + " in " + pluginName + ". SavePlugin to persist.");
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Set " + std::to_string(stages.stages.size()) + " quest stage(s) on " +
            recordText + " in " + pluginName + ". SavePlugin to persist.");
    } catch (const fo4::forms::QuestError& error) {
        const std::string message = error.what();
        if (message.find("QUST VMAD") != std::string::npos) {
            return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", message});
        }
        return std::unexpected(Error{"SetQuestStagesFailed", message});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetQuestStagesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
