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

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

struct QuestReadContext {
    std::string language;
    std::optional<std::filesystem::path> stringsDirectory;
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

Result<std::string> resolveQuestText(const std::vector<fo4::env::PluginStringTable>& tables,
                                     fo4::env::StringSource source, std::uint32_t id,
                                     std::string_view language) {
    const auto* table = findTable(tables, source);
    if (table == nullptr) {
        return std::unexpected(Error{"StringTableUnavailable", "localized quest text table is unavailable"});
    }
    const auto* entry = fo4::localization::find_string(table->table, id);
    if (entry == nullptr) {
        return std::unexpected(Error{"StringEntryMissing", "localized quest text ID is missing: " +
                                                             std::to_string(id)});
    }
    return support::displayText(language, entry->text);
}

const char* aliasKindName(fo4::forms::QuestAliasKind kind) {
    switch (kind) {
        case fo4::forms::QuestAliasKind::location:
            return "location";
        case fo4::forms::QuestAliasKind::collection:
            return "collection";
        case fo4::forms::QuestAliasKind::reference:
            break;
    }
    return "reference";
}

nlohmann::json logTextJson(const std::optional<fo4::schema::LocalizedString>& text) {
    nlohmann::json out{{"logText", std::string{}}};
    if (!text.has_value()) return out;
    if (text->localized()) {
        out["logTextId"] = *text->id;
    } else {
        out["logText"] = fo4::text::windows1252_to_utf8(text->inline_text());
    }
    return out;
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
                return entry.log_text && entry.log_text->id;
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
                    auto text = resolveQuestText(tables, fo4::env::StringSource::dl,
                                                 *entry.log_text->id, context->language);
                    if (!text) return std::unexpected(text.error());
                    row["logText"] = *text;
                }
                row["flags"] = entry.flags;
                row["note"] = fo4::text::windows1252_to_utf8(entry.note.value_or(std::string{}));
                entries.push_back(std::move(row));
            }
            out.push_back({{"index", stage.index}, {"flags", stage.flags},
                           {"startUp", (stage.flags & 0x02u) != 0},
                           {"logEntries", entries}});
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
                                                                objective.display_text->id;
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
                        auto resolved = resolveQuestText(tables, fo4::env::StringSource::normal,
                                                         *objective.display_text->id, context->language);
                        if (!resolved) return std::unexpected(resolved.error());
                        text = std::move(*resolved);
                    }
                } else {
                    text = fo4::text::windows1252_to_utf8(objective.display_text->inline_text());
                }
            }
            row["displayText"] = text;
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
        const auto aliases = fo4::forms::decode_quest_aliases(loaded->plugin, questKey);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& alias : aliases.aliases) {
            nlohmann::json row{{"kind", aliasKindName(alias.kind)}, {"id", alias.id}};
            if (alias.name.has_value()) row["name"] = *alias.name;
            if (alias.flags.has_value()) row["flags"] = *alias.flags;
            if (alias.kind == fo4::forms::QuestAliasKind::collection) {
                nlohmann::json entries = nlohmann::json::array();
                for (const auto& entry : alias.collection_entries) {
                    nlohmann::json item{{"id", entry.id}};
                    if (entry.max_initial_fill_count.has_value()) {
                        item["maxInitialFillCount"] = *entry.max_initial_fill_count;
                    }
                    entries.push_back(std::move(item));
                }
                row["collectionEntries"] = entries;
            }
            out.push_back(std::move(row));
        }
        return nlohmann::json{{"nextAliasId", aliases.next_alias_id}, {"aliases", out}};
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetQuestAliasesFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
