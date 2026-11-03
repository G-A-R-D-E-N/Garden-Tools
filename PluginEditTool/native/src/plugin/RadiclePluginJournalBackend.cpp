#include "RadiclePluginSupport.hpp"
#ifdef FO4IDE_TEST_HOOKS
#include "RadiclePluginJournalSession.hpp"
#ifdef __linux__
#include "RadiclePluginJournalProfile.hpp"
#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginHeldLocalization.hpp"
#endif
#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/plugin/writer.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin::detail {
namespace { JournalTimestampHook timestampHook = nullptr; }
void setJournalTimestampHook(JournalTimestampHook hook) { timestampHook = hook; }
}

namespace fo4ide::plugin {

void RadiclePluginBackend::testRecoverJournal(detail::JournalSession& session,
                                               const std::vector<detail::JournalRoots>& approved,
                                               const nlohmann::json& arguments, TestReadRoots& readRoots) const {
#ifdef __linux__
    for (const auto& entry : arguments.at("plugins")) {
        if (!entry.is_object() || !entry.value("resolved", false)) continue;
        const auto name = entry.value("name", std::string{}), path = entry.value("path", std::string{});
        if (name.empty() || path.empty()) continue;
        std::filesystem::path parent;
        try { parent = std::filesystem::absolute(fo4::io::from_utf8(path)).lexically_normal().parent_path(); }
        catch (const std::exception&) { throw detail::JournalRecoveryProfileInvalid("invalid profile plugin path: " + name); }
        if (!readRoots.contains(parent)) {
            if (readRoots.size() == 4096) throw detail::JournalRecoveryProfileInvalid("too many profile read roots");
            try { readRoots.emplace(parent, std::make_shared<detail::Ba2ReadRoot>(detail::Ba2ReadRoot::authorize(parent))); }
            catch (const std::exception&) { throw detail::JournalRecoveryProfileInvalid("unavailable profile plugin: " + name); }
        }
    }
    session.recover(approved, [&](const auto& manifest, const auto& finalPlugin, auto& config) {
        detail::prepareJournalProfile(arguments, dataDirectory_, testDataReadRoot_, readRoots, manifest, finalPlugin, config);
    });
#else
    (void)arguments; (void)readRoots;
    session.recover(approved);
#endif
}

RadiclePluginBackend::TestReadRoots RadiclePluginBackend::testFilterReadRoots(
    const TestReadRoots& roots, const std::vector<LoadedPlugin>& loaded) {
    TestReadRoots result;
#ifdef __linux__
    for (const auto& plugin : loaded) {
        const auto path = std::filesystem::absolute(plugin.path).lexically_normal();
        const auto found = roots.find(path.parent_path());
        if (found == roots.end() || !found->second || !plugin.diskBaseline) throw std::runtime_error("profile read root is unavailable");
        auto source = found->second->open(path.filename(), detail::kJournalMaxFileBytes);
        if (source->size() != plugin.diskBaseline->size()) throw std::runtime_error("profile plugin changed during load");
        std::array<std::byte, 65536> buffer{};
        for (std::size_t offset = 0; offset < plugin.diskBaseline->size();) {
            const auto count = std::min(buffer.size(), plugin.diskBaseline->size() - offset);
            source->read(offset, std::span(buffer).first(count));
            if (!std::equal(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(count), plugin.diskBaseline->begin() + static_cast<std::ptrdiff_t>(offset)))
                throw std::runtime_error("profile plugin changed during load");
            offset += count;
        }
        result.emplace(found->first, found->second);
    }
#else
    (void)roots; (void)loaded;
#endif
    return result;
}

#ifdef __linux__
bool RadiclePluginBackend::testHasReadAuthority(const std::filesystem::path& root) const {
    OpLock lock(*this, OpLock::Mode::Read);
    const auto found = testReadRoots_.find(std::filesystem::absolute(root).lexically_normal());
    if (found == testReadRoots_.end()) return false;
    static_cast<void>(found->second->list({}, 4096));
    return true;
}
#endif

std::optional<Error> RadiclePluginBackend::testRecoveryGate(std::string_view operation,
                                                           const nlohmann::json& arguments) const {
    OpLock lock(*this, OpLock::Mode::Read);
    if (testRecoveryPlugin_.empty() || operation == "environment.LoadMo2") return std::nullopt;
    const bool grid = operation == "GetRecordsGrid" || operation == "backend.GetRecordsGrid";
    if (grid) {
        const auto name = support::stringArgument(arguments, 0);
        if (!name.empty() && fo4::PluginKey::from_filename(name) != fo4::PluginKey::from_filename(testRecoveryPlugin_))
            return std::nullopt;
    }
    return Error{"JournalRecoveryRequired", "Plugin journal recovery is required: " + testRecoveryPlugin_};
}

namespace {
std::vector<std::byte> asBytes(std::string_view text) {
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}
}

Result<void> RadiclePluginBackend::persistJournalForTestLocked(LoadedPlugin& loaded,
                                                               const std::filesystem::path& output,
                                                               std::optional<std::string_view> stringsOnly) {
    using namespace detail;
    std::string pendingName = loaded.name;
    if (output != loaded.path) return std::unexpected(Error{"SavePathNotAllowed", "Journal Save As is unavailable"});
    try {
#ifdef __linux__
        const auto held = loaded.stagedAllocation ? loaded.stagedAllocation->heldGeneration : nullptr;
        const auto dataRoot = held ? loaded.stagedAllocation->heldDataRoot : nullptr;
        if (held && stringsOnly) return std::unexpected(Error{"LocalizedSourceRecoveryUnavailable", "Selective saves require held-source rebasing"});
        if (held && (static_cast<bool>(dataRoot) != static_cast<bool>(testDataReadRoot_) ||
            (dataRoot && (!sameJournalIdentity(dataRoot->path(), testDataReadRoot_->path()) || !dataRoot->sameRoot(*testDataReadRoot_)))))
            return std::unexpected(Error{"LocalizedSidecarChangedOnDisk", "Data root changed after localized allocation"});
#endif
        StateReleased released;
        std::unique_lock saveIo(saveIoMutex_);
        if (!diskMatchesBaseline(loaded)) return std::unexpected(Error{"PluginChangedOnDisk", "Plugin bytes changed after loading"});
        JournalSession session(testJournalRoot_);
        if (loaded.stagedAllocation) {
            auto valid = validateLocalizedAllocation(loaded, *loaded.stagedAllocation);
            if (!valid) return std::unexpected(valid.error());
#ifdef __linux__
            if (!held)
#endif
            if (!dataDirectory_.empty()) {
                std::error_code ec;
                if (!std::filesystem::equivalent(dataDirectory_, loaded.path.parent_path(), ec) || ec)
                    return std::unexpected(Error{"LocalizedSourceRecoveryUnavailable", "Data source roots require recoverable read-only authorization"});
            }
#ifdef __linux__
            if (!held)
#endif
            {
                std::vector<SkippedStringArchive> skipped;
                const auto assets = stringAssets(loaded, &skipped);
                if (!skipped.empty() || !assets->archives().empty())
                    return std::unexpected(Error{"LocalizedSourceRecoveryUnavailable", "archive providers require recoverable source snapshots"});
            }
        }
        JournalRoots roots; roots.plugin = loaded.path;
        roots.pluginDirectoryIdentity = JournalDestination::captureRoot(loaded.path.parent_path());
        for (const auto& path : trustedSaveRoots_) roots.approvedRoots.push_back({path, JournalDestination::captureRoot(path)});
        std::vector<JournalArchiveInventory> archives;
        std::vector<JournalReadStringInventory> readStrings;
#ifdef __linux__
        if (held) {
            const auto& captured = held->archives();
            roots.approvedReadRoots = captured.readRoots();
            archives = captured.inventories();
            for (const auto& inventory : archives) roots.archiveDirectories.push_back({inventory.rootSelector, inventory.directories, {}});
            roots.archiveProviderOrder = captured.providerOrder();
            if (dataRoot) {
                const auto approved = std::find_if(roots.approvedReadRoots.begin(), roots.approvedReadRoots.end(), [&](const auto& root) {
                    return root.lease && sameJournalIdentity(root.path, dataRoot->path()) && root.lease->sameRoot(*dataRoot);
                });
                if (approved == roots.approvedReadRoots.end()) throw std::runtime_error("held Data root changed");
                roots.readStringDirectories.push_back({approved->path, {"Strings"}, {}});
                readStrings.push_back(held->readOnlyInventory(approved->path, "Strings"));
            }
        }
#endif
        auto pluginBytes = stringsOnly ? loaded.diskBaseline :
            std::make_shared<const std::vector<std::byte>>(fo4::write_plugin(loaded.plugin, output));
        std::vector<JournalInput> inputs{{"plugin", "", true, fo4::io::sha256_hex(*loaded.diskBaseline),
                                          loaded.diskBaseline->size(), *pluginBytes}};
        std::map<std::filesystem::path, StagedLocalizationFile> sidecars;
        std::map<std::pair<std::filesystem::path, std::vector<std::string>>, std::vector<std::string>> sourceNames;
        std::map<std::pair<std::filesystem::path, std::vector<std::string>>, bool> sourcePresence;
        std::vector<JournalSourceSet> sourceSets;
        std::unique_ptr<LocalizedStringAllocation> rebased;
        if (stringsOnly && loaded.stagedAllocation) rebased = std::make_unique<LocalizedStringAllocation>(*loaded.stagedAllocation);
        const auto add = [&](const StagedLocalizationFile& file) {
            const auto at = std::find_if(sidecars.begin(), sidecars.end(), [&](const auto& item) {
                return support::sameLocalizedPath(item.first, file.path);
            });
            if (at == sidecars.end()) { sidecars.emplace(file.path, file); return; }
            if (!support::sameLocalizedSpelling(at->first, file.path))
                throw std::runtime_error("localized destination has an unsupported alias");
            const auto& old = at->second;
            if (!support::sameLocalizedSpelling(old.rootSelector, file.rootSelector) ||
                old.kind != file.kind || old.language != file.language ||
                old.originalExists != file.originalExists || old.originalHash != file.originalHash ||
                old.originalSize != file.originalSize || (!old.verifyOnly && !file.verifyOnly && old.finalBytes != file.finalBytes))
                throw std::runtime_error("conflicting staged string table baselines");
            if (old.verifyOnly && !file.verifyOnly) at->second = file;
        };
        if (loaded.stagedAllocation) {
            const auto defaultRoot = localizationFile(loaded.path.parent_path() / "Strings", "source-set", "strings", loaded.stagedAllocation->language, {}, false);
            const auto archiveRoot = localizationFile(loaded.path.parent_path(), "archive-set", "strings", loaded.stagedAllocation->language, {}, false);
            if (!defaultRoot || !archiveRoot) return std::unexpected(Error{"StringSavePathNotAllowed", "allocation roots are not approved"});
            sourceNames[{defaultRoot->rootSelector, defaultRoot->directories}];
            sourcePresence[{defaultRoot->rootSelector, defaultRoot->directories}] = loaded.stagedAllocation->defaultDirectoryExists;
#ifdef __linux__
            if (!held)
#endif
            sourceSets.push_back({"archive-empty", archiveRoot->rootSelector, archiveRoot->directories, {}});
            const auto addInventory = [&](const auto& inventory, const std::filesystem::path& directory) -> Result<void> {
              for (const auto& [language, snapshots] : inventory) {
                for (const auto& snapshot : snapshots) {
                    if (!snapshot.resolved.archive.empty() || !support::sameLocalizedPath(snapshot.resolved.path.parent_path(), directory))
                        return std::unexpected(Error{"LocalizedAllocationSnapshotUnsupported", "allocation source requires verified journal provenance"});
                    const auto kind = snapshot.resolved.source == fo4::env::StringSource::normal ? "strings" :
                        snapshot.resolved.source == fo4::env::StringSource::dl ? "dlstrings" : "ilstrings";
                    auto file = localizationFile(snapshot.resolved.path.parent_path(), fo4::io::utf8(snapshot.resolved.path.filename()),
                                                 kind, language, snapshot.bytes, true);
                    if (!file) return std::unexpected(Error{"StringSavePathNotAllowed", "allocation source is outside an approved writable root"});
                    file->verifyOnly = true;
                    sourceNames[{file->rootSelector, file->directories}].push_back(file->filename);
                    add(*file);
                }
              }
              return {};
            };
            auto included = addInventory(loaded.stagedAllocation->allocationSources, loaded.path.parent_path() / "Strings");
            if (!included) return std::unexpected(included.error());
#ifdef __linux__
            if (held) {
                const auto& directories = held->directories();
                if (directories.empty() || !support::sameLocalizedPath(directories.front().directory(), loaded.path.parent_path() / "Strings"))
                    throw std::runtime_error("held plugin string directory changed");
                for (const auto& table : directories.front().tables()) {
                    const auto kind = table.source == fo4::env::StringSource::normal ? "strings" :
                                      table.source == fo4::env::StringSource::dl ? "dlstrings" : "ilstrings";
                    auto file = localizationFile(directories.front().directory(), table.filename, kind, table.language, table.bytes, true);
                    if (!file) return std::unexpected(Error{"StringSavePathNotAllowed", "held string source is outside an approved root"});
                    file->verifyOnly = true;
                    sourceNames[{file->rootSelector, file->directories}].push_back(file->filename);
                    add(*file);
                }
            }
#endif
            for (const auto& custom : loaded.stagedAllocation->additionalSources) {
                const auto customRoot = localizationFile(custom.directory, "source-set", "strings", loaded.stagedAllocation->language, {}, false);
                if (!customRoot) return std::unexpected(Error{"StringSavePathNotAllowed", "custom allocation root is not approved"});
                sourceNames[{customRoot->rootSelector, customRoot->directories}];
                sourcePresence[{customRoot->rootSelector, customRoot->directories}] = true;
                included = addInventory(custom.languages, custom.directory);
                if (!included) return std::unexpected(included.error());
            }
        }
        {
            std::scoped_lock lock(stagedStringsMutex_);
            if (rebased) {
                const auto update = [&](auto& snapshots, const StagedStringTable& value) {
                    for (auto& snapshot : snapshots) {
                        if (!support::sameLocalizedPath(snapshot.resolved.path, value.file.path)) continue;
                        snapshot.bytes = value.file.finalBytes;
                        snapshot.resolved.table = value.table;
                    }
                };
                for (const auto& [key, value] : stagedStrings_) {
                    if (std::get<0>(key) != loaded.name || std::get<1>(key) != *stringsOnly) continue;
                    if (rebased->language == *stringsOnly) update(rebased->sourceSnapshots, value);
                    for (auto& [language, snapshots] : rebased->allocationSources) if (language == *stringsOnly) update(snapshots, value);
                    for (auto& directory : rebased->additionalSources)
                        for (auto& [language, snapshots] : directory.languages) if (language == *stringsOnly) update(snapshots, value);
                }
            }
            if (!stringsOnly && loaded.stagedLocalization) {
                for (const auto& file : loaded.stagedLocalization->participants) {
                    auto baseline = file;
                    if (!file.verifyOnly) {
                        const auto matches = std::count_if(stagedStrings_.begin(), stagedStrings_.end(), [&](const auto& item) {
                            return std::get<0>(item.first) == loaded.name && support::sameLocalizedPath(item.second.file.path, file.path);
                        });
                        if (matches != 1) throw std::runtime_error("staged localized table output is missing or ambiguous");
                        baseline.verifyOnly = true;
                        baseline.finalBytes.clear();
                    }
                    add(baseline);
                }
            }
            for (const auto& [key, value] : stagedStrings_)
                if (std::get<0>(key) == loaded.name && (!stringsOnly || std::get<1>(key) == *stringsOnly)) add(value.file);
        }
        for (const auto& [path, file] : sidecars) {
            auto expected = file.rootSelector;
            for (const auto& directory : file.directories) expected /= fo4::io::from_utf8(directory);
            expected /= fo4::io::from_utf8(file.filename);
            const auto extension = file.kind == "strings" ? ".STRINGS" : file.kind == "dlstrings" ? ".DLSTRINGS" :
                                   file.kind == "ilstrings" ? ".ILSTRINGS" : nullptr;
            if (!extension || expected.lexically_normal() != path || file.filename !=
                fo4::io::utf8(loaded.path.stem()) + "_" + file.language + extension)
                throw std::runtime_error("staged string table identity does not match its destination");
            JournalInput input{file.kind, file.language, file.originalExists, file.originalHash,
                               file.originalSize, file.verifyOnly ? std::vector<std::byte>{} : file.finalBytes};
            input.rootSelector = file.rootSelector; input.directories = file.directories; input.verifyOnly = file.verifyOnly;
            inputs.push_back(std::move(input));
        }
        if (!stringsOnly && loaded.stagedQuestFragments) {
            const auto& fragment = *loaded.stagedQuestFragments;
            const auto script = fo4::io::utf8(fragment.sourcePath.stem());
            if (fragment.pexPath.parent_path() != fragment.sourcePath.parent_path() ||
                fragment.pexPath.stem() != fragment.sourcePath.stem() || fragment.sourcePath.extension() != ".psc" ||
                fragment.pexPath.extension() != ".pex") throw std::runtime_error("fragment output identity mismatch");
            const JournalApprovedRoot* selected = nullptr;
            for (const auto& root : roots.approvedRoots) {
                const auto rel = fragment.sourcePath.parent_path().lexically_relative(root.path);
                if (rel.empty() && fragment.sourcePath.parent_path() != root.path) continue;
                if (!rel.empty() && *rel.begin() == "..") continue;
                if (!selected || root.path.native().size() > selected->path.native().size()) selected = &root;
            }
            if (!selected) throw std::runtime_error("fragment output root is not authorized");
            std::vector<std::string> directories;
            const auto relative = fragment.sourcePath.parent_path().lexically_relative(selected->path);
            for (const auto& component : relative) if (component != ".") directories.push_back(fo4::io::utf8(component));
            const auto addScript = [&](std::string kind, const std::optional<std::string>& original, std::vector<std::byte> final) {
                const auto originalBytes = original ? asBytes(*original) : std::vector<std::byte>{};
                JournalInput input{std::move(kind), script, original.has_value(),
                    original ? fo4::io::sha256_hex(originalBytes) : std::string{}, originalBytes.size(), std::move(final)};
                input.rootSelector = selected->path; input.directories = directories; inputs.push_back(std::move(input));
            };
            addScript("psc", fragment.originalSource, asBytes(fragment.source));
            const auto compiled = std::as_bytes(std::span{fragment.pex});
            addScript("pex", fragment.originalPex, {compiled.begin(), compiled.end()});
        }
        for (auto& [location, names] : sourceNames) {
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            sourceSets.push_back({"strings", location.first, location.second, std::move(names),
                                  sourcePresence.at(location)});
        }
        session.publish(roots, inputs, sourceSets, archives, readStrings);
        std::error_code ec;
        auto writtenTime = std::filesystem::last_write_time(output, ec);
        if (timestampHook) ec = timestampHook();
        if (ec) writtenTime = std::filesystem::file_time_type::min();
        saveIo.unlock();
        CommitScope commit;
        if (stringsOnly) {
            std::scoped_lock lock(stagedStringsMutex_);
            std::erase_if(stagedStrings_, [&](const auto& item) {
                return std::get<0>(item.first) == loaded.name && std::get<1>(item.first) == *stringsOnly;
            });
            const bool remaining = std::any_of(stagedStrings_.begin(), stagedStrings_.end(), [&](const auto& item) {
                return std::get<0>(item.first) == loaded.name;
            });
            if (!remaining && loaded.stagedPluginLanguages.empty() && !loaded.stagedLocalization) loaded.stagedAllocation.reset();
            else if (rebased) loaded.stagedAllocation.swap(rebased);
            ++mutationEpoch_;
            return {};
        }
        loaded.diskBaseline = std::move(pluginBytes); loaded.loadedTime = writtenTime;
        loaded.stagedLocalization.reset(); loaded.stagedAllocation.reset(); loaded.stagedPluginLanguages.clear(); loaded.stagedPluginStringIds.clear(); loaded.stagedQuestFragments.reset();
        discardStagedStringTables(loaded.name); loaded.dirty = false;
        return {};
    } catch (const JournalRecoveryRequired& error) {
        testRecoveryPlugin_.swap(pendingName);
        return std::unexpected(Error{"JournalRecoveryRequired", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"JournalWriteFailed", error.what()});
    }
}

}
#endif
