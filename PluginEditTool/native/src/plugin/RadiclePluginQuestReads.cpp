#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstdint>
#include <string>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/signature.hpp>
#include <fo4/text/encoding.hpp>

#include "RadiclePluginQuestInternal.hpp"

namespace fo4ide::plugin {

using namespace questinternal;

namespace {

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

Result<nlohmann::json> RadiclePluginBackend::getQuestStages(const nlohmann::json& arguments) const {
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
        return std::unexpected(Error{"RecordTypeMismatch", "GetQuestStages requires a QUST record"});
    }

    try {
        const auto stages = fo4::forms::decode_quest_stages(loaded->plugin, questKey);
        nlohmann::json out = nlohmann::json::array();
        for (const auto& stage : stages.stages) {
            nlohmann::json entries = nlohmann::json::array();
            for (const auto& entry : stage.log_entries) {
                auto row = logTextJson(entry.log_text);
                row["flags"] = entry.flags;
                row["note"] = fo4::text::windows1252_to_utf8(entry.note.value_or(std::string{}));
                entries.push_back(std::move(row));
            }
            out.push_back({{"index", stage.index}, {"flags", stage.flags}, {"logEntries", entries}});
        }
        return nlohmann::json{{"stages", out}};
    } catch (const fo4::forms::QuestError& error) {
        return std::unexpected(Error{"QuestAuthoringCoverageIncomplete", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"GetQuestStagesFailed", error.what()});
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
