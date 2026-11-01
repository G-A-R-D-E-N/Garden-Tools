#include "RadiclePluginSupport.hpp"
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "RadiclePluginFields.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/forms/eyes.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/formlink_lists.hpp>
#include <fo4/schema/scalar.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {

using support::editorId;

namespace {

std::size_t sizeArgument(const nlohmann::json& args,
                         std::size_t index,
                         std::size_t fallback,
                         std::size_t maximum = 5000) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) {
        return fallback;
    }
    const auto raw = args[index].get<long long>();
    if (raw < 0) return fallback;
    return std::min(static_cast<std::size_t>(raw), maximum);
}

std::string asciiLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool containsInsensitive(std::string_view value, std::string_view needle) {
    if (needle.empty()) return true;
    return asciiLower(value).find(asciiLower(needle)) != std::string::npos;
}

std::string pluginKind(const fo4::Plugin& plugin) {
    if (fo4::classify_plugin_kind(plugin.header.tes4_flags, plugin.key) == fo4::PluginKind::light) return "light";
    if (fo4::is_master_plugin(plugin.header.tes4_flags, plugin.key)) return "master";
    return "plugin";
}

std::string rawFormIdText(fo4::RawFormId formId) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << formId.value();
    return out.str();
}

std::string lastModifiedIso(const std::filesystem::path& path) {
    std::error_code ec;
    const auto fileTime = std::filesystem::last_write_time(path, ec);
    if (ec) return {};
    const auto sysTime = std::chrono::clock_cast<std::chrono::system_clock>(fileTime);
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(sysTime);
    const std::time_t tt = std::chrono::system_clock::to_time_t(seconds);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

using SubrecordMap = std::map<std::pair<std::string, std::size_t>, std::vector<std::byte>>;

SubrecordMap subrecordMap(const fo4::Record* record) {
    SubrecordMap out;
    if (record == nullptr) return out;
    std::map<std::string, std::size_t> ordinals;
    for (const auto& subrecord : record->subrecords) {
        const auto signature = subrecord.signature.to_string();
        out.emplace(std::make_pair(signature, ordinals[signature]++), subrecord.data);
    }
    return out;
}

std::size_t changedSubrecordCount(const fo4::Record* previous, const fo4::Record* current) {
    const auto before = subrecordMap(previous);
    const auto after = subrecordMap(current);
    std::map<std::pair<std::string, std::size_t>, bool> keys;
    for (const auto& [key, value] : before) {
        static_cast<void>(value);
        keys.emplace(key, true);
    }
    for (const auto& [key, value] : after) {
        static_cast<void>(value);
        keys.emplace(key, true);
    }

    std::size_t changed = 0;
    for (const auto& [key, unused] : keys) {
        static_cast<void>(unused);
        const auto lhs = before.find(key);
        const auto rhs = after.find(key);
        if (lhs == before.end() || rhs == after.end() || lhs->second != rhs->second) ++changed;
    }
    return changed;
}

Result<fo4::FormKey> formKeyArgument(const nlohmann::json& arguments) {
    const auto text = support::stringArgument(arguments, 0);
    if (text.empty()) {
        return std::unexpected(Error{"FormKeyMissing", "FormKey is empty"});
    }
    try {
        return fo4::FormKey::parse(text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidFormKey", error.what()});
    }
}

nlohmann::json gridColumns(std::string_view signature, std::size_t maxEntries = 0) {
    if (signature == "EYES") {
        return nlohmann::json::array({
            "EDID",
            "EYES\\Full",
            "EYES\\Icon",
            "EYES\\DataFlags"
        });
    }
    if (signature == "LVSP") {
        nlohmann::json columns = nlohmann::json::array({
            "EDID",
            "LVSP\\ChanceNone",
            "LVSP\\MaxCount",
            "LVSP\\Flags",
            "LVSP\\EntryCount"
        });
        for (std::size_t index = 0; index < maxEntries; ++index) {
            const auto prefix = "LVSP\\Entries[" + std::to_string(index) + "]\\";
            columns.push_back(prefix + "Level");
            columns.push_back(prefix + "Spell");
            columns.push_back(prefix + "Count");
            columns.push_back(prefix + "ChanceNone");
        }
        return columns;
    }

    nlohmann::json columns = nlohmann::json::array({"EDID"});
    if (signature.size() == 4) {
        for (const auto& field :
             fo4::schema::scalar_fields(fo4::Signature::from_string(signature))) {
            columns.push_back(std::string(field.path));
        }
        for (const auto& field :
             fo4::schema::formlink_fields(fo4::Signature::from_string(signature))) {
            columns.push_back(std::string(field.path));
        }
    }
    return columns;
}

}

Result<nlohmann::json> RadiclePluginBackend::getPluginRecordTypes(
    const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    // A typo'd plugin name must not be reported as "plugin with no records".
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    std::map<std::string, std::size_t> counts;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        ++counts[record->header.signature.to_string()];
    }

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& [signature, count] : counts) {
        rows.push_back(signature + " (" + std::to_string(count) + ")");
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordsGrid(
    const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto signature = support::stringArgument(arguments, 1);
    const auto limit = sizeArgument(arguments, 2, 200, 1000);
    const auto offset = sizeArgument(arguments, 3, 0, 1000000);

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    // An unknown plugin must be an error, not an empty grid: the two are
    // indistinguishable for the client otherwise.
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (signature.empty()) {
        return nlohmann::json(nlohmann::json{
            {"columns", nlohmann::json::array()},
            {"rows", nlohmann::json::array()},
            {"total", 0},
            {"offset", offset}
        }.dump());
    }

    std::vector<const fo4::Record*> matching;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == fo4::sig_tes4 || record->form_key.owner().empty()) continue;
        if (record->header.signature.to_string() == signature) matching.push_back(record);
    }

    const auto begin = std::min(offset, matching.size());
    const auto end = std::min(begin + limit, matching.size());
    std::vector<fo4::forms::LeveledSpell> lvspModels;
    std::size_t maxEntries = 0;
    if (signature == "LVSP") {
        lvspModels.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            try {
                auto model = fo4::forms::decode_leveled_spell(
                    loaded->plugin, matching[index]->form_key);
                maxEntries = std::max(maxEntries, model.entries.size());
                lvspModels.push_back(std::move(model));
            } catch (const fo4::forms::LeveledSpellError& error) {
                return std::unexpected(Error{
                    "LeveledSpellReadFailed",
                    matching[index]->form_key.to_string() + ": " + error.what()
                });
            }
        }
    }

    auto columns = gridColumns(signature, maxEntries);
    std::vector<std::vector<SemanticFieldCell>> semanticRows;
    nlohmann::json presentations = nlohmann::json::object();
    if (signature.size() == 4 && signature != "EYES" && signature != "LVSP") {
        columns = nlohmann::json::array({"EDID"});
        for (std::size_t index = begin; index < end; ++index) {
            auto projected = semanticFieldCells(loaded->plugin, *matching[index]);
            if (!projected) return std::unexpected(projected.error());
            for (const auto& cell : *projected) {
                if (std::find(columns.begin(), columns.end(), cell.path) == columns.end()) {
                    columns.push_back(cell.path);
                }
            }
            semanticRows.push_back(std::move(*projected));
        }
        const auto recordSignature = fo4::Signature::from_string(signature);
        for (const auto& column : columns) {
            const auto path = column.get<std::string>();
            auto presentation = scalarPresentationJson(recordSignature, path);
            if (!presentation.is_null()) presentations[path] = std::move(presentation);
        }
    }
    nlohmann::json rows = nlohmann::json::array();
    for (std::size_t index = begin; index < end; ++index) {
        const auto* record = matching[index];
        const auto editor = editorId(*record);
        nlohmann::json cells = nlohmann::json::array({editor});
        nlohmann::json editable = nlohmann::json::array({true});

        if (signature == "EYES") {
            try {
                const auto model = fo4::forms::decode_eyes(
                    loaded->plugin, record->form_key);
                if (model.full_text.has_value()) {
                    cells.push_back(*model.full_text);
                    editable.push_back(true);
                } else {
                    cells.push_back("<opaque/localized>");
                    editable.push_back(false);
                }
                cells.push_back(model.icon);
                cells.push_back(std::to_string(model.data_flags));
                editable.push_back(true);
                editable.push_back(true);
            } catch (const fo4::forms::EyesError& error) {
                return std::unexpected(Error{
                    "EyesReadFailed",
                    record->form_key.to_string() + ": " + error.what()
                });
            }
        } else if (signature == "LVSP") {
            const auto& model = lvspModels[index - begin];
            cells.push_back(std::to_string(model.chance_none));
            cells.push_back(std::to_string(model.max_count));
            cells.push_back(std::to_string(model.flags));
            cells.push_back(std::to_string(model.entries.size()));
            editable.push_back(true);
            editable.push_back(true);
            editable.push_back(true);
            editable.push_back(false);

            for (std::size_t entryIndex = 0; entryIndex < maxEntries; ++entryIndex) {
                if (entryIndex < model.entries.size()) {
                    const auto& entry = model.entries[entryIndex];
                    cells.push_back(std::to_string(entry.level));
                    cells.push_back(entry.spell.to_string());
                    cells.push_back(std::to_string(entry.count));
                    cells.push_back(std::to_string(entry.chance_none));
                    editable.push_back(true);
                    editable.push_back(true);
                    editable.push_back(true);
                    editable.push_back(true);
                } else {
                    cells.push_back("");
                    cells.push_back("");
                    cells.push_back("");
                    cells.push_back("");
                    editable.push_back(false);
                    editable.push_back(false);
                    editable.push_back(false);
                    editable.push_back(false);
                }
            }
        } else if (signature.size() == 4) {
            const auto& projected = semanticRows[index - begin];
            for (std::size_t column = 1; column < columns.size(); ++column) {
                const auto found = std::find_if(projected.begin(), projected.end(), [&](const auto& cell) {
                    return cell.path == columns[column].get<std::string>();
                });
                cells.push_back(found == projected.end() ? std::string{} : found->value);
                editable.push_back(found != projected.end() && found->available && found->writable && isEditable(*loaded));
            }
        }

        if (!isEditable(*loaded)) std::fill(editable.begin(), editable.end(), false);
        rows.push_back({
            {"formKey", record->form_key.to_string()},
            {"editorId", editor},
            {"cells", std::move(cells)},
            {"editable", std::move(editable)}
        });
    }

    return nlohmann::json(nlohmann::json{
        {"columns", columns},
        {"rows", std::move(rows)},
        {"total", matching.size()},
        {"offset", begin},
        {"presentations", std::move(presentations)}
    }.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordsOfType(
    const nlohmann::json& arguments) const {
    const auto signature = support::stringArgument(arguments, 0);
    const auto filter = support::stringArgument(arguments, 1);
    const auto limit = sizeArgument(arguments, 2, 200, 5000);
    const auto offset = sizeArgument(arguments, 3, 0, 1000000);
    if (signature.empty()) return nlohmann::json("[]");

    struct WinnerRow {
        const LoadedPlugin* plugin{nullptr};
        const fo4::Record* record{nullptr};
    };

    OpLock lock(*this, OpLock::Mode::Read);
    std::map<fo4::FormKey, WinnerRow> winners;
    for (const auto& loaded : plugins_) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            if (record->header.signature == fo4::sig_tes4 || record->form_key.owner().empty()) continue;
            if (record->header.signature.to_string() != signature) continue;
            winners[record->form_key] = WinnerRow{&loaded, record};
        }
    }

    std::vector<WinnerRow> filtered;
    filtered.reserve(winners.size());
    for (const auto& [key, row] : winners) {
        const auto keyText = key.to_string();
        const auto editor = editorId(*row.record);
        if (!containsInsensitive(keyText, filter) && !containsInsensitive(editor, filter)) continue;
        filtered.push_back(row);
    }

    nlohmann::json rows = nlohmann::json::array();
    const auto begin = std::min(offset, filtered.size());
    const auto end = std::min(begin + limit, filtered.size());
    for (std::size_t index = begin; index < end; ++index) {
        const auto& row = filtered[index];
        rows.push_back({
            {"FormKey", row.record->form_key.to_string()},
            {"EditorID", editorId(*row.record)},
            {"Type", row.record->header.signature.to_string()},
            {"Plugin", row.plugin->name}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordDetails(
    const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* winner = (*graph_).winner(key);
    if (winner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }
    const auto* loaded = findPlugin(winner->plugin.filename());
    if (loaded == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "The winning plugin is not loaded for FormKey: " + key.to_string()
        });
    }
    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + loaded->name + ": " + key.to_string()
        });
    }

    const auto& versions = (*graph_).versions(key);

    // Resolve the base form for override records: the earliest definition in
    // load order. All overrides share the FormKey, so the UI can navigate to
    // the base record inside any plugin that contains it.
    std::string baseForm;
    std::string baseFormKey;
    if (versions.size() > 1) {
        const auto& origin = versions.front();
        baseFormKey = key.to_string();
        const auto* originLoaded = findPlugin(origin.plugin.filename());
        if (originLoaded != nullptr) {
            if (const auto* originRecord = fo4::find_record(originLoaded->plugin, key)) {
                auto editor = editorId(*originRecord);
                if (editor.empty()) editor = key.to_string();
                baseForm = std::move(editor) + " (" +
                           std::string(origin.plugin.filename()) + ")";
            }
        }
        if (baseForm.empty()) {
            baseForm = std::string(origin.plugin.filename());
        }
    }

    return nlohmann::json(nlohmann::json{
        {"FormKey", key.to_string()},
        {"FormId", rawFormIdText(record->header.form_id)},
        {"EditorId", editorId(*record)},
        {"Signature", record->header.signature.to_string()},
        {"ClassName", record->header.signature.to_string()},
        {"BaseForm", std::move(baseForm)},
        {"BaseFormKey", std::move(baseFormKey)},
        {"File", loaded->name},
        {"Winner", loaded->name},
        {"OverrideCount", versions.empty() ? 0 : versions.size() - 1}
    }.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordPluginMatrix(
    const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    OpLock lock(*this, OpLock::Mode::Read);
    const auto& versions = (*graph_).versions(key);
    nlohmann::json rows = nlohmann::json::array();
    const fo4::Record* previous = nullptr;

    for (std::size_t index = 0; index < versions.size(); ++index) {
        const auto& version = versions[index];
        const auto* loaded = findPlugin(version.plugin.filename());
        const auto* record = loaded == nullptr ? nullptr : fo4::find_record(loaded->plugin, key);
        const auto changes = index == 0 ? 0 : changedSubrecordCount(previous, record);
        rows.push_back({
            {"Plugin", std::string(version.plugin.filename())},
            {"LoadOrder", version.load_order},
            {"Kind", loaded == nullptr ? "plugin" : pluginKind(loaded->plugin)},
            {"Changes", changes},
            {"Conflicts", changes},
            {"IsOverride", index != 0},
            {"IsWinner", index + 1 == versions.size()},
            {"LastModified", loaded == nullptr ? std::string{} : lastModifiedIso(loaded->path)}
        });
        previous = record;
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getContainmentPath(
    const nlohmann::json& arguments) const {
    auto keyResult = formKeyArgument(arguments);
    if (!keyResult) return std::unexpected(keyResult.error());
    const auto& key = *keyResult;

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* winner = (*graph_).winner(key);
    if (winner == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "No loaded plugin contains this FormKey: " + key.to_string()
        });
    }
    const auto* loaded = findPlugin(winner->plugin.filename());
    if (loaded == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "The winning plugin is not loaded for FormKey: " + key.to_string()
        });
    }
    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) {
        return std::unexpected(Error{
            "RecordNotFound",
            "Record is not present in " + loaded->name + ": " + key.to_string()
        });
    }

    const auto editor = editorId(*record);
    return nlohmann::json(nlohmann::json::array({
        {{"Kind", "Plugin"}, {"Label", loaded->name}, {"FormKey", ""}},
        {{"Kind", "RecordType"}, {"Label", record->header.signature.to_string()}, {"FormKey", ""}},
        {{"Kind", "Record"}, {"Label", editor.empty() ? key.to_string() : editor}, {"FormKey", key.to_string()}}
    }).dump());
}

}
