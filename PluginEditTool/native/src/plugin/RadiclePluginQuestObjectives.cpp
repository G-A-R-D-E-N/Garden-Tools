#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/graph/conflict_graph.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/plugin/container.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4/schema/ctda.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include "RadiclePluginQuestInternal.hpp"

namespace fo4ide::plugin {

using namespace questinternal;
using fo4::env::StringSource;

namespace {

constexpr const char* kObjectiveError = "InvalidQuestObjectivePayload";

bool knownObjectiveKey(std::string_view key) {
    return key == "index" || key == "flags" || key == "displayText" || key == "targets" ||
           key == "displayTextId" || key == "textMissing" || key == "flagNames" || key == "unknownFlagBits";
}

bool knownTargetKey(std::string_view key) {
    return key == "alias" || key == "flags" || key == "conditions" || key == "aliasName" ||
           key == "keyword";
}

bool isConditionSubrecord(fo4::Signature signature) {
    const auto text = signature.to_string();
    return text == "CTDA" || text == "CIS1" || text == "CIS2";
}

std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::optional<std::int32_t> resolveTargetAlias(const fo4::forms::QuestAliases& aliases,
                                               const nlohmann::json& value,
                                               std::optional<Error>& error) {
    if (value.is_number_integer()) {
        const auto id = value.get<std::int64_t>();
        for (const auto& alias : aliases.aliases) {
            if (alias.kind != fo4::forms::QuestAliasKind::collection &&
                static_cast<std::int64_t>(alias.id) == id) {
                return static_cast<std::int32_t>(alias.id);
            }
        }
        error = Error{std::string(kObjectiveError),
                      "objective target alias ID " + std::to_string(id) + " is not an alias of this quest"};
        return std::nullopt;
    }
    if (value.is_string()) {
        const auto wanted = asciiLower(value.get<std::string>());
        for (const auto& alias : aliases.aliases) {
            if (alias.kind == fo4::forms::QuestAliasKind::collection || !alias.name.has_value()) continue;
            if (asciiLower(*alias.name) == wanted) return static_cast<std::int32_t>(alias.id);
        }
    }
    error = Error{std::string(kObjectiveError),
                  "objective target must name an alias of this quest by ID or name; a reference or other form is refused"};
    return std::nullopt;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::setQuestObjectives(const nlohmann::json& arguments) {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    const auto* objectivesJson = questArrayArg(arguments, 2);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (recordText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});
    if (objectivesJson == nullptr) {
        return std::unexpected(Error{kObjectiveError, "SetQuestObjectives requires a JSON array of objective objects"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);

    fo4::FormKey questKey;
    try {
        questKey = fo4::FormKey::parse(recordText);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
    const auto* record = fo4::find_record(loaded->plugin, questKey);
    if (record == nullptr) {
        return std::unexpected(Error{"RecordNotFound", "Record is not present in " + pluginName + ": " + recordText});
    }
    if (record->header.signature != fo4::Signature::from_string("QUST")) {
        return std::unexpected(Error{"RecordTypeMismatch", "SetQuestObjectives requires a QUST record"});
    }
    const bool localized = loaded->plugin.header.tes4_flags.is_localized();
    std::optional<LocalizedQuestTables> localizedTables;
    std::optional<StagedLocalization> localizationState;
    std::optional<LocalizedStringAllocation> sourceAllocation;
    if (localized) {
        localizedTables.emplace();
        std::filesystem::path stringsDirectory;
        if (arguments.is_array() && arguments.size() > 3 && arguments[3].is_object() &&
            arguments[3].contains("stringsDirectory") && arguments[3]["stringsDirectory"].is_string()) {
            stringsDirectory = fo4::io::from_utf8(arguments[3]["stringsDirectory"].get<std::string>());
        }
        bool allowAbsent = false;
#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
        allowAbsent = !stringsDirectory.empty() && !testJournalRoot_.empty() && !dataDirectory_.empty() &&
            support::sameLocalizedPath(stringsDirectory, loaded->path.parent_path() / "Strings");
#endif
        const auto allowedDirectory = stringsDirectory.empty()
                                          ? std::nullopt
                                          : allowedLocalizationDirectory(stringsDirectory, allowAbsent);
        if (!stringsDirectory.empty() && !allowedDirectory.has_value()) {
            return std::unexpected(Error{
                "LocalizedSidecarPathNotAllowed",
                "Refusing to write localized sidecars outside a trusted writable root: " +
                    stringsDirectory.string()
            });
        }
        if (const auto error = loadLocalizedQuestTables(
                arguments, loaded->path, allowedDirectory.value_or(std::filesystem::path{}),
                *localizedTables, allowAbsent)) {
            return std::unexpected(*error);
        }
        auto state = loaded->stagedLocalization.value_or(StagedLocalization{});
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
            const auto previous = std::find_if(state.participants.begin(), state.participants.end(),
                [&](const StagedLocalizationFile& item) { return support::sameLocalizedPath(item.path, file->path); });
            if (previous == state.participants.end()) state.participants.push_back(std::move(*file));
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
        if (maximum == std::numeric_limits<std::uint32_t>::max()) return std::unexpected(Error{"LocalizedObjectiveTableFull", "localized string-table ID space is exhausted"});
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
        localizationState = std::move(state);
    }

    try {
        const auto previous = fo4::forms::decode_quest_objectives(loaded->plugin, questKey);
        const auto aliases = fo4::forms::decode_quest_aliases(loaded->plugin, questKey);
        std::map<std::uint16_t, const fo4::forms::QuestObjective*> previousByIndex;
        for (const auto& objective : previous.objectives) previousByIndex.emplace(objective.index, &objective);

        fo4::forms::QuestObjectives objectives;
        std::vector<fo4::FormKey> conditionKeys;
        std::vector<std::vector<std::vector<PendingQuestCondition>>> pendingConditions;
        std::vector<std::vector<bool>> replaceConditions;

        for (const auto& element : *objectivesJson) {
            if (!element.is_object()) {
                return std::unexpected(Error{kObjectiveError, "Every quest objective must be an object"});
            }
            if (const auto error = rejectUnknownKeys(element, knownObjectiveKey, "quest objective", kObjectiveError)) {
                return std::unexpected(*error);
            }
            std::uint32_t index = 0;
            std::uint32_t flags = 0;
            if (!questUnsigned(element, "index", 0, index) ||
                index > std::numeric_limits<std::uint16_t>::max()) {
                return std::unexpected(Error{kObjectiveError, "objective index must be 0..65535"});
            }
            if (!questUnsigned(element, "flags", 0, flags)) {
                return std::unexpected(Error{kObjectiveError, "objective flags must be an unsigned 32-bit integer"});
            }
            fo4::forms::QuestObjective objective;
            objective.index = static_cast<std::uint16_t>(index);
            objective.flags = flags;
            const auto displayText = element.find("displayText");
            if (displayText == element.end() || !displayText->is_string()) {
                return std::unexpected(Error{kObjectiveError, "objective displayText must be a string"});
            }
            const auto sourceText = displayText->get<std::string>();
            if (sourceText.find('\0') != std::string::npos) {
                return std::unexpected(Error{kObjectiveError, "objective displayText cannot contain NUL bytes"});
            }
            const auto encoded = localized ? std::optional<std::string>{sourceText}
                                           : fo4::text::utf8_to_windows1252(sourceText);
            if (!encoded) {
                return std::unexpected(Error{kObjectiveError, "objective displayText must be Windows-1252 text without NUL bytes"});
            }
            fo4::schema::LocalizedString text;
            text.text = *encoded;
            objective.display_text = text;
            bool textMissing = false;
            if (const auto field = element.find("textMissing"); field != element.end() && !field->is_null()) {
                if (!field->is_boolean()) {
                    return std::unexpected(Error{kObjectiveError, "objective textMissing must be a boolean"});
                }
                textMissing = field->get<bool>();
            }
            const auto existingText = previousByIndex.find(objective.index);
            const bool hasExistingTextId = existingText != previousByIndex.end() &&
                                           existingText->second->display_text.has_value() &&
                                           existingText->second->display_text->id.has_value();
            if (textMissing && !hasExistingTextId) {
                return std::unexpected(Error{kObjectiveError,
                    "objective textMissing row has no existing text ID to keep; reload the quest"});
            }

            const auto targetsJson = element.find("targets");
            if (targetsJson == element.end() || !targetsJson->is_array()) {
                return std::unexpected(Error{kObjectiveError, "objective targets must be an array"});
            }
            const auto previousObjective = previousByIndex.find(objective.index);
            std::vector<std::int32_t> matchedAliases;
            pendingConditions.emplace_back();
            replaceConditions.emplace_back();
            for (std::size_t position = 0; position < targetsJson->size(); ++position) {
                const auto& targetJson = (*targetsJson)[position];
                if (!targetJson.is_object()) {
                    return std::unexpected(Error{kObjectiveError, "every objective target must be an object"});
                }
                if (const auto error = rejectUnknownKeys(targetJson, knownTargetKey, "objective target", kObjectiveError)) {
                    return std::unexpected(*error);
                }
                const auto aliasJson = targetJson.find("alias");
                if (aliasJson == targetJson.end()) {
                    return std::unexpected(Error{kObjectiveError, "every objective target needs an alias ID or name"});
                }
                std::optional<Error> aliasError;
                const auto alias = resolveTargetAlias(aliases, *aliasJson, aliasError);
                if (!alias) return std::unexpected(*aliasError);

                fo4::forms::QuestObjectiveTarget target;
                target.alias = *alias;
                std::uint32_t targetFlags = 0;
                if (!questUnsigned(targetJson, "flags", 0, targetFlags)) {
                    return std::unexpected(Error{kObjectiveError, "objective target flags must be an unsigned 32-bit integer"});
                }
                target.flags = targetFlags;

                const fo4::forms::QuestObjectiveTarget* before = nullptr;
                if (previousObjective != previousByIndex.end()) {
                    for (const auto& oldTarget : previousObjective->second->targets) {
                        if (oldTarget.alias != *alias) continue;
                        if (before != nullptr || std::find(matchedAliases.begin(), matchedAliases.end(), *alias) != matchedAliases.end()) {
                            return std::unexpected(Error{kObjectiveError, "ambiguous repeated objective target alias"});
                        }
                        before = &oldTarget;
                    }
                }
                if (before != nullptr) matchedAliases.push_back(*alias);
                if (targetJson.contains("keyword") && !targetJson["keyword"].is_null()) {
                    if (!targetJson["keyword"].is_string()) {
                        return std::unexpected(Error{kObjectiveError, "objective target keyword must be a FormKey string"});
                    }
                    fo4::FormKey wanted;
                    try {
                        wanted = fo4::FormKey::parse(targetJson["keyword"].get<std::string>());
                    } catch (const std::exception&) {
                        return std::unexpected(Error{kObjectiveError, "objective target keyword must be a FormKey string"});
                    }
                    const bool same = before != nullptr && before->keyword.has_value() &&
                                      *before->keyword == wanted;
                    if (!same) {
                        return std::unexpected(Error{kObjectiveError, "objective target keyword cannot be changed by SetQuestObjectives; it is kept from the existing target"});
                    }
                }
                std::vector<PendingQuestCondition> parsed;
                const bool conditionsGiven = targetJson.contains("conditions");
                if (conditionsGiven) {
                    if (!targetJson["conditions"].is_array()) {
                        return std::unexpected(Error{kObjectiveError, "objective target conditions must be an array"});
                    }
                    for (const auto& conditionJson : targetJson["conditions"]) {
                        PendingQuestCondition pending;
                        if (const auto error = parseQuestCondition(
                                conditionJson, graph_, pending, conditionKeys, kObjectiveError,
                                "quest objective targets")) {
                            return std::unexpected(*error);
                        }
                        parsed.push_back(std::move(pending));
                    }
                }
                pendingConditions.back().push_back(std::move(parsed));
                replaceConditions.back().push_back(conditionsGiven);
                objective.targets.push_back(std::move(target));
            }
            if (std::any_of(objectives.objectives.begin(), objectives.objectives.end(),
                            [&](const auto& existing) { return existing.index == objective.index; })) {
                return std::unexpected(Error{kObjectiveError, "duplicate objective index " + std::to_string(objective.index)});
            }
            objectives.objectives.push_back(std::move(objective));
        }

        auto mutated = loaded->plugin;
        bool localizedTableChanged = false;
        if (localizedTables.has_value()) {
            for (auto& objective : objectives.objectives) {
                auto& text = *objective.display_text;
                std::vector<std::byte> encodedText;
                if (const auto error = encodeLocalizedText(
                        localizedTables->language, *text.text, encodedText)) {
                    return std::unexpected(*error);
                }
                std::optional<std::uint32_t> id;
                const auto previousObjective = previousByIndex.find(objective.index);
                if (previousObjective != previousByIndex.end() &&
                    previousObjective->second->display_text.has_value() &&
                    previousObjective->second->display_text->id.has_value()) {
                    const auto previousId = *previousObjective->second->display_text->id;
                    const auto* previousText = fo4::localization::find_string(
                        localizedTables->strings, previousId);
                    if (encodedText.empty() && previousText == nullptr) {
                        id = previousId;
                    } else if (previousText != nullptr && previousText->text == encodedText) {
                        id = previousId;
                    } else if (encodedText.empty()) {
                        id = 0u;
                    }
                }
                if (!id.has_value()) {
                    if (localizedTables->nextId == std::numeric_limits<std::uint32_t>::max()) {
                        return std::unexpected(Error{
                            "LocalizedObjectiveTableFull",
                            "localized string-table ID space is exhausted"
                        });
                    }
                    id = localizedTables->nextId++;
                    localizedTables->strings.entries.push_back({*id, std::move(encodedText)});
                    localizedTableChanged = true;
                }
                text.id = id;
                text.text.reset();
            }
        }
        for (const auto& key : conditionKeys) ensureOwnerMaster(mutated, key.owner());
        const auto remappedPrevious = fo4::forms::decode_quest_objectives(mutated, questKey);
        const auto context = fo4::plugin_form_id_context(mutated);
        const auto encodeKey = [&](const std::optional<fo4::FormKey>& key) -> std::uint32_t {
            if (!key.has_value()) return 0;
            return fo4::encode_plugin_form_id(*key, context).value();
        };
        for (std::size_t objectiveIndex = 0; objectiveIndex < objectives.objectives.size(); ++objectiveIndex) {
            auto& objective = objectives.objectives[objectiveIndex];
            for (std::size_t position = 0; position < objective.targets.size(); ++position) {
                const auto& pending = pendingConditions[objectiveIndex][position];
                auto& target = objective.targets[position];
                for (const auto& oldObjective : remappedPrevious.objectives) {
                    if (oldObjective.index != objective.index) continue;
                    for (const auto& before : oldObjective.targets) {
                        if (before.alias != target.alias) continue;
                        target.keyword = before.keyword;
                        target.keyword_unresolved = before.keyword_unresolved;
                        for (const auto& member : before.subrecords) {
                            if (replaceConditions[objectiveIndex][position] && isConditionSubrecord(member.signature)) continue;
                            target.subrecords.push_back(member);
                        }
                        break;
                    }
                    break;
                }
                if (pending.empty()) continue;
                std::vector<fo4::CtdaCondition> encoded;
                encoded.reserve(pending.size());
                for (auto condition : pending) {
                    if (condition.param1.has_value()) {
                        condition.condition.param1 = static_cast<std::int32_t>(encodeKey(condition.param1));
                    }
                    if (condition.param2.has_value()) {
                        condition.condition.param2 = static_cast<std::int32_t>(encodeKey(condition.param2));
                    }
                    if (condition.reference.has_value()) {
                        condition.condition.reference = encodeKey(condition.reference);
                    }
                    if (condition.compareGlobal.has_value()) {
                        condition.condition.comparison_raw = encodeKey(condition.compareGlobal);
                    }
                    encoded.push_back(std::move(condition.condition));
                }
                auto run = fo4::serialize_ctda_run(encoded);
                target.subrecords.insert(target.subrecords.begin(), run.begin(), run.end());
            }
        }
        fo4::forms::write_quest_objectives(mutated, questKey, objectives);
        if (localizedTableChanged) {
#ifdef FO4IDE_TEST_HOOKS
            if (localizedQuestBeforeCommitHook != nullptr) localizedQuestBeforeCommitHook();
#endif
            const auto baseline = std::find_if(
                localizationState->participants.begin(), localizationState->participants.end(),
                [&](const StagedLocalizationFile& file) { return support::sameLocalizedPath(file.path, localizedTables->stringsPath); });
            if (baseline == localizationState->participants.end()) {
                return std::unexpected(Error{"LocalizedObjectiveTableInvalid", "STRINGS baseline is missing"});
            }
            auto staged = prepareStringTable(
                loaded->name, localizedTables->language, StringSource::normal,
                localizedTables->strings, localizedTables->stringsPath.parent_path(), &*baseline);
            if (!staged) return std::unexpected(staged.error());
            const auto participant = std::find_if(
                localizationState->participants.begin(), localizationState->participants.end(),
                [&](const StagedLocalizationFile& file) { return support::sameLocalizedPath(file.path, staged->file.path); });
            if (participant == localizationState->participants.end()) {
                return std::unexpected(Error{"LocalizedObjectiveTableInvalid", "STRINGS baseline is missing"});
            }
            *participant = staged->file;
            auto stagedResult = stageQuestLocalizationMutationLocked(
                *loaded, std::move(*localizationState), std::move(mutated), StringSource::normal,
                std::move(*staged), std::move(*sourceAllocation));
            if (!stagedResult) return std::unexpected(stagedResult.error());
            return nlohmann::json(
                "Set " + std::to_string(objectives.objectives.size()) + " quest objective(s) on " +
                recordText + " in " + pluginName + ". SavePlugin to persist.");
        }
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Set " + std::to_string(objectives.objectives.size()) + " quest objective(s) on " +
            recordText + " in " + pluginName + ". SavePlugin to persist.");
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetQuestObjectivesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
