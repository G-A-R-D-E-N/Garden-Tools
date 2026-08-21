#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
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
#include <fo4/schema/scalar.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

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

std::string editorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
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
    if (plugin.header.tes4_flags.is_small_file()) return "light";
    if (plugin.header.tes4_flags.is_master()) return "master";
    return "plugin";
}

std::string rawFormIdText(fo4::RawFormId formId) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
        << formId.value();
    return out.str();
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
    const auto text = stringArgument(arguments, 0);
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
    if (signature == "REFR") {
        return nlohmann::json::array({
            "EDID",
            "REFR\\HealthPercent"
        });
    }
    if (signature == "EYES") {
        return nlohmann::json::array({
            "EDID",
            "EYES\\Full",
            "EYES\\Icon",
            "EYES\\DataFlags"
        });
    }
    if (signature != "LVSP") return nlohmann::json::array({"EDID"});

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

}

Result<nlohmann::json> RadiclePluginBackend::getPluginRecordTypes(
    const nlohmann::json& arguments) const {
    const auto pluginName = stringArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    std::scoped_lock lock(mutex_);
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
    const auto pluginName = stringArgument(arguments, 0);
    const auto signature = stringArgument(arguments, 1);
    const auto limit = sizeArgument(arguments, 2, 200, 1000);
    const auto offset = sizeArgument(arguments, 3, 0, 1000000);

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr || signature.empty()) {
        return nlohmann::json(nlohmann::json{
            {"columns", gridColumns(signature)},
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

    const auto columns = gridColumns(signature, maxEntries);
    nlohmann::json rows = nlohmann::json::array();
    for (std::size_t index = begin; index < end; ++index) {
        const auto* record = matching[index];
        const auto editor = editorId(*record);
        nlohmann::json cells = nlohmann::json::array({editor});
        nlohmann::json editable = nlohmann::json::array({true});

        if (signature == "REFR") {
            const auto xhltSignature = fo4::Signature::from_string("XHLT");
            const fo4::Subrecord* xhlt = nullptr;
            for (const auto& subrecord : record->subrecords) {
                if (subrecord.signature != xhltSignature) continue;
                if (xhlt != nullptr) {
                    return std::unexpected(Error{
                        "ScalarReadFailed",
                        record->form_key.to_string() + ": duplicate REFR XHLT subrecords"
                    });
                }
                xhlt = &subrecord;
            }

            if (xhlt == nullptr) {
                cells.push_back("");
                editable.push_back(false);
            } else {
                try {
                    const auto descriptor = fo4::schema::scalar_descriptor(xhltSignature);
                    if (!descriptor.has_value()) {
                        return std::unexpected(Error{
                            "ScalarReadFailed",
                            "REFR XHLT is missing its Radicle scalar descriptor"
                        });
                    }
                    cells.push_back(fo4::schema::decode_scalar(xhlt->data, *descriptor));
                    editable.push_back(true);
                } catch (const fo4::schema::ScalarError& error) {
                    return std::unexpected(Error{
                        "ScalarReadFailed",
                        record->form_key.to_string() + ": " + error.what()
                    });
                }
            }
        } else if (signature == "EYES") {
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
        }

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
        {"offset", begin}
    }.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getRecordsOfType(
    const nlohmann::json& arguments) const {
    const auto signature = stringArgument(arguments, 0);
    const auto filter = stringArgument(arguments, 1);
    const auto limit = sizeArgument(arguments, 2, 200, 5000);
    const auto offset = sizeArgument(arguments, 3, 0, 1000000);
    if (signature.empty()) return nlohmann::json("[]");

    struct WinnerRow {
        const LoadedPlugin* plugin{nullptr};
        const fo4::Record* record{nullptr};
    };

    std::scoped_lock lock(mutex_);
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

    std::scoped_lock lock(mutex_);
    const auto* winner = graph_.winner(key);
    if (winner == nullptr) return nlohmann::json("{}");
    const auto* loaded = findPlugin(winner->plugin.filename());
    if (loaded == nullptr) return nlohmann::json("{}");
    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) return nlohmann::json("{}");

    const auto& versions = graph_.versions(key);
    return nlohmann::json(nlohmann::json{
        {"FormKey", key.to_string()},
        {"FormId", rawFormIdText(record->header.form_id)},
        {"EditorId", editorId(*record)},
        {"Signature", record->header.signature.to_string()},
        {"ClassName", record->header.signature.to_string()},
        {"BaseForm", ""},
        {"BaseFormKey", ""},
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

    std::scoped_lock lock(mutex_);
    const auto& versions = graph_.versions(key);
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
            {"LastModified", ""}
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

    std::scoped_lock lock(mutex_);
    const auto* winner = graph_.winner(key);
    if (winner == nullptr) return nlohmann::json("[]");
    const auto* loaded = findPlugin(winner->plugin.filename());
    if (loaded == nullptr) return nlohmann::json("[]");
    const auto* record = fo4::find_record(loaded->plugin, key);
    if (record == nullptr) return nlohmann::json("[]");

    const auto editor = editorId(*record);
    return nlohmann::json(nlohmann::json::array({
        {{"Kind", "Plugin"}, {"Label", loaded->name}, {"FormKey", ""}},
        {{"Kind", "RecordType"}, {"Label", record->header.signature.to_string()}, {"FormKey", ""}},
        {{"Kind", "Record"}, {"Label", editor.empty() ? key.to_string() : editor}, {"FormKey", key.to_string()}}
    }).dump());
}

}