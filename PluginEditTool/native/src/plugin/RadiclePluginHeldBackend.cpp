#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#if (defined(FO4IDE_TEST_HOOKS) && defined(__linux__)) || defined(_WIN32)
#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginHeldLocalization.hpp"
#include "RadiclePluginJournalSession.hpp"
#include "RadiclePluginSupport.hpp"
#include <fo4/io/path_text.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/localized_fields.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace fo4ide::plugin {
Result<std::shared_ptr<detail::HeldLocalizationGeneration>> RadiclePluginBackend::testCaptureHeldGeneration(
    const LoadedPlugin& loaded, std::string_view language) const {
    try {
        if (testJournalRoot_.empty()) throw std::runtime_error("journal read roots are not configured");
        const auto modPath = std::filesystem::absolute(loaded.path).lexically_normal().parent_path();
        const auto found = testReadRoots_.find(modPath);
        if (found == testReadRoots_.end() || !found->second) throw std::runtime_error("plugin read root is unavailable");
        auto modRoot = found->second;
        if (!dataDirectory_.empty() && !testDataReadRoot_) throw std::runtime_error("Data read root is unavailable: " + dataReadRootError_);
        const auto dataRoot = testDataReadRoot_;
        detail::JournalReadConfiguration config;
        std::vector<detail::HeldStringDirectoryInput> directories{{modRoot, "Strings"}};
        if (dataRoot) {
            config.approvedReadRoots.push_back({dataRoot->path(), dataRoot});
            config.archiveDirectories.push_back({dataRoot->path(), {}, {}});
        }
        if (!dataRoot || !dataRoot->sameRoot(*modRoot)) {
            config.approvedReadRoots.push_back({modRoot->path(), modRoot});
            config.archiveDirectories.push_back({modRoot->path(), {}, {}});
            if (dataRoot) directories.push_back({dataRoot, "Strings"});
        }
        std::vector<std::string> names, languages{std::string(language)};
        for (const auto& plugin : plugins_) names.push_back(plugin.name);
        {
            std::scoped_lock staged(stagedStringsMutex_);
            for (const auto& [key, value] : stagedStrings_) {
                (void)value;
                if (std::get<0>(key) == loaded.name) languages.push_back(std::get<1>(key));
            }
        }
        config.archiveProviderOrder = detail::journalArchiveOrder(dataRoot, modRoot, names, loaded.name);
        return std::make_shared<detail::HeldLocalizationGeneration>(detail::HeldLocalizationGeneration::capture(
            config, directories, loaded.name, languages, detail::kJournalMaxFileBytes));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"LocalizedSourcesUnavailable", error.what()});
    }
}

Result<std::uint32_t> RadiclePluginBackend::testHeldMaximum(const LoadedPlugin& loaded,
                                                            detail::HeldLocalizationGeneration& generation) const {
    try {
        generation.verify();
        auto maximum = generation.maximumStringId();
        if (loaded.stagedAllocation) {
            const auto& previous = *loaded.stagedAllocation;
            if (!previous.heldGeneration || !previous.additionalSources.empty())
                return std::unexpected(Error{"LocalizedSourcesUnavailable", "prior allocation sources are not held"});
            if (static_cast<bool>(previous.heldDataRoot) != static_cast<bool>(testDataReadRoot_) ||
                (previous.heldDataRoot && (!detail::sameJournalIdentity(previous.heldDataRoot->path(), testDataReadRoot_->path()) ||
                                           !previous.heldDataRoot->sameRoot(*testDataReadRoot_))))
                return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", "Data root changed after localized allocation"});
            if (auto valid = validateLocalizedAllocation(loaded, previous); !valid) return std::unexpected(valid.error());
        }
        {
            std::scoped_lock staged(stagedStringsMutex_);
            for (const auto& [key, value] : stagedStrings_) if (std::get<0>(key) == loaded.name)
                for (const auto& entry : value.table.entries) maximum = std::max(maximum, entry.id);
        }
        if (loaded.plugin.header.tes4_flags.is_localized()) for (const auto* record : fo4::collect_records(loaded.plugin)) {
            for (const auto& row : record->subrecords) {
                if (row.data.size() != sizeof(std::uint32_t) || !fo4::schema::is_localized_field(record->header.signature, row.signature)) continue;
                std::uint32_t id = 0; std::memcpy(&id, row.data.data(), sizeof(id)); maximum = std::max(maximum, id);
            }
        }
        return maximum;
    }
    catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSourcesUnavailable", error.what()}); }
}

#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
Result<std::uint32_t> RadiclePluginBackend::testHeldMaximumStringId(std::string_view plugin,
                                                                     std::string_view language) const {
    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(plugin);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded"});
    auto generation = testCaptureHeldGeneration(*loaded, language);
    if (!generation) return std::unexpected(generation.error());
    return testHeldMaximum(*loaded, **generation);
}
#endif

namespace {
void reserveTableCopy(detail::HeldLocalizationGeneration& generation, const fo4::localization::LocalizedStringTable& table) {
    generation.reserveWorkingBytes(4096);
    for (const auto& entry : table.entries) {
        generation.reserveWorkingBytes(sizeof(fo4::localization::LocalizedStringEntry) + 64);
        generation.reserveWorkingBytes(entry.text.size());
    }
}
}

Result<LocalizedStringAllocation> RadiclePluginBackend::testOpenHeldLocalizedStrings(
    const LoadedPlugin& loaded, std::string_view language) const {
    auto generation = testCaptureHeldGeneration(loaded, language);
    if (!generation) return std::unexpected(generation.error());
    const auto maximum = testHeldMaximum(loaded, **generation);
    if (!maximum) return std::unexpected(maximum.error());
    if (*maximum == std::numeric_limits<std::uint32_t>::max()) return std::unexpected(Error{"LocalizedStringTableFull", "localized string-table ID space is exhausted"});
    try {
        LocalizedStringAllocation allocation; allocation.language = std::string(language); allocation.nextId = *maximum + 1;
        allocation.sourceSnapshots = (*generation)->copySelectedSnapshots(language, detail::kJournalMaxFileBytes);
        for (const auto source : {fo4::env::StringSource::normal, fo4::env::StringSource::dl, fo4::env::StringSource::il}) {
            const auto kind = source == fo4::env::StringSource::normal ? fo4::localization::StringTableKind::strings :
                              source == fo4::env::StringSource::dl ? fo4::localization::StringTableKind::dlstrings : fo4::localization::StringTableKind::ilstrings;
            auto table = fo4::localization::LocalizedStringTable{kind, {}};
            const auto snapshot = std::find_if(allocation.sourceSnapshots.begin(), allocation.sourceSnapshots.end(), [&](const auto& item) { return item.resolved.source == source; });
            if (snapshot != allocation.sourceSnapshots.end()) table = std::move(snapshot->resolved.table);
            const auto filename = fo4::localization::string_table_path(loaded.name, kind, language).filename();
            const auto destination = loaded.path.parent_path() / "Strings" / filename;
            {
                std::scoped_lock staged(stagedStringsMutex_);
                const StagedStringTable* overlay = nullptr;
                for (const auto& [key, value] : stagedStrings_) {
                    if (std::get<0>(key) != loaded.name || std::get<1>(key) != language || std::get<2>(key) != source ||
                        !support::sameLocalizedPath(std::get<3>(key), destination)) continue;
                    if (overlay) throw std::runtime_error("ambiguous staged string table");
                    overlay = &value;
                }
                if (overlay) { reserveTableCopy(**generation, overlay->table); table = overlay->table; }
            }
            allocation.tables.emplace(source, std::move(table));
        }
        allocation.defaultDirectoryExists = (*generation)->directories().front().exists();
        allocation.heldGeneration = std::move(*generation);
        allocation.heldDataRoot = testDataReadRoot_;
        return allocation;
    } catch (const std::exception& error) { return std::unexpected(Error{"LocalizedSourcesUnavailable", error.what()}); }
}

void RadiclePluginBackend::testReserveHeldWriter(detail::HeldLocalizationGeneration& generation,
                                                  const fo4::localization::LocalizedStringTable& table) {
    generation.reserveWorkingBytes(1024 * 1024);
    for (const auto& entry : table.entries) {
        generation.reserveWorkingBytes(256);
        if (entry.text.size() > std::numeric_limits<std::size_t>::max() / 8) throw std::runtime_error("string writer budget exceeded");
        generation.reserveWorkingBytes(entry.text.size() * 8);
    }
}

void RadiclePluginBackend::testReserveHeldStagedCopy(detail::HeldLocalizationGeneration& generation) const {
    for (const auto& [key, value] : stagedStrings_) {
        generation.reserveWorkingBytes(8192);
        generation.reserveWorkingBytes(std::get<0>(key).size()); generation.reserveWorkingBytes(std::get<1>(key).size());
        const auto pathSize = fo4::io::utf8(std::get<3>(key)).size();
        if (pathSize > std::numeric_limits<std::size_t>::max() / 4) throw std::runtime_error("staged path budget exceeded");
        generation.reserveWorkingBytes(pathSize * 4);
        generation.reserveWorkingBytes(value.file.finalBytes.size());
        reserveTableCopy(generation, value.table);
    }
}
}
#endif
