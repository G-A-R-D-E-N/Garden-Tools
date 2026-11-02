#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fo4/environment/strings.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {

namespace {

const fo4::env::PluginStringTable* findTable(const std::vector<fo4::env::PluginStringTable>& tables,
                                             std::string_view label) {
    const auto it = std::find_if(tables.begin(), tables.end(), [label](const fo4::env::PluginStringTable& table) {
        return fo4::env::string_source_extension(table.source) == label;
    });
    return it == tables.end() ? nullptr : &*it;
}

}

std::shared_ptr<const fo4::archive::AssetResolver> RadiclePluginBackend::stringAssets(
    const LoadedPlugin& loaded, std::vector<SkippedStringArchive>* skipped) const {
    std::vector<std::string> names;
    names.reserve(plugins_.size());
    for (const auto& plugin : plugins_) names.push_back(plugin.name);
    const std::vector<std::string> own{loaded.name};
    const auto dataArchives = fo4::archive::archive_load_order(dataDirectory_, {}, names);
    std::vector<std::filesystem::path> folderArchives;
    if (std::error_code ec; !std::filesystem::equivalent(loaded.path.parent_path(), dataDirectory_, ec)) {
        folderArchives = fo4::archive::archive_load_order(loaded.path.parent_path(), {}, own);
    }
    std::vector<std::filesystem::path> archives;
    for (const auto& path : dataArchives) {
        const auto shadowed = std::any_of(folderArchives.begin(), folderArchives.end(),
                                          [&path](const std::filesystem::path& other) {
                                              return fo4::text::equals_folded(fo4::io::utf8(other.filename()),
                                                                              fo4::io::utf8(path.filename()));
                                          });
        if (!shadowed) archives.push_back(path);
    }
    archives.insert(archives.end(), folderArchives.begin(), folderArchives.end());

    std::vector<StringArchiveStamp> stamps;
    stamps.reserve(archives.size());
    for (const auto& path : archives) {
        std::error_code ec;
        stamps.emplace_back(path, std::filesystem::file_size(path, ec), std::filesystem::last_write_time(path, ec));
    }
    std::scoped_lock lock(stringAssetsMutex_);
    if (!stringAssets_ || stamps != stringArchives_) {
        std::vector<std::filesystem::path> readable;
        std::vector<SkippedStringArchive> unreadable;
        for (const auto& path : archives) {
            try {
                fo4::archive::Ba2Reader::open(path);
                readable.push_back(path);
            } catch (const std::exception& error) {
                unreadable.push_back({fo4::io::utf8(path), error.what()});
            }
        }
        stringAssets_ = std::make_shared<fo4::archive::AssetResolver>(
            fo4::archive::AssetResolver::open(dataDirectory_, readable));
        stringSkipped_ = std::move(unreadable);
        stringArchives_ = std::move(stamps);
    }
    if (skipped != nullptr) *skipped = stringSkipped_;
    return stringAssets_;
}

Result<std::vector<fo4::env::PluginStringTable>> RadiclePluginBackend::readStringTables(
    const LoadedPlugin& loaded, std::string_view language,
    std::vector<SkippedStringArchive>* skipped) const {
    try {
        std::vector<std::filesystem::path> directories{loaded.path.parent_path() / "Strings"};
        if (!dataDirectory_.empty()) directories.push_back(dataDirectory_ / "Strings");
        auto tables = fo4::env::load_plugin_string_tables(directories, loaded.plugin, language);
        if (tables.size() != 3 && loaded.plugin.header.tes4_flags.is_localized()) {
            const auto assets = stringAssets(loaded, skipped);
            tables = fo4::env::load_plugin_string_tables(directories, loaded.plugin, language, assets.get());
        }
        overlayStagedStringTables(loaded, language, tables);
        return tables;
    } catch (const std::exception& error) {
        return std::unexpected(Error{
            "StringTableInvalid",
            std::string("could not read string tables: ") + error.what()
        });
    }
}

Result<nlohmann::json> RadiclePluginBackend::listStringTables(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = support::stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    std::vector<SkippedStringArchive> skipped;
    auto tables = readStringTables(*loaded, *language, &skipped);
    if (!tables) return std::unexpected(tables.error());

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& found : *tables) {
        nlohmann::json row{
            {"kind", std::string(fo4::env::string_source_extension(found.source))},
            {"path", found.path.string()},
            {"count", found.table.entries.size()},
            {"source", found.archive.empty() ? std::string("loose") : found.archive}
        };
        if (stringTablePending(loaded->name, *language, found.source)) row["pending"] = true;
        rows.push_back(std::move(row));
    }
    nlohmann::json skippedRows = nlohmann::json::array();
    for (const auto& archive : skipped) {
        skippedRows.push_back({{"path", archive.path}, {"reason", archive.reason}});
    }
    const nlohmann::json payload{
        {"tables", rows},
        {"skippedArchives", skippedRows}
    };
    return nlohmann::json(payload.dump());
}

Result<nlohmann::json> RadiclePluginBackend::getStrings(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = support::stringLanguage(arguments);
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
    auto tables = readStringTables(*loaded, *language);
    if (!tables) return std::unexpected(tables.error());
    const auto* found = findTable(*tables, tableLabel);
    if (found == nullptr) {
        return std::unexpected(Error{
            "StringTableUnavailable",
            "No " + tableLabel + " table for " + pluginName + " in language " + *language
        });
    }

    const auto& entries = found->table.entries;
    const auto needle = support::asciiLower(filter);
    nlohmann::json rows = nlohmann::json::array();
    std::size_t total = 0;
    for (const auto& entry : entries) {
        const auto text = support::displayText(*language, entry.text);
        if (!needle.empty() && support::asciiLower(text).find(needle) == std::string::npos &&
            std::to_string(entry.id).find(filter) == std::string::npos) {
            continue;
        }
        if (total >= offset && rows.size() < limit) {
            rows.push_back({
                {"id", entry.id},
                {"text", text}
            });
        }
        ++total;
    }
    nlohmann::json payload{
        {"total", total},
        {"offset", offset},
        {"limit", limit},
        {"rows", rows}
    };
    if (stringTablePending(loaded->name, *language, found->source)) payload["pending"] = true;
    return nlohmann::json(payload.dump());
}

Result<nlohmann::json> RadiclePluginBackend::lookupString(const nlohmann::json& arguments) const {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = support::stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());
    const auto id = support::integerArgument<std::uint32_t>(arguments, 2);
    if (!id) return std::unexpected(Error{"InvalidStringId", "String ID must be a 32-bit integer"});

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    auto tables = readStringTables(*loaded, *language);
    if (!tables) return std::unexpected(tables.error());

    nlohmann::json rows = nlohmann::json::array();
    for (const auto& found : *tables) {
        const auto* entry = fo4::localization::find_string(found.table, *id);
        if (entry == nullptr) continue;
        nlohmann::json row{
            {"kind", std::string(fo4::env::string_source_extension(found.source))},
            {"id", *id},
            {"text", support::displayText(*language, entry->text)}
        };
        if (stringTablePending(loaded->name, *language, found.source)) row["pending"] = true;
        rows.push_back(std::move(row));
    }
    return nlohmann::json(rows.dump());
}

}
