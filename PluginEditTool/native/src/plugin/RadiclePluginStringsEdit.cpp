#include "RadiclePluginSupport.hpp"
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
#include "RadiclePluginHeldLocalization.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include <fo4/environment/strings.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4/text/encoding.hpp>

namespace fo4ide::plugin {

namespace {

using StringSource = fo4::env::StringSource;
using StringTable = fo4::localization::LocalizedStringTable;
using StringTableKind = fo4::localization::StringTableKind;
#ifdef FO4IDE_TEST_HOOKS
LocalizedStringBatchAfterInsertHook localizedStringBatchAfterInsertHook = nullptr;
LocalizedAllocationAfterReadHook localizedAllocationAfterReadHook = nullptr;
LocalizedStringAfterReadHook localizedStringAfterReadHook = nullptr;
#endif

struct ExistingFile {
    bool exists{false};
    std::vector<std::byte> bytes;
};

Result<ExistingFile> readExistingFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && !std::filesystem::exists(status))) {
        return ExistingFile{};
    }
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return std::unexpected(Error{
            "StringTableDestinationUnsafe",
            "string table destination must be an existing regular non-symlink file: " + path.string()
        });
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return std::unexpected(Error{"StringTableUnavailable", "could not open string table: " + path.string()});
    }
    const auto end = stream.tellg();
    if (end < 0) {
        return std::unexpected(Error{"StringTableUnavailable", "could not determine string table size: " + path.string()});
    }
    ExistingFile result;
    result.exists = true;
    result.bytes.resize(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!result.bytes.empty()) {
        stream.read(reinterpret_cast<char*>(result.bytes.data()),
                    static_cast<std::streamsize>(result.bytes.size()));
        if (!stream) {
            return std::unexpected(Error{"StringTableUnavailable", "could not read string table: " + path.string()});
        }
    }
    return result;
}

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

#ifdef FO4IDE_TEST_HOOKS
void setLocalizedStringBatchAfterInsertHook(LocalizedStringBatchAfterInsertHook hook) {
    localizedStringBatchAfterInsertHook = hook;
}
void setLocalizedAllocationAfterReadHook(LocalizedAllocationAfterReadHook hook) { localizedAllocationAfterReadHook = hook; }
void setLocalizedStringAfterReadHook(LocalizedStringAfterReadHook hook) { localizedStringAfterReadHook = hook; }
#endif

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
    std::vector<fo4::env::PluginStringTableSnapshot> snapshots;
    auto tables = readStringTables(*loaded, target->language, nullptr, &snapshots);
    if (!tables) return std::unexpected(tables.error());
#ifdef FO4IDE_TEST_HOOKS
    if (localizedStringAfterReadHook != nullptr) localizedStringAfterReadHook();
#endif
    auto table = effectiveTable(*tables, target->source);
    if (!fo4::localization::set_string(table, *id, *text)) return std::unexpected(missingStringId(*target, *id));
    if (const auto staged = stageStringTable(loaded->name, target->language, target->source, std::move(table), {}, nullptr, &snapshots); !staged) {
        return std::unexpected(staged.error());
    }
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
    std::vector<fo4::env::PluginStringTableSnapshot> snapshots;
    auto tables = readStringTables(*loaded, target->language, nullptr, &snapshots);
    if (!tables) return std::unexpected(tables.error());
    auto table = effectiveTable(*tables, target->source);
    LocalizedStringAllocation allocation; allocation.language = target->language; allocation.sourceSnapshots = snapshots;
    const auto next = nextLocalizedStringId(*loaded, &allocation);
    if (!next) return std::unexpected(next.error());
#ifdef FO4IDE_TEST_HOOKS
    if (localizedStringAfterReadHook != nullptr) localizedStringAfterReadHook();
#endif
    try {
        fo4::localization::insert_string(table, *next, *text);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"StringEditFailed", error.what()});
    }
    allocation.tables.emplace(target->source, std::move(table)); allocation.touched.insert(target->source);
    if (const auto staged = stageAllocatedStringTable(loaded->name, std::move(allocation), target->source); !staged) {
        return std::unexpected(staged.error());
    }
    return support::dumpPayload({{"id", *next}, {"pending", true}});
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
    std::vector<fo4::env::PluginStringTableSnapshot> snapshots;
    auto tables = readStringTables(*loaded, target->language, nullptr, &snapshots);
    if (!tables) return std::unexpected(tables.error());
#ifdef FO4IDE_TEST_HOOKS
    if (localizedStringAfterReadHook != nullptr) localizedStringAfterReadHook();
#endif
    auto table = effectiveTable(*tables, target->source);
    if (!fo4::localization::remove_string(table, *id)) return std::unexpected(missingStringId(*target, *id));
    if (auto valid = validateStringIdsRetained(*loaded, target->language, target->source, table); !valid) return std::unexpected(valid.error());
    if (const auto staged = stageStringTable(loaded->name, target->language, target->source, std::move(table), {}, nullptr, &snapshots); !staged) {
        return std::unexpected(staged.error());
    }
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
    std::vector<fo4::env::PluginStringTableSnapshot> snapshots;
    auto tables = readStringTables(*loaded, target->language, nullptr, &snapshots);
    if (!tables) return std::unexpected(tables.error());
#ifdef FO4IDE_TEST_HOOKS
    if (localizedStringAfterReadHook != nullptr) localizedStringAfterReadHook();
#endif
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
    if (auto valid = validateStringIdsRetained(*loaded, target->language, target->source, table); !valid) return std::unexpected(valid.error());
    if (const auto staged = stageStringTable(loaded->name, target->language, target->source, std::move(table), {}, nullptr, &snapshots); !staged) {
        return std::unexpected(staged.error());
    }
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
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (!isEditable(*loaded)) {
        return std::unexpected(Error{
            "StringSaveProtected",
            "Refusing to write string tables for protected or game-Data plugin: " + pluginName
        });
    }
    if (loaded->stagedPluginLanguages.contains(*language)) return std::unexpected(pendingLocalizationError());
    std::vector<StagedStringTable> pending;
    {
        std::scoped_lock staged(stagedStringsMutex_);
        for (const auto& [key, table] : stagedStrings_) {
            const auto& [name, lang, source, path] = key;
            static_cast<void>(source);
            static_cast<void>(path);
            if (name == loaded->name && lang == *language) pending.push_back(table);
        }
    }
    if (!pending.empty()) {
        if (!testJournalRoot_.empty()) {
            std::vector<std::string> paths;
            for (const auto& table : pending) paths.push_back(fo4::io::utf8(table.file.path));
            auto response = support::dumpPayload({{"saved", paths}});
            auto saved = persistJournalLocked(*loaded, loaded->path, *language);
            if (!saved) return std::unexpected(saved.error());
            return response;
        }
        return std::unexpected(Error{"LocalizedSaveUnavailable", "Pending string tables require recoverable publication."});
    }
    std::vector<std::string> written;
    for (const auto& table : pending) {
        const auto& path = table.file.path;
        try {
            std::filesystem::create_directories(path.parent_path());
            fo4::localization::write_string_table_file(table.table, path);
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

Result<LocalizedStringAllocation> RadiclePluginBackend::openLocalizedStrings(
    const LoadedPlugin& loaded, std::string_view language) const {
    if (loaded.stagedLocalization.has_value()) return std::unexpected(pendingLocalizationError());
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (!testJournalRoot_.empty() && (!dataDirectory_.empty() || (loaded.stagedAllocation && loaded.stagedAllocation->heldGeneration))) {
        auto held = testOpenHeldLocalizedStrings(loaded, language);
        if (held && localizedAllocationAfterReadHook != nullptr) localizedAllocationAfterReadHook();
        return held;
    }
#endif
    LocalizedStringAllocation allocation;
    allocation.language = std::string(language);
    auto tables = readStringTables(loaded, language, nullptr, &allocation.sourceSnapshots);
    if (!tables) return std::unexpected(tables.error());
    auto next = nextLocalizedStringId(loaded, &allocation);
    if (!next) return std::unexpected(next.error());
    allocation.nextId = *next;
    for (const auto source : {StringSource::normal, StringSource::dl, StringSource::il}) {
        allocation.tables.emplace(source, effectiveTable(*tables, source));
    }
#ifdef FO4IDE_TEST_HOOKS
    if (localizedAllocationAfterReadHook != nullptr) localizedAllocationAfterReadHook();
#endif
    return allocation;
}

Result<std::uint32_t> RadiclePluginBackend::allocateLocalizedString(
    LocalizedStringAllocation& allocation, StringSource source, std::string_view text) const {
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (allocation.heldGeneration) {
        try {
            allocation.heldGeneration->reserveWorkingBytes(256);
            allocation.heldGeneration->reserveWorkingBytes(text.size());
            allocation.heldGeneration->reserveWorkingBytes(text.size());
        }
        catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSourcesUnavailable", error.what()}); }
    }
#endif
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
    allocation.allocatedIds[source].insert(id);
    return id;
}

Result<void> RadiclePluginBackend::stageLocalizedStringsAndPlugin(
    std::string_view plugin,
    LocalizedStringAllocation allocation,
    fo4::Plugin mutated) {
    auto* loaded = findPlugin(plugin);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + std::string(plugin)});
    if (loaded->stagedAllocation) { auto valid = validateLocalizedAllocation(*loaded, *loaded->stagedAllocation); if (!valid) return std::unexpected(valid.error()); }
    if (!allocation.touched.empty()) { auto valid = validateLocalizedAllocation(*loaded, allocation); if (!valid) return std::unexpected(valid.error()); }
    const bool hasAllocation = !allocation.touched.empty();
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    const auto heldGeneration = allocation.heldGeneration;
#endif
    auto dependentLanguages = loaded->stagedPluginLanguages; if (!allocation.touched.empty()) dependentLanguages.insert(allocation.language);
    auto dependentIds = loaded->stagedPluginStringIds;
    std::vector<std::pair<StagedStringKey, StagedStringTable>> prepared;
    prepared.reserve(allocation.touched.size());
    for (const auto source : allocation.touched) {
        auto result = prepareAllocatedStringTable(plugin, allocation, source);
        if (!result) return std::unexpected(result.error());
        if (const auto ids = allocation.allocatedIds.find(source); ids != allocation.allocatedIds.end())
            dependentIds[{allocation.language, source, result->file.path}].insert(ids->second.begin(), ids->second.end());
        prepared.emplace_back(
            StagedStringKey{loaded->name, allocation.language, source, result->file.path},
            std::move(*result));
    }
    std::unique_ptr<LocalizedStringAllocation> retained;
    if (hasAllocation) retained = std::make_unique<LocalizedStringAllocation>(std::move(allocation));
    CommitScope commit;
    std::scoped_lock staged(stagedStringsMutex_);
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (heldGeneration) testReserveHeldStagedCopy(*heldGeneration);
#endif
    auto replacement = stagedStrings_;
    std::size_t inserted = 0;
    for (auto& [key, table] : prepared) {
        replacement.insert_or_assign(std::move(key), std::move(table));
        if (++inserted == 1) {
#ifdef FO4IDE_TEST_HOOKS
            if (localizedStringBatchAfterInsertHook != nullptr) localizedStringBatchAfterInsertHook();
#endif
        }
    }
    markGraphDirtyLocked();
    static_assert(std::is_nothrow_swappable_v<fo4::Plugin>);
    static_assert(std::is_nothrow_swappable_v<LocalizedReferences> && std::is_nothrow_swappable_v<std::set<std::string>>);
    using std::swap;
    swap(loaded->plugin, mutated);
    static_assert(std::is_nothrow_swappable_v<decltype(loaded->stagedAllocation)>); if (hasAllocation) loaded->stagedAllocation.swap(retained);
    loaded->stagedPluginLanguages.swap(dependentLanguages);
    loaded->stagedPluginStringIds.swap(dependentIds);
    stagedStrings_.swap(replacement);
    loaded->dirty = true;
    ++mutationEpoch_;
    return {};
}

std::optional<RadiclePluginBackend::StagedLocalizationFile> RadiclePluginBackend::localizationFile(
    const std::filesystem::path& directory,
    std::string_view filename,
    std::string_view kind,
    std::string_view language,
    std::span<const std::byte> originalBytes,
    bool originalExists) const {
    std::error_code directoryEc;
    const auto canonicalDirectory = std::filesystem::weakly_canonical(directory, directoryEc);
    if (directoryEc || canonicalDirectory.empty()) return std::nullopt;
    std::filesystem::path selectedRoot;
    std::filesystem::path selectedRelative;
    std::size_t selectedDepth = 0;
    bool ambiguous = false;
    for (const auto& root : trustedSaveRoots_) {
        std::error_code relativeEc;
        const auto relative = std::filesystem::relative(canonicalDirectory, root, relativeEc);
        if (relativeEc || relative.is_absolute()) continue;
        bool within = true;
        for (const auto& part : relative) {
            if (part == "..") { within = false; break; }
        }
        if (!within) continue;
        const auto depth = static_cast<std::size_t>(std::distance(root.begin(), root.end()));
        if (selectedRoot.empty() || depth > selectedDepth) {
            selectedRoot = root;
            selectedRelative = relative;
            selectedDepth = depth;
            ambiguous = false;
        } else if (depth == selectedDepth && root != selectedRoot) {
            ambiguous = true;
        }
    }
    if (selectedRoot.empty() || ambiguous) return std::nullopt;
    StagedLocalizationFile file;
    file.path = canonicalDirectory / fo4::io::from_utf8(filename);
    file.rootSelector = selectedRoot;
    file.filename = std::string(filename);
    file.kind = std::string(kind);
    file.language = std::string(language);
    file.originalExists = originalExists;
    file.originalSize = originalBytes.size();
    if (originalExists) file.originalHash = fo4::io::sha256_hex(originalBytes);
    for (const auto& part : selectedRelative) {
        if (part == ".") continue;
        if (part == ".." || part.empty()) return std::nullopt;
        file.directories.push_back(fo4::io::utf8(part));
    }
    return file;
}

Result<void> RadiclePluginBackend::stageStringTable(
    std::string_view plugin,
    std::string_view language,
    StringSource source,
    StringTable table,
    const std::filesystem::path& directory,
    const StagedLocalizationFile* baseline,
    const std::vector<fo4::env::PluginStringTableSnapshot>* snapshots) {
    Result<StagedStringTable> prepared = std::unexpected(Error{"StringEditFailed", "string table was not prepared"});
    if (snapshots != nullptr) {
        LocalizedStringAllocation allocation; allocation.language = language; allocation.sourceSnapshots = *snapshots;
        allocation.tables.emplace(source, std::move(table));
        prepared = prepareAllocatedStringTable(plugin, allocation, source);
    } else {
        prepared = prepareStringTable(plugin, language, source, std::move(table), directory, baseline);
    }
    if (!prepared) return std::unexpected(prepared.error());
    const auto* loaded = findPlugin(plugin);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + std::string(plugin)});
    CommitScope commit;
    std::scoped_lock staged(stagedStringsMutex_);
    const StagedStringKey key{loaded->name, std::string(language), source, prepared->file.path};
    stagedStrings_.insert_or_assign(key, std::move(*prepared));
    ++mutationEpoch_;
    return {};
}

Result<RadiclePluginBackend::StagedStringTable> RadiclePluginBackend::prepareStringTable(
    std::string_view plugin,
    std::string_view language,
    StringSource source,
    StringTable table,
    const std::filesystem::path& directory,
    const StagedLocalizationFile* baseline, bool heldVerified) {
    const auto* loaded = findPlugin(plugin);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + std::string(plugin)});
    const auto folder = directory.empty() ? loaded->path.parent_path() / "Strings" : directory;
    const auto tablePath = fo4::localization::string_table_path(loaded->name, tableKind(source), language);
    const auto filename = fo4::io::utf8(tablePath.filename());
    const auto kind = source == StringSource::normal ? "strings" : source == StringSource::dl ? "dlstrings" : "ilstrings";
    const auto destination = localizationFile(folder, filename, kind, language, {}, false);
    if (!destination) {
        return std::unexpected(Error{"StringSavePathNotAllowed", "string table destination is outside an approved writable root"});
    }
    ExistingFile original;
    if (baseline != nullptr) {
        if (!support::sameLocalizedPath(baseline->path, destination->path) || baseline->kind != kind || baseline->language != language) {
            return std::unexpected(Error{"StringTableDestinationConflict", "string table destination changed"});
        }
        if (!heldVerified) {
            auto current = readExistingFile(destination->path);
            if (!current) return std::unexpected(current.error());
            const auto hash = current->exists ? fo4::io::sha256_hex(current->bytes) : std::string{};
            if (current->exists != baseline->originalExists || current->bytes.size() != baseline->originalSize ||
                hash != baseline->originalHash) {
                return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", "localized sibling table changed after it was read; reload before writing"});
            }
        }
    } else {
        auto tables = readStringTables(*loaded, language);
        if (!tables) return std::unexpected(tables.error());
        for (const auto& candidate : *tables) {
            if (candidate.source != source || !candidate.archive.empty()) continue;
            std::error_code ec;
            const auto resolved = std::filesystem::weakly_canonical(candidate.path, ec);
            if (ec) return std::unexpected(Error{"StringTableDestinationConflict", "string table source cannot be resolved"});
            if (support::sameLocalizedPath(resolved.parent_path(), destination->path.parent_path()) && !support::sameLocalizedPath(resolved, destination->path))
                return std::unexpected(Error{"StringTableDestinationConflict", "string table filename casing differs from the destination"});
        }
        auto current = readExistingFile(destination->path);
        if (!current) return std::unexpected(current.error());
        original = std::move(*current);
    }
    std::scoped_lock staged(stagedStringsMutex_);
    StagedLocalizationFile file;
    if (baseline != nullptr) {
        file = *baseline;
    } else {
        const auto snapshot = localizationFile(folder, filename, kind, language,
                                               original.bytes, original.exists);
        if (!snapshot) {
            return std::unexpected(Error{"StringSavePathNotAllowed", "string table destination is outside an approved writable root"});
        }
        file = *snapshot;
    }
    const StagedStringTable* existing = nullptr;
    for (const auto& [otherKey, value] : stagedStrings_) {
        if (std::get<0>(otherKey) != loaded->name || std::get<1>(otherKey) != language || std::get<2>(otherKey) != source ||
            !support::sameLocalizedPath(std::get<3>(otherKey), destination->path)) continue;
        if (existing) return std::unexpected(Error{"StringTableDestinationConflict", "staged localized destination is ambiguous"});
        existing = &value;
    }
    if (existing) file = existing->file;
    file.finalBytes = fo4::localization::write_string_table(table);
    file.verifyOnly = false;
    return StagedStringTable{std::move(table), std::move(file)};
}
Result<void> RadiclePluginBackend::stageQuestLocalizationMutationLocked(
    LoadedPlugin& loaded,
    StagedLocalization localization,
    fo4::Plugin mutated,
    StringSource source, StagedStringTable table, LocalizedStringAllocation allocation) {
    if (loaded.stagedAllocation) { auto valid = validateLocalizedAllocation(loaded, *loaded.stagedAllocation); if (!valid) return std::unexpected(valid.error()); }
    if (auto valid = validateLocalizedAllocation(loaded, allocation); !valid) return std::unexpected(valid.error());
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    const auto heldGeneration = allocation.heldGeneration;
    if (heldGeneration) { try { testReserveHeldWriter(*heldGeneration, table.table); } catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSourcesUnavailable", error.what()}); } }
#endif
    for (const auto& file : localization.participants) {
        auto current = readExistingFile(file.path);
        if (!current) return std::unexpected(current.error());
        const auto hash = current->exists ? fo4::io::sha256_hex(current->bytes) : std::string{};
        if (current->exists != file.originalExists || current->bytes.size() != file.originalSize || hash != file.originalHash)
            return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", "localized sibling table changed after it was read"});
    }
    auto retained = std::make_unique<LocalizedStringAllocation>(std::move(allocation));
    CommitScope commit;
    std::scoped_lock staged(stagedStringsMutex_);
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (heldGeneration) { try { testReserveHeldStagedCopy(*heldGeneration); } catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSourcesUnavailable", error.what()}); } }
#endif
    const StagedStringKey key{loaded.name, table.file.language, source, table.file.path};
    auto dependentLanguages = loaded.stagedPluginLanguages; dependentLanguages.insert(table.file.language);
    auto dependentIds = questStringReferences(loaded, mutated, table.file.language, source, table.file.path);
    auto replacement = stagedStrings_;
    replacement.insert_or_assign(key, std::move(table));
#ifdef FO4IDE_TEST_HOOKS
    if (localizedStringBatchAfterInsertHook != nullptr) localizedStringBatchAfterInsertHook();
#endif
    markGraphDirtyLocked();
    static_assert(std::is_nothrow_swappable_v<fo4::Plugin>);
    static_assert(std::is_nothrow_swappable_v<LocalizedReferences> && std::is_nothrow_swappable_v<std::set<std::string>> && std::is_nothrow_swappable_v<decltype(loaded.stagedAllocation)>);
    using std::swap;
    swap(loaded.plugin, mutated);
    loaded.stagedAllocation.swap(retained);
    loaded.stagedPluginLanguages.swap(dependentLanguages);
    loaded.stagedPluginStringIds.swap(dependentIds);
    stagedStrings_.swap(replacement);
    static_assert(std::is_nothrow_move_assignable_v<StagedLocalization>);
    loaded.stagedLocalization = std::move(localization);
    loaded.dirty = true;
    ++mutationEpoch_;
    return {};
}
}
