#include "RadiclePluginSupport.hpp"
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
#include "RadiclePluginHeldLocalization.hpp"
#endif

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <type_traits>

#include <fo4/environment/strings.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <fo4ide/plugin/RadiclePluginStringLanguages.hpp>

namespace fo4ide::plugin {
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

namespace {
using StringSource = fo4::env::StringSource;
fo4::localization::StringTableKind tableKind(StringSource source) {
    if (source == StringSource::dl) return fo4::localization::StringTableKind::dlstrings;
    if (source == StringSource::il) return fo4::localization::StringTableKind::ilstrings;
    return fo4::localization::StringTableKind::strings;
}

std::set<std::uint32_t> referencedIds(const fo4::Plugin& plugin, StringSource source) {
    std::set<std::uint32_t> ids;
    if (!plugin.header.tes4_flags.is_localized()) return ids;
    for (const auto* record : fo4::collect_records(plugin)) {
        for (const auto& row : record->subrecords) {
            if (row.data.size() != sizeof(std::uint32_t) ||
                !fo4::schema::is_localized_field(record->header.signature, row.signature) ||
                fo4::env::string_source(record->header.signature, row.signature) != source) continue;
            std::uint32_t id = 0;
            std::memcpy(&id, row.data.data(), sizeof(id));
            ids.insert(id);
        }
    }
    return ids;
}
}

Result<nlohmann::json> RadiclePluginBackend::discardStringEdits(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    if (pluginName.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto language = support::stringLanguage(arguments);
    if (!language) return std::unexpected(language.error());
    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded && loaded->stagedPluginLanguages.contains(*language))
        return std::unexpected(Error{"LocalizedPendingPluginEdit", "Discard the plugin edit before discarding its string tables"});
    const auto name = loaded ? loaded->name : pluginName;
    CommitScope commit;
    std::scoped_lock staged(stagedStringsMutex_);
    const auto discarded = std::erase_if(stagedStrings_, [&name, &language](const auto& entry) {
        return std::get<0>(entry.first) == name && std::get<1>(entry.first) == *language;
    });
    if (loaded && loaded->stagedPluginLanguages.empty() && std::none_of(stagedStrings_.begin(), stagedStrings_.end(),
        [&name](const auto& entry) { return std::get<0>(entry.first) == name; })) loaded->stagedAllocation.reset();
    if (discarded) ++mutationEpoch_;
    return support::dumpPayload({{"discarded", discarded}});
}

Result<void> RadiclePluginBackend::validateStringIdsRetained(
    const LoadedPlugin& loaded, std::string_view language, StringSource source,
    const fo4::localization::LocalizedStringTable& table) const {
    const auto directory = loaded.path.parent_path() / "Strings";
    const auto filename = fo4::io::utf8(fo4::localization::string_table_path(loaded.name, tableKind(source), language).filename());
    const auto kind = source == StringSource::normal ? "strings" : source == StringSource::dl ? "dlstrings" : "ilstrings";
    const auto destination = localizationFile(directory, filename, kind, language, {}, false);
    if (!destination) return std::unexpected(Error{"StringSavePathNotAllowed", "string table destination is outside an approved writable root"});
    const auto referenced = referencedIds(loaded.plugin, source);
    for (const auto& [key, ids] : loaded.stagedPluginStringIds) {
        const auto& [lang, kind, path] = key;
        if (lang != language || kind != source || !support::sameLocalizedPath(path, destination->path)) continue;
        for (const auto id : ids) {
            if (referenced.contains(id) && fo4::localization::find_string(table, id) == nullptr)
                return std::unexpected(Error{"LocalizedStringInUse", "a staged plugin record references this string ID"});
        }
    }
    return {};
}

LocalizedReferences RadiclePluginBackend::questStringReferences(
    const LoadedPlugin& loaded, const fo4::Plugin& mutated, std::string_view language, StringSource source,
    const std::filesystem::path& destination) const {
    auto result = loaded.stagedPluginStringIds;
    const auto ids = referencedIds(mutated, source);
    result[{std::string(language), source, destination}].insert(ids.begin(), ids.end());
    return result;
}

Result<std::set<std::string>> RadiclePluginBackend::localizationLanguages(const LoadedPlugin& loaded) const {
    try {
        std::vector<std::filesystem::path> directories{loaded.path.parent_path() / "Strings"};
        if (!dataDirectory_.empty()) directories.push_back(dataDirectory_ / "Strings");
        std::vector<std::string> stagedLanguages;
        {
            std::scoped_lock staged(stagedStringsMutex_);
            for (const auto& entry : stagedStrings_)
                if (std::get<0>(entry.first) == loaded.name) stagedLanguages.push_back(std::get<1>(entry.first));
        }
        std::vector<std::string_view> stagedViews;
        for (const auto& language : stagedLanguages) stagedViews.push_back(language);
        std::vector<SkippedStringArchive> skipped;
        const auto assets = stringAssets(loaded, &skipped);
        if (!skipped.empty()) return std::unexpected(Error{"LocalizedSourcesUnavailable", "an applicable string archive is unreadable"});
        return discoverPluginStringLanguages(loaded.name, directories, assets.get(), stagedViews);
    } catch (const std::exception&) {
        return std::unexpected(Error{"LocalizedSourcesUnavailable", "could not inspect localized source providers"});
    }
}

Result<bool> RadiclePluginBackend::localStringDirectoryExists(const LoadedPlugin& loaded) const {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(loaded.path.parent_path() / "Strings", ec);
    if (ec == std::errc::no_such_file_or_directory || (!ec && !std::filesystem::exists(status))) return false;
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
        return std::unexpected(Error{"LocalizedSourcesUnavailable", "localized source directory is unsafe or unavailable"});
    return true;
}

Result<LocalizedDirectorySnapshot> RadiclePluginBackend::captureLocalizedDirectory(
    const LoadedPlugin& loaded, const std::filesystem::path& directory) const {
    try {
        const auto approved = allowedLocalizationDirectory(directory);
        if (!approved) return std::unexpected(Error{"LocalizedSourcesUnavailable", "localized source directory is not approved"});
        std::vector<std::string> stagedLanguages;
        {
            std::scoped_lock staged(stagedStringsMutex_);
            for (const auto& [key, value] : stagedStrings_) {
                if (std::get<0>(key) == loaded.name && support::sameLocalizedPath(value.file.path.parent_path(), *approved))
                    stagedLanguages.push_back(std::get<1>(key));
            }
        }
        std::vector<std::string_view> views;
        for (const auto& language : stagedLanguages) views.push_back(language);
        const std::vector<std::filesystem::path> directories{*approved};
        const auto languages = discoverPluginStringLanguages(loaded.name, directories, nullptr, views);
        if (!languages) return std::unexpected(languages.error());
        LocalizedDirectorySnapshot result; result.directory = *approved;
        for (const auto& language : *languages) {
            auto snapshots = fo4::env::load_plugin_string_table_snapshots(directories, loaded.plugin, language);
            for (const auto& snapshot : snapshots) {
                auto table = snapshot.resolved.table;
                if (const auto staged = stagedStringTable(loaded.name, language, snapshot.resolved.source, snapshot.resolved.path))
                    table = staged->table;
                for (const auto& row : table.entries) result.maximum = std::max(result.maximum, row.id);
            }
            result.languages.emplace(language, std::move(snapshots));
        }
        {
            std::scoped_lock staged(stagedStringsMutex_);
            for (const auto& [key, value] : stagedStrings_) {
                if (std::get<0>(key) != loaded.name || !support::sameLocalizedPath(value.file.path.parent_path(), *approved)) continue;
                for (const auto& row : value.table.entries) result.maximum = std::max(result.maximum, row.id);
            }
        }
        return result;
    } catch (const std::exception&) {
        return std::unexpected(Error{"LocalizedSourcesUnavailable", "could not capture localized source directory"});
    }
}

Result<std::uint32_t> RadiclePluginBackend::nextLocalizedStringId(
    const LoadedPlugin& loaded, LocalizedStringAllocation* allocation) const {
    std::uint32_t maximum = 0;
    if (loaded.stagedAllocation) {
        auto valid = validateLocalizedAllocation(loaded, *loaded.stagedAllocation);
        if (!valid) return std::unexpected(valid.error());
        for (const auto& source : loaded.stagedAllocation->additionalSources)
            maximum = std::max(maximum, source.maximum);
        if (allocation) allocation->additionalSources = loaded.stagedAllocation->additionalSources;
    }
    if (allocation) {
        auto exists = localStringDirectoryExists(loaded);
        if (!exists) return std::unexpected(exists.error());
        allocation->defaultDirectoryExists = *exists;
    }
    auto languages = localizationLanguages(loaded);
    if (!languages) return std::unexpected(languages.error());
    if (allocation && !allocation->language.empty()) languages->insert(allocation->language);
    for (const auto& language : *languages) {
        auto tables = readStringTables(loaded, language, nullptr, allocation ? &allocation->allocationSources[language] : nullptr);
        if (!tables) return std::unexpected(tables.error());
        for (const auto& found : *tables)
            for (const auto& entry : found.table.entries) maximum = std::max(maximum, entry.id);
    }
    {
        std::scoped_lock staged(stagedStringsMutex_);
        for (const auto& [key, value] : stagedStrings_) {
            if (std::get<0>(key) != loaded.name) continue;
            for (const auto& entry : value.table.entries) maximum = std::max(maximum, entry.id);
        }
    }
    if (loaded.plugin.header.tes4_flags.is_localized()) {
        for (const auto* record : fo4::collect_records(loaded.plugin)) {
            for (const auto& row : record->subrecords) {
                if (row.data.size() != sizeof(std::uint32_t) ||
                    !fo4::schema::is_localized_field(record->header.signature, row.signature)) continue;
                std::uint32_t id = 0;
                std::memcpy(&id, row.data.data(), sizeof(id));
                maximum = std::max(maximum, id);
            }
        }
    }
    if (maximum == std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(Error{"LocalizedStringTableFull", "localized string-table ID space is exhausted"});
    return maximum + 1;
}

Result<std::uint32_t> RadiclePluginBackend::includeLocalizedDirectory(
    const LoadedPlugin& loaded, LocalizedStringAllocation& allocation,
    const std::filesystem::path& directory, std::uint32_t next) const {
    if (support::sameLocalizedPath(directory, loaded.path.parent_path() / "Strings")) return next;
    auto captured = captureLocalizedDirectory(loaded, directory);
    if (!captured) return std::unexpected(captured.error());
    if (captured->maximum == std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(Error{"LocalizedStringTableFull", "localized string-table ID space is exhausted"});
    next = std::max(next, captured->maximum + 1);
    auto previous = std::find_if(allocation.additionalSources.begin(), allocation.additionalSources.end(),
        [&](const auto& source) { return support::sameLocalizedPath(source.directory, captured->directory); });
    if (previous == allocation.additionalSources.end()) allocation.additionalSources.push_back(std::move(*captured));
    else *previous = std::move(*captured);
    return next;
}

Result<RadiclePluginBackend::StagedStringTable> RadiclePluginBackend::prepareAllocatedStringTable(
    std::string_view plugin, LocalizedStringAllocation& allocation, StringSource source) {
    const auto* loaded = findPlugin(plugin);
    if (loaded == nullptr) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded"});
    const auto directory = loaded->path.parent_path() / "Strings";
    const auto filename = fo4::io::utf8(fo4::localization::string_table_path(
        loaded->name, tableKind(source), allocation.language).filename());
    const auto kind = source == StringSource::normal ? "strings" : source == StringSource::dl ? "dlstrings" : "ilstrings";
    const auto destination = localizationFile(directory, filename, kind, allocation.language, {}, false);
    if (!destination) return std::unexpected(Error{"StringSavePathNotAllowed", "string table destination is outside an approved writable root"});
    const auto found = std::find_if(allocation.sourceSnapshots.begin(), allocation.sourceSnapshots.end(),
        [source](const auto& snapshot) { return snapshot.resolved.source == source; });
    std::error_code ec;
    if (found != allocation.sourceSnapshots.end() && !found->resolved.archive.empty()) {
        return std::unexpected(Error{"LocalizedAllocationSnapshotUnsupported", "localized allocation requires a verified loose destination snapshot"});
    }
    if (found == allocation.sourceSnapshots.end()) {
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
        if (!allocation.heldGeneration || !allocation.defaultDirectoryExists)
#endif
            return std::unexpected(Error{"LocalizedAllocationSnapshotUnsupported", "localized allocation requires a verified loose destination snapshot"});
    } else {
        const auto resolved = std::filesystem::weakly_canonical(found->resolved.path, ec);
        if (ec || !support::sameLocalizedPath(resolved, destination->path)) return std::unexpected(Error{"StringTableDestinationConflict", "string table source differs from the approved destination"});
    }
    const auto baseline = found == allocation.sourceSnapshots.end()
        ? localizationFile(directory, filename, kind, allocation.language, {}, false)
        : localizationFile(directory, filename, kind, allocation.language, found->bytes, true);
    if (!baseline) return std::unexpected(Error{"StringSavePathNotAllowed", "string table destination is outside an approved writable root"});
    bool heldVerified = false;
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (allocation.heldGeneration) {
        try { allocation.heldGeneration->verify(); testReserveHeldWriter(*allocation.heldGeneration, allocation.tables.at(source)); heldVerified = true; }
        catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", error.what()}); }
    }
#endif
    return prepareStringTable(plugin, allocation.language, source, std::move(allocation.tables.at(source)), directory, &*baseline, heldVerified);
}

Result<void> RadiclePluginBackend::stageAllocatedStringTable(
    std::string_view plugin, LocalizedStringAllocation allocation, StringSource source) {
    auto* loaded = findPlugin(plugin);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded"});
    if (loaded->stagedAllocation) {
        auto valid = validateLocalizedAllocation(*loaded, *loaded->stagedAllocation);
        if (!valid) return std::unexpected(valid.error());
    }
    auto valid = validateLocalizedAllocation(*loaded, allocation);
    if (!valid) return std::unexpected(valid.error());
    auto prepared = prepareAllocatedStringTable(plugin, allocation, source);
    if (!prepared) return std::unexpected(prepared.error());
    StagedStringKey key{loaded->name, allocation.language, source, prepared->file.path};
    auto retained = std::make_unique<LocalizedStringAllocation>(std::move(allocation));
    CommitScope commit;
    std::scoped_lock lock(stagedStringsMutex_);
    auto replacement = stagedStrings_;
    replacement.insert_or_assign(std::move(key), std::move(*prepared));
    static_assert(std::is_nothrow_swappable_v<decltype(loaded->stagedAllocation)>);
    loaded->stagedAllocation.swap(retained);
    stagedStrings_.swap(replacement);
    ++mutationEpoch_;
    return {};
}

Result<void> RadiclePluginBackend::validateLocalizedAllocation(
    const LoadedPlugin& loaded, const LocalizedStringAllocation& allocation) const {
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (allocation.heldGeneration) {
        try { allocation.heldGeneration->verify(); return {}; }
        catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", error.what()}); }
    }
#endif
    const auto changed = [] {
        return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", "localized allocation sources changed; reload before writing"});
    };
    const auto equal = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t i = 0; i < left.size(); ++i) {
            if (left[i].resolved.source != right[i].resolved.source ||
                !support::sameLocalizedPath(left[i].resolved.path, right[i].resolved.path) ||
                left[i].resolved.archive != right[i].resolved.archive || left[i].bytes != right[i].bytes) return false;
        }
        return true;
    };
    auto languages = localizationLanguages(loaded);
    if (!languages) return std::unexpected(languages.error());
    if (!allocation.language.empty()) languages->insert(allocation.language);
    const auto directoryExists = localStringDirectoryExists(loaded);
    if (!directoryExists) return std::unexpected(directoryExists.error());
    if (*directoryExists != allocation.defaultDirectoryExists) return changed();
    if (languages->size() != allocation.allocationSources.size()) return changed();
    for (const auto& language : *languages) {
        const auto original = allocation.allocationSources.find(language);
        if (original == allocation.allocationSources.end()) return changed();
        std::vector<fo4::env::PluginStringTableSnapshot> current;
        auto tables = readStringTables(loaded, language, nullptr, &current);
        if (!tables) return std::unexpected(tables.error());
        if (!equal(original->second, current)) return changed();
        if (language == allocation.language && !equal(allocation.sourceSnapshots, original->second)) return changed();
    }
    for (const auto& original : allocation.additionalSources) {
        const auto current = captureLocalizedDirectory(loaded, original.directory);
        if (!current) return std::unexpected(current.error());
        if (current->languages.size() != original.languages.size()) return changed();
        for (const auto& [language, snapshots] : original.languages) {
            const auto found = current->languages.find(language);
            if (found == current->languages.end() || !equal(snapshots, found->second)) return changed();
        }
    }
    return {};
}

std::optional<RadiclePluginBackend::StagedStringTable> RadiclePluginBackend::stagedStringTable(
    std::string_view plugin,
    std::string_view language,
    StringSource source,
    const std::filesystem::path& path) const {
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) return std::nullopt;
    std::scoped_lock staged(stagedStringsMutex_);
    const StagedStringTable* found = nullptr;
    for (const auto& [key, value] : stagedStrings_) {
        if (std::get<0>(key) != plugin || std::get<1>(key) != language || std::get<2>(key) != source ||
            !support::sameLocalizedPath(std::get<3>(key), canonical)) continue;
        if (found) throw std::runtime_error("ambiguous staged localized destination");
        found = &value;
    }
    return found ? std::optional<StagedStringTable>{*found} : std::nullopt;
}

bool RadiclePluginBackend::stringTablePending(std::string_view plugin,
                                              std::string_view language,
                                              StringSource source) const {
    std::scoped_lock staged(stagedStringsMutex_);
    return std::any_of(stagedStrings_.begin(), stagedStrings_.end(),
        [plugin, language, source](const auto& entry) {
            return std::get<0>(entry.first) == plugin && std::get<1>(entry.first) == language &&
                   std::get<2>(entry.first) == source;
        });
}

void RadiclePluginBackend::overlayStagedStringTables(const LoadedPlugin& loaded,
                                                     std::string_view language,
                                                     std::vector<fo4::env::PluginStringTable>& tables) const {
    std::scoped_lock staged(stagedStringsMutex_);
    for (const auto& [key, value] : stagedStrings_) {
        const auto& [name, lang, source, path] = key;
        if (name != loaded.name || lang != language) continue;
        const auto wanted = source;
        const auto defaultPath = loaded.path.parent_path() / "Strings" /
            fo4::localization::string_table_path(loaded.name, tableKind(wanted), language).filename();
        std::error_code ec;
        const auto canonicalDefault = std::filesystem::weakly_canonical(defaultPath, ec);
        if (ec || !support::sameLocalizedPath(canonicalDefault, path)) continue;
        const auto found = std::find_if(tables.begin(), tables.end(),
                                        [wanted](const fo4::env::PluginStringTable& candidate) {
                                            return candidate.source == wanted;
                                        });
        if (found != tables.end()) {
            found->table = value.table;
        } else {
            tables.push_back({
                wanted,
                defaultPath,
                {},
                value.table
            });
        }
    }
    std::sort(tables.begin(), tables.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.source < rhs.source;
    });
}


}
