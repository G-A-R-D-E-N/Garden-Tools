#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstdint>
#include <array>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/environment/strings.hpp>
#include <fo4/forms/quest.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include "RadiclePluginSupport.hpp"
#include "RadiclePluginQuestInternal.hpp"
#include "RadiclePluginReadJson.hpp"

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

struct QuestReadContext {
    std::string language{};
    std::optional<std::filesystem::path> stringsDirectory{};
};

std::optional<Error> parseQuestReadContext(const nlohmann::json& arguments,
                                          std::optional<QuestReadContext>& context) {
    if (!arguments.is_array() || arguments.size() <= 2 || arguments[2].is_null()) return std::nullopt;
    const auto& value = arguments[2];
    if (!value.is_object() || !value.contains("language") || !value["language"].is_string()) {
        return Error{"InvalidLocalizedQuestContext", "quest read context requires a string language"};
    }
    auto language = fo4::text::fold(value["language"].get<std::string>());
    static constexpr std::array<std::string_view, 11> supported{
        "en", "de", "it", "es", "esmx", "fr", "pl", "ptbr", "cn", "ru", "ja"};
    if (std::find(supported.begin(), supported.end(), language) == supported.end()) {
        return Error{"InvalidLocalizedQuestContext", "unsupported localized quest language: " + language};
    }
    QuestReadContext parsed{std::move(language), std::nullopt};
    if (value.contains("stringsDirectory")) {
        if (!value["stringsDirectory"].is_string() || value["stringsDirectory"].get<std::string>().empty()) {
            return Error{"InvalidLocalizedQuestContext", "stringsDirectory must be a non-empty path"};
        }
        parsed.stringsDirectory = fo4::io::from_utf8(value["stringsDirectory"].get<std::string>());
    }
    context = std::move(parsed);
    return std::nullopt;
}

const fo4::env::PluginStringTable* findTable(
    const std::vector<fo4::env::PluginStringTable>& tables, fo4::env::StringSource source) {
    const auto found = std::find_if(tables.begin(), tables.end(), [source](const auto& table) {
        return table.source == source;
    });
    return found == tables.end() ? nullptr : &*found;
}

constexpr std::uint32_t kNoLocalizedText = 0;

Result<std::optional<std::string>> resolveQuestText(const std::vector<fo4::env::PluginStringTable>& tables,
                                                    fo4::env::StringSource source, std::uint32_t id,
                                                    std::string_view language) {
    const auto* table = findTable(tables, source);
    if (table == nullptr) {
        return std::unexpected(Error{"StringTableUnavailable", "localized quest text table is unavailable"});
    }
    const auto* entry = fo4::localization::find_string(table->table, id);
    if (entry == nullptr) return std::optional<std::string>{};
    return std::optional<std::string>{support::displayText(language, entry->text)};
}

nlohmann::json logTextJson(const std::optional<fo4::schema::LocalizedString>& text) {
    nlohmann::json out = nlohmann::json::object();
    if (!text.has_value()) return out;
    if (text->localized()) {
        out["logTextId"] = *text->id;
    } else {
        out["logText"] = fo4::text::windows1252_to_utf8(text->inline_text());
    }
    return out;
}

struct NamedFlagBit {
    std::uint32_t bit;
    const char* name;
};

constexpr std::array<NamedFlagBit, 3> kStageFlagNames{{
    {static_cast<std::uint32_t>(fo4::forms::QuestStageFlag::RunOnStart), "RunOnStart"},
    {static_cast<std::uint32_t>(fo4::forms::QuestStageFlag::RunOnStop), "RunOnStop"},
    {static_cast<std::uint32_t>(fo4::forms::QuestStageFlag::KeepInstanceDataFromHereOn), "KeepInstanceDataFromHereOn"},
}};

constexpr std::array<NamedFlagBit, 2> kObjectiveFlagNames{{
    {static_cast<std::uint32_t>(fo4::forms::QuestObjectiveFlag::ORedWithPrevious), "ORedWithPrevious"},
    {static_cast<std::uint32_t>(fo4::forms::QuestObjectiveFlag::NoStatsTracking), "NoStatsTracking"},
}};

template <std::size_t N>
void addNamedFlags(nlohmann::json& row, std::uint32_t flags, const std::array<NamedFlagBit, N>& names) {
    nlohmann::json set = nlohmann::json::array();
    std::uint32_t known = 0;
    for (const auto& flag : names) {
        if ((flags & flag.bit) == 0) continue;
        set.push_back(flag.name);
        known |= flag.bit;
    }
    row["flagNames"] = std::move(set);
    row["unknownFlagBits"] = flags & ~known;
}

}  // namespace

Result<std::vector<fo4::env::PluginStringTable>> RadiclePluginBackend::readQuestStringTables(
    const LoadedPlugin& loaded, std::string_view language,
    const std::optional<std::filesystem::path>& stringsDirectory) const {
    if (!stringsDirectory) return readStringTables(loaded, language);
    const auto allowed = allowedLocalizationDirectory(*stringsDirectory);
    if (!allowed) {
        return std::unexpected(Error{"UntrustedStringTableDirectory",
                                     "refusing to read localized quest text outside a trusted root"});
    }
    std::error_code ec;
    const auto adjacent = std::filesystem::weakly_canonical(loaded.path.parent_path() / "Strings", ec);
    if (!ec && support::sameLocalizedPath(*allowed, adjacent)) return readStringTables(loaded, language);
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (testJournalRoot_.empty()) return std::unexpected(Error{"LocalizedSourceReadUnavailable", "custom source reads are unavailable"});
    std::vector<fo4::env::PluginStringTable> tables;
    for (const auto& [source, kind] : {
             std::pair{fo4::env::StringSource::normal, fo4::localization::StringTableKind::strings},
             std::pair{fo4::env::StringSource::dl, fo4::localization::StringTableKind::dlstrings},
             std::pair{fo4::env::StringSource::il, fo4::localization::StringTableKind::ilstrings}}) {
        const auto path = *allowed / fo4::localization::string_table_path(loaded.name, kind, language).filename();
        if (const auto staged = stagedStringTable(loaded.name, language, source, path))
            tables.push_back({source, path, {}, staged->table});
    }
    if (tables.empty()) return std::unexpected(Error{"LocalizedSourceReadUnavailable", "custom saved table reads require held-root authorization"});
    return tables;
#else
    return std::unexpected(Error{"LocalizedSourceReadUnavailable", "custom source reads require held-root authorization"});
#endif
}

Result<nlohmann::json> RadiclePluginBackend::getQuestStages(const nlohmann::json& arguments) const {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    std::optional<QuestReadContext> context;
    if (const auto error = parseQuestReadContext(arguments, context)) return std::unexpected(*error);
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
        return std::unexpected(Error{"RecordTypeMismatch", "GetQuestStages requires a QUST record"});
    }

    try {
        const auto stages = fo4::forms::decode_quest_stages(loaded->plugin, questKey);
        std::vector<fo4::env::PluginStringTable> tables;
        const bool needsLocalizedText = std::any_of(stages.stages.begin(), stages.stages.end(), [](const auto& stage) {
            return std::any_of(stage.log_entries.begin(), stage.log_entries.end(), [](const auto& entry) {
                return entry.log_text && entry.log_text->id && *entry.log_text->id != kNoLocalizedText;
            });
        });
        if (context && needsLocalizedText) {
            auto read = readQuestStringTables(*loaded, context->language, context->stringsDirectory);
            if (!read) return std::unexpected(read.error());
            tables = std::move(*read);
        }
        nlohmann::json out = nlohmann::json::array();
        for (const auto& stage : stages.stages) {
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& entry : stage.log_entries) {
                auto row = logTextJson(entry.log_text);
                if (context && entry.log_text && entry.log_text->id) {
                    row["logText"] = std::string{};
                    if (*entry.log_text->id != kNoLocalizedText) {
                        auto text = resolveQuestText(tables, fo4::env::StringSource::dl,
                                                     *entry.log_text->id, context->language);
                        if (!text) return std::unexpected(text.error());
                        if (text->has_value()) row["logText"] = std::move(**text);
                        else row["textMissing"] = true;
                    }
                }
                row["flags"] = entry.flags;
                if (entry.note.has_value()) {
                    row["note"] = fo4::text::windows1252_to_utf8(*entry.note);
                }
                std::vector<fo4::CtdaCondition> conditions;
                std::size_t consumed = 0;
                bool unreadable = !fo4::forms::lossless_quest_log_entry_conditions(
                    entry.members, conditions, consumed);
                if (!unreadable) {
                    unreadable = std::any_of(conditions.begin(), conditions.end(),
                                             [](const fo4::CtdaCondition& condition) {
                                                 return nonFiniteConditionError(condition).has_value();
                                             });
                }
                if (unreadable) {
                    row["conditionsUnreadable"] = true;
                } else {
                    row["conditions"] = readjson::conditionsJson(loaded->plugin, conditions);
                }
                entries.push_back(std::move(row));
            }
            nlohmann::json row{{"index", stage.index}, {"flags", stage.flags},
                               {"startUp", (stage.flags & static_cast<std::uint8_t>(
                                                              fo4::forms::QuestStageFlag::RunOnStart)) != 0},
                               {"logEntries", entries}};
            addNamedFlags(row, stage.flags, kStageFlagNames);
            out.push_back(std::move(row));
        }
        return nlohmann::json{{"stages", out}};
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetQuestStagesFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getQuestObjectives(const nlohmann::json& arguments) const {
    const auto pluginName = questStringArg(arguments, 0);
    const auto recordText = questStringArg(arguments, 1);
    std::optional<QuestReadContext> context;
    if (const auto error = parseQuestReadContext(arguments, context)) return std::unexpected(*error);
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
        std::vector<fo4::env::PluginStringTable> tables;
        const bool needsLocalizedText = std::any_of(objectives.objectives.begin(), objectives.objectives.end(),
                                                     [](const auto& objective) {
                                                         return objective.display_text &&
                                                                objective.display_text->id &&
                                                                *objective.display_text->id != kNoLocalizedText;
                                                     });
        if (context && needsLocalizedText) {
            auto read = readQuestStringTables(*loaded, context->language, context->stringsDirectory);
            if (!read) return std::unexpected(read.error());
            tables = std::move(*read);
        }
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
            std::string text;
            nlohmann::json row{{"index", objective.index}, {"flags", objective.flags}, {"targets", targets}};
            if (objective.display_text) {
                if (objective.display_text->id) {
                    if (context) {
                        row["displayTextId"] = *objective.display_text->id;
                        if (*objective.display_text->id != kNoLocalizedText) {
                            auto resolved = resolveQuestText(tables, fo4::env::StringSource::normal,
                                                             *objective.display_text->id, context->language);
                            if (!resolved) return std::unexpected(resolved.error());
                            if (resolved->has_value()) text = std::move(**resolved);
                            else row["textMissing"] = true;
                        }
                    }
                } else {
                    text = fo4::text::windows1252_to_utf8(objective.display_text->inline_text());
                }
            }
            row["displayText"] = text;
            addNamedFlags(row, objective.flags, kObjectiveFlagNames);
            out.push_back(std::move(row));
        }
        return nlohmann::json{{"objectives", out}};
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetQuestObjectivesFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::getQuestAliases(const nlohmann::json& arguments) const {
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
        return std::unexpected(Error{"RecordTypeMismatch", "GetQuestAliases requires a QUST record"});
    }

    try {
        const auto ownerTypes = questOwnerTypes(loaded->plugin, *graph_, false);
        const auto aliases = fo4::forms::decode_quest_aliases(loaded->plugin, questKey, ownerTypes);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& alias : aliases.aliases) {
            out.push_back(questAliasJson(loaded->plugin, alias, ownerTypes));
        }
        return nlohmann::json{{"nextAliasId", aliases.next_alias_id}, {"aliases", out}};
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetQuestAliasesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
