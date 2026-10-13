#include "RadiclePluginQuestInternal.hpp"
#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/localization/strings.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {

namespace {

using fo4::localization::LocalizedStringTable;
using fo4::localization::StringTableKind;

struct TableSpec {
    StringTableKind kind;
    const char* label;
    const char* extension;
};

constexpr std::array<TableSpec, 3> k_tables{{
    {StringTableKind::strings, "STRINGS", ".STRINGS"},
    {StringTableKind::dlstrings, "DLSTRINGS", ".DLSTRINGS"},
    {StringTableKind::ilstrings, "ILSTRINGS", ".ILSTRINGS"},
}};

struct FoundTable {
    const TableSpec* spec;
    std::filesystem::path path;
    LocalizedStringTable table;
};

Result<std::string> stringLanguage(const nlohmann::json& arguments) {
    auto language = support::stringArgument(arguments, 1);
    if (language.empty()) return std::string("en");
    const bool valid = std::all_of(language.begin(), language.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0;
    });
    if (!valid) {
        return std::unexpected(Error{
            "InvalidStringLanguage",
            "string table language must be an alphanumeric token"
        });
    }
    return language;
}

// Same sibling-table lookup as questinternal::loadLocalizedQuestTables: Strings/<stem>_<language>.<ext>.
Result<std::vector<FoundTable>> readTables(const std::filesystem::path& pluginPath,
                                           std::string_view language) {
    const auto stem = pluginPath.stem().string() + "_" + std::string(language);
    const auto directory = pluginPath.parent_path() / "Strings";
    std::vector<FoundTable> found;
    for (const auto& spec : k_tables) {
        const auto path = directory / (stem + spec.extension);
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            continue;
        }
        try {
            const auto bytes = questinternal::readLocalizedFileBytes(path);
            found.push_back({&spec, path, fo4::localization::read_string_table(bytes, spec.kind)});
        } catch (const std::exception& error) {
            return std::unexpected(Error{
                "LocalizedLogTableInvalid",
                "could not read string table " + path.string() + ": " + error.what()
            });
        }
    }
    return found;
}

const FoundTable* findTable(const std::vector<FoundTable>& tables, std::string_view label) {
    const auto it = std::find_if(tables.begin(), tables.end(), [label](const FoundTable& table) {
        return table.spec->label == label;
    });
    return it == tables.end() ? nullptr : &*it;
}

// UTF-8 text is used as stored for non-English tables; everything else is CP1252.
std::string displayText(std::string_view language, const std::vector<std::byte>& bytes) {
    const std::string raw(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (language != "en" && fo4::text::is_valid_utf8(raw)) return raw;
    return fo4::text::windows1252_to_utf8(raw);
}

}

Result<nlohmann::json> RadiclePluginBackend::listStringTables(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    auto tables = readTables(loaded->path, *language);
    if (!tables) return std::unexpected(tables.error());

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& found : *tables) {
        rows.push_back({
            {"kind", found.spec->label},
            {"path", found.path.string()},
            {"count", found.table.entries.size()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getStrings(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());
    const auto tableLabel = support::stringArgument(arguments, 2);
    const auto filter = support::stringArgument(arguments, 3);
    const auto offset = static_cast<std::size_t>(std::clamp<std::int64_t>(
        support::integerArgument<std::int64_t>(arguments, 4).value_or(0), 0, 1000000));
    const auto limit = static_cast<std::size_t>(std::clamp<std::int64_t>(
        support::integerArgument<std::int64_t>(arguments, 5).value_or(200), 1, 1000));

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    auto tables = readTables(loaded->path, *language);
    if (!tables) return std::unexpected(tables.error());
    const auto* found = findTable(*tables, tableLabel);
    if (found == nullptr) {
        return std::unexpected(Error{
            "StringTableUnavailable",
            "No " + tableLabel + " table for " + pluginName + " in language " + *language
        });
    }

    const auto& entries = found->table.entries;
    std::map<std::uint32_t, std::size_t> idCounts;
    for (const auto& entry : entries) ++idCounts[entry.id];

    const auto needle = support::asciiLower(filter);
    nlohmann::json rows = nlohmann::json::array();
    std::size_t total = 0;
    for (const auto& entry : entries) {
        const auto text = displayText(*language, entry.text);
        if (!needle.empty() && support::asciiLower(text).find(needle) == std::string::npos &&
            std::to_string(entry.id).find(filter) == std::string::npos) {
            continue;
        }
        if (total >= offset && rows.size() < limit) {
            rows.push_back({
                {"id", entry.id},
                {"text", text},
                {"duplicate", idCounts[entry.id] > 1}
            });
        }
        ++total;
    }
    const nlohmann::json payload{
        {"total", total},
        {"offset", offset},
        {"limit", limit},
        {"rows", rows}
    };
    return nlohmann::json(payload.dump());
}

Result<nlohmann::json> RadiclePluginBackend::lookupString(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());
    const auto id = support::integerArgument<std::uint32_t>(arguments, 2);
    if (!id) return std::unexpected(Error{"InvalidStringId", "String ID must be a 32-bit integer"});

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    auto tables = readTables(loaded->path, *language);
    if (!tables) return std::unexpected(tables.error());

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& found : *tables) {
        const auto* entry = fo4::localization::find_string(found.table, *id);
        if (entry == nullptr) continue;
        rows.push_back({
            {"kind", found.spec->label},
            {"id", *id},
            {"text", displayText(*language, entry->text)}
        });
    }
    return nlohmann::json(rows.dump());
}

}
