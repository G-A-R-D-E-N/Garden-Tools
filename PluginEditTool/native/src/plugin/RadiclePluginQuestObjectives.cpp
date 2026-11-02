#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/forms/quest.hpp>
#include <fo4/graph/conflict_graph.hpp>
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

namespace {

constexpr const char* kObjectiveError = "InvalidQuestObjectivePayload";

bool knownObjectiveKey(std::string_view key) {
    return key == "index" || key == "flags" || key == "displayText" || key == "targets";
}

bool knownTargetKey(std::string_view key) {
    return key == "alias" || key == "flags" || key == "conditions";
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

Result<nlohmann::json> RadiclePluginBackend::getQuestObjectives(const nlohmann::json& arguments) const {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    if (recordText.empty()) return std::unexpected(Error{"RecordMissing", "Record is empty"});

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});

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
        return std::unexpected(Error{"RecordTypeMismatch", "GetQuestObjectives requires a QUST record"});
    }

    try {
        const auto objectives = fo4::forms::decode_quest_objectives(loaded->plugin, questKey);
        const auto aliases = fo4::forms::decode_quest_aliases(loaded->plugin, questKey);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& objective : objectives.objectives) {
            nlohmann::json targets = nlohmann::json::array();
            for (const auto& target : objective.targets) {
                nlohmann::json row{{"alias", target.alias}, {"flags", target.flags}};
                for (const auto& alias : aliases.aliases) {
                    if (alias.kind != fo4::forms::QuestAliasKind::collection &&
                        static_cast<std::int32_t>(alias.id) == target.alias && alias.name.has_value()) {
                        row["aliasName"] = *alias.name;
                    }
                }
                if (target.keyword.has_value()) row["keyword"] = target.keyword->to_string();
                targets.push_back(std::move(row));
            }
            const auto text = objective.display_text.has_value() && !objective.display_text->localized()
                                  ? fo4::text::windows1252_to_utf8(objective.display_text->inline_text())
                                  : std::string{};
            out.push_back({{"index", objective.index}, {"flags", objective.flags},
                           {"displayText", text}, {"targets", targets}});
        }
        return nlohmann::json{{"objectives", out}};
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetQuestObjectivesFailed", error.what()});
    }
}

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
    if (loaded->plugin.header.tes4_flags.is_localized()) {
        return std::unexpected(Error{
            "LocalizedObjectiveTextUnsupported",
            "SetQuestObjectives writes plain objective text; a localized plugin needs a string-table ID"});
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
            const auto encoded = fo4::text::utf8_to_windows1252(displayText->get<std::string>());
            if (!encoded || encoded->find('\0') != std::string::npos) {
                return std::unexpected(Error{kObjectiveError, "objective displayText must be Windows-1252 text without NUL bytes"});
            }
            fo4::schema::LocalizedString text;
            text.text = *encoded;
            objective.display_text = text;

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
            objectives.objectives.push_back(std::move(objective));
        }

        auto mutated = loaded->plugin;
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
