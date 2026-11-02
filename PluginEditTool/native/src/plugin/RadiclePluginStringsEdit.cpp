#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fo4/environment/strings.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {

namespace {

using StringSource = fo4::env::StringSource;
using StringTable = fo4::localization::LocalizedStringTable;
using StringTableKind = fo4::localization::StringTableKind;

struct StringTarget {
    std::string plugin;
    std::string language;
    StringSource source;
};

struct ImportRow {
    std::uint32_t id;
    std::string text;
    std::string encoded;
};

Result<StringSource> stringSourceArgument(std::string_view label) {
    for (const auto source : {StringSource::normal, StringSource::dl, StringSource::il}) {
        if (fo4::env::string_source_extension(source) == label) return source;
    }
    return std::unexpected(Error{
        "StringTableUnavailable",
        "Unknown string table: " + std::string(label)
    });
}

StringTableKind tableKind(StringSource source) {
    switch (source) {
        case StringSource::dl:
            return StringTableKind::dlstrings;
        case StringSource::il:
            return StringTableKind::ilstrings;
        case StringSource::normal:
            break;
    }
    return StringTableKind::strings;
}

Result<StringTarget> stringTargetArguments(const nlohmann::json& arguments) {
    auto plugin = support::stringArgument(arguments, 0);
    if (plugin.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    auto language = support::stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());
    const auto source = stringSourceArgument(support::stringArgument(arguments, 2));
    if (!source) return std::unexpected(source.error());
    return StringTarget{std::move(plugin), std::move(*language), *source};
}

Result<std::string> encodeStringText(std::string_view language, std::string_view text) {
    if (text.find('\0') != std::string_view::npos) {
        return std::unexpected(Error{"StringNotEncodable", "String text must not contain a NUL byte"});
    }
    if (language != "en") {
        if (!fo4::text::is_valid_utf8(text)) {
            return std::unexpected(Error{"StringNotEncodable", "String text is not valid UTF-8"});
        }
        return std::string(text);
    }
    auto encoded = fo4::text::utf8_to_windows1252(text);
    if (!encoded) {
        return std::unexpected(Error{
            "StringNotEncodable",
            "English string text has a character outside Windows-1252"
        });
    }
    return std::move(*encoded);
}

StringTable effectiveTable(const std::vector<fo4::env::PluginStringTable>& tables, StringSource source) {
    for (const auto& found : tables) {
        if (found.source == source) return found.table;
    }
    return StringTable{tableKind(source), {}};
}

Error missingStringId(const StringTarget& target, std::uint32_t id) {
    return Error{
        "StringIdMissing",
        "No string ID " + std::to_string(id) + " in " + target.plugin + " " + target.language + " " +
            std::string(fo4::env::string_source_extension(target.source))
    };
}

Error pendingLocalizationError() {
    return Error{
        "StringEditBlockedByPendingLocalization",
        "Save or discard the pending localized quest edit before editing or saving its string tables"
    };
}

Result<std::vector<ImportRow>> parseImportRows(const std::string& json_text, std::string_view language) {
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(json_text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ImportInvalid", std::string("import is not valid JSON: ") + error.what()});
    }
    if (!document.is_object() || !document.contains("rows") || !document["rows"].is_array()) {
        return std::unexpected(Error{"ImportInvalid", "import must be an object with a rows array"});
    }
    std::vector<ImportRow> rows;
    std::set<std::uint32_t> seen;
    for (const auto& row : document["rows"]) {
        const auto id = support::integerField<std::uint32_t>(row, "id");
        if (!id) return std::unexpected(Error{"ImportInvalid", "each import row needs a 32-bit integer id"});
        if (!row.contains("text") || !row["text"].is_string()) {
            return std::unexpected(Error{"ImportInvalid", "each import row needs a text string"});
        }
        if (!seen.insert(*id).second) {
            return std::unexpected(Error{
                "ImportDuplicateId",
                "import lists string ID " + std::to_string(*id) + " more than once"
            });
        }
        auto text = row["text"].get<std::string>();
        auto encoded = encodeStringText(language, text);
        if (!encoded) return std::unexpected(encoded.error());
        rows.push_back(ImportRow{*id, std::move(text), std::move(*encoded)});
    }
    return rows;
}

}

Result<nlohmann::json> RadiclePluginBackend::setString(const nlohmann::json& arguments) {
    const auto target = stringTargetArguments(arguments);
    if (!target) return std::unexpected(target.error());
    const auto id = support::integerArgument<std::uint32_t>(arguments, 3);
    if (!id) return std::unexpected(Error{"InvalidStringId", "String ID must be a 32-bit integer"});
    const auto utf8 = support::argumentAt(arguments, 4);
    if (!utf8.is_string()) return std::unexpected(Error{"StringTextMissing", "String text must be a string"});
    const auto text = encodeStringText(target->language, utf8.get<std::string>());
    if (!text) return std::unexpected(text.error());

    OpLock lock(*this, OpLock::Mode::Write, {target->plugin});
    const auto* loaded = findPlugin(target->plugin);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + target->plugin});
    }
    if (loaded->stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
    auto tables = readStringTables(*loaded, target->language);
    if (!tables) return std::unexpected(tables.error());
    auto table = effectiveTable(*tables, target->source);
    if (!fo4::localization::set_string(table, *id, *text)) return std::unexpected(missingStringId(*target, *id));
    stageStringTable(loaded->name, target->language, target->source, std::move(table));
    return support::dumpPayload({{"id", *id}, {"pending", true}});
}

Result<nlohmann::json> RadiclePluginBackend::addString(const nlohmann::json& arguments) {
    const auto target = stringTargetArguments(arguments);
    if (!target) return std::unexpected(target.error());
    const auto utf8 = support::argumentAt(arguments, 3);
    if (!utf8.is_string()) return std::unexpected(Error{"StringTextMissing", "String text must be a string"});
    const auto text = encodeStringText(target->language, utf8.get<std::string>());
    if (!text) return std::unexpected(text.error());
    if (text->empty()) return std::unexpected(Error{"StringTextEmpty", "A new string must have text"});

    OpLock lock(*this, OpLock::Mode::Write, {target->plugin});
    const auto* loaded = findPlugin(target->plugin);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + target->plugin});
    }
    if (loaded->stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
    auto tables = readStringTables(*loaded, target->language);
    if (!tables) return std::unexpected(tables.error());
    auto table = effectiveTable(*tables, target->source);
    std::uint32_t id = 0;
    try {
        id = fo4::localization::add_string(table, *text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"StringEditFailed", error.what()});
    }
    stageStringTable(loaded->name, target->language, target->source, std::move(table));
    return support::dumpPayload({{"id", id}, {"pending", true}});
}

Result<nlohmann::json> RadiclePluginBackend::removeString(const nlohmann::json& arguments) {
    const auto target = stringTargetArguments(arguments);
    if (!target) return std::unexpected(target.error());
    const auto id = support::integerArgument<std::uint32_t>(arguments, 3);
    if (!id) return std::unexpected(Error{"InvalidStringId", "String ID must be a 32-bit integer"});

    OpLock lock(*this, OpLock::Mode::Write, {target->plugin});
    const auto* loaded = findPlugin(target->plugin);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + target->plugin});
    }
    if (loaded->stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
    auto tables = readStringTables(*loaded, target->language);
    if (!tables) return std::unexpected(tables.error());
    auto table = effectiveTable(*tables, target->source);
    if (!fo4::localization::remove_string(table, *id)) return std::unexpected(missingStringId(*target, *id));
    stageStringTable(loaded->name, target->language, target->source, std::move(table));
    return support::dumpPayload({{"id", *id}, {"pending", true}});
}

Result<nlohmann::json> RadiclePluginBackend::importStrings(const nlohmann::json& arguments) {
    const auto target = stringTargetArguments(arguments);
    if (!target) return std::unexpected(target.error());
    const auto mode = support::stringArgument(arguments, 4);
    if (mode != "merge" && mode != "replace") {
        return std::unexpected(Error{"ImportModeInvalid", "Import mode must be merge or replace"});
    }
    const auto rows = parseImportRows(support::stringArgument(arguments, 3), target->language);
    if (!rows) return std::unexpected(rows.error());

    OpLock lock(*this, OpLock::Mode::Write, {target->plugin});
    const auto* loaded = findPlugin(target->plugin);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + target->plugin});
    }
    if (loaded->stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
    auto tables = readStringTables(*loaded, target->language);
    if (!tables) return std::unexpected(tables.error());
    auto table = effectiveTable(*tables, target->source);
    if (mode == "replace") {
        std::set<std::uint32_t> listed;
        for (const auto& row : *rows) listed.insert(row.id);
        std::vector<std::uint32_t> unlisted;
        for (const auto& entry : table.entries) {
            if (!listed.contains(entry.id)) unlisted.push_back(entry.id);
        }
        for (const auto id : unlisted) fo4::localization::remove_string(table, id);
    }
    for (const auto& row : *rows) {
        const auto* existing = fo4::localization::find_string(table, row.id);
        if (existing == nullptr) {
            fo4::localization::insert_string(table, row.id, row.encoded);
        } else if (support::displayText(target->language, existing->text) != row.text) {
            fo4::localization::set_string(table, row.id, row.encoded);
        }
    }
    stageStringTable(loaded->name, target->language, target->source, std::move(table));
    return support::dumpPayload({{"rows", rows->size()}, {"mode", mode}, {"pending", true}});
}

Result<nlohmann::json> RadiclePluginBackend::exportStrings(const nlohmann::json& arguments) const {
    const auto target = stringTargetArguments(arguments);
    if (!target) return std::unexpected(target.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(target->plugin);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + target->plugin});
    }
    auto tables = readStringTables(*loaded, target->language);
    if (!tables) return std::unexpected(tables.error());
    auto entries = effectiveTable(*tables, target->source).entries;
    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.id < rhs.id;
    });
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& entry : entries) {
        rows.push_back({
            {"id", entry.id},
            {"text", support::displayText(target->language, entry.text)}
        });
    }
    return support::dumpPayload({
        {"kind", std::string(fo4::env::string_source_extension(target->source))},
        {"language", target->language},
        {"rows", rows}
    });
}

Result<nlohmann::json> RadiclePluginBackend::saveStringTables(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = support::stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "StringSaveProtected",
            "Refusing to write string tables for protected or game-Data plugin: " + pluginName
        });
    }
    if (loaded->stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
    std::vector<std::pair<StringSource, StringTable>> pending;
    {
        std::scoped_lock staged(stagedStringsMutex_);
        for (const auto& [key, table] : stagedStrings_) {
            const auto& [name, lang, source] = key;
            if (name == loaded->name && lang == *language) pending.emplace_back(source, table);
        }
    }
    std::vector<std::string> written;
    for (const auto& [source, table] : pending) {
        const auto path = loaded->path.parent_path() /
                          fo4::localization::string_table_path(loaded->name, tableKind(source), *language);
        try {
            std::filesystem::create_directories(path.parent_path());
            fo4::localization::write_string_table_file(table, path);
        } catch (const std::exception& error) {
            return std::unexpected(Error{
                "StringSaveFailed",
                "could not write " + fo4::io::utf8(path) + ": " + error.what()
            });
        }
        written.push_back(fo4::io::utf8(path));
    }
    {
        std::scoped_lock staged(stagedStringsMutex_);
        std::erase_if(stagedStrings_, [&loaded, &language](const auto& entry) {
            return std::get<0>(entry.first) == loaded->name && std::get<1>(entry.first) == *language;
        });
    }
    return support::dumpPayload({{"saved", written}});
}

Result<nlohmann::json> RadiclePluginBackend::discardStringEdits(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = support::stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    const auto* loaded = findPlugin(pluginName);
    const auto name = loaded != nullptr ? loaded->name : pluginName;
    std::scoped_lock staged(stagedStringsMutex_);
    const auto discarded = std::erase_if(stagedStrings_, [&name, &language](const auto& entry) {
        return std::get<0>(entry.first) == name && std::get<1>(entry.first) == *language;
    });
    return support::dumpPayload({{"discarded", discarded}});
}

void RadiclePluginBackend::discardStagedStringTables(std::string_view plugin) {
    std::scoped_lock staged(stagedStringsMutex_);
    std::erase_if(stagedStrings_, [plugin](const auto& entry) {
        return std::get<0>(entry.first) == plugin;
    });
}

bool RadiclePluginBackend::hasStagedStringTables(std::string_view plugin) const {
    std::scoped_lock staged(stagedStringsMutex_);
    return std::any_of(stagedStrings_.begin(), stagedStrings_.end(), [plugin](const auto& entry) {
        return std::get<0>(entry.first) == plugin;
    });
}

Result<LocalizedStringAllocation> RadiclePluginBackend::openLocalizedStrings(
    const LoadedPlugin& loaded, std::string_view language) const {
    if (loaded.stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
    auto tables = readStringTables(loaded, language);
    if (!tables) return std::unexpected(tables.error());
    LocalizedStringAllocation allocation;
    allocation.language = std::string(language);
    std::uint32_t maximum = 0;
    for (const auto source : {StringSource::normal, StringSource::dl, StringSource::il}) {
        auto table = effectiveTable(*tables, source);
        for (const auto& entry : table.entries) maximum = std::max(maximum, entry.id);
        allocation.tables.emplace(source, std::move(table));
    }
    if (maximum == std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error{"LocalizedStringTableFull", "localized string-table ID space is exhausted"});
    }
    allocation.nextId = maximum + 1;
    return allocation;
}

Result<std::uint32_t> RadiclePluginBackend::allocateLocalizedString(
    LocalizedStringAllocation& allocation, StringSource source, std::string_view text) const {
    const auto encoded = encodeStringText(allocation.language, text);
    if (!encoded) return std::unexpected(encoded.error());
    if (encoded->empty()) return std::unexpected(Error{"StringTextEmpty", "A new string must have text"});
    const auto table = allocation.tables.find(source);
    if (table == allocation.tables.end() || allocation.nextId == std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error{"LocalizedStringTableFull", "localized string-table ID space is exhausted"});
    }
    const auto id = allocation.nextId++;
    try {
        fo4::localization::insert_string(table->second, id, *encoded);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"StringEditFailed", error.what()});
    }
    allocation.touched.insert(source);
    return id;
}

void RadiclePluginBackend::stageLocalizedStrings(std::string_view plugin,
                                                 const LocalizedStringAllocation& allocation) {
    for (const auto source : allocation.touched) {
        stageStringTable(plugin, allocation.language, source, allocation.tables.at(source));
    }
}

void RadiclePluginBackend::stageStringTable(std::string_view plugin,
                                            std::string_view language,
                                            StringSource source,
                                            StringTable table) {
    CommitScope commit;
    std::scoped_lock staged(stagedStringsMutex_);
    stagedStrings_.insert_or_assign(StagedStringKey{std::string(plugin), std::string(language), source},
                                    std::move(table));
    ++mutationEpoch_;
}

bool RadiclePluginBackend::stringTablePending(std::string_view plugin,
                                              std::string_view language,
                                              StringSource source) const {
    std::scoped_lock staged(stagedStringsMutex_);
    return stagedStrings_.contains(StagedStringKey{std::string(plugin), std::string(language), source});
}

void RadiclePluginBackend::overlayStagedStringTables(const LoadedPlugin& loaded,
                                                     std::string_view language,
                                                     std::vector<fo4::env::PluginStringTable>& tables) const {
    std::scoped_lock staged(stagedStringsMutex_);
    for (const auto& [key, table] : stagedStrings_) {
        const auto& [name, lang, source] = key;
        if (name != loaded.name || lang != language) continue;
        const auto wanted = source;
        const auto found = std::find_if(tables.begin(), tables.end(),
                                        [wanted](const fo4::env::PluginStringTable& candidate) {
                                            return candidate.source == wanted;
                                        });
        if (found != tables.end()) {
            found->table = table;
        } else {
            tables.push_back({
                wanted,
                loaded.path.parent_path() / fo4::localization::string_table_path(
                                                loaded.name, tableKind(wanted), language),
                {},
                table
            });
        }
    }
    std::sort(tables.begin(), tables.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.source < rhs.source;
    });
}

}
