#include "RadiclePluginSupport.hpp"
#include "RadiclePluginJournalSession.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/io/path_text.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {
using namespace support;

#ifdef FO4IDE_TEST_HOOKS
namespace {
LoadMo2AfterPendingCheckHook loadMo2AfterPendingCheckHook = nullptr;
LoadReadWindowHook loadReadWindowHook = nullptr;
}

void setLoadMo2AfterPendingCheckHook(LoadMo2AfterPendingCheckHook hook) {
    loadMo2AfterPendingCheckHook = hook;
}

void setLoadReadWindowHook(LoadReadWindowHook hook) {
    loadReadWindowHook = hook;
}

#endif

Result<void> requirePluginUnchangedSince(const std::filesystem::path& path,
                                         std::filesystem::file_time_type loadedTime) {
    std::error_code ec;
    const auto current = std::filesystem::last_write_time(path, ec);
    if (ec || current != loadedTime) {
        return std::unexpected(Error{
            "PluginChangedOnDisk",
            path.string() + " changed while it was being loaded; try again"
        });
    }
    return {};
}

namespace {

std::filesystem::path utf8Path(std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const unsigned char ch : value) {
        utf8.push_back(static_cast<char8_t>(ch));
    }
    return std::filesystem::path(utf8);
}

std::pair<std::string, std::string> splitTreePath(std::string_view path) {
    const auto slash = path.find_first_of("\\/");
    if (slash == std::string_view::npos) return {std::string(path), {}};
    const auto second = path.find_first_of("\\/", slash + 1);
    if (second == std::string_view::npos) {
        return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1))};
    }
    return {std::string(path.substr(0, slash)), std::string(path.substr(slash + 1, second - slash - 1))};
}

const char* malformedEnvironmentField(const nlohmann::json& entry) {
    for (const char* key : std::array<const char*, 3>{"name", "path", "source"}) {
        const auto field = entry.find(key);
        if (field != entry.end() && !field->is_string()) return key;
    }
    const auto resolved = entry.find("resolved");
    if (resolved != entry.end() && !resolved->is_boolean()) return "resolved";
    return nullptr;
}

}

Capabilities RadiclePluginBackend::capabilities() const noexcept {
    return {
        .available = true,
        .readPlugins = true,
        .writePlugins = true,
        .resolveReferences = true,
    };
}

bool RadiclePluginBackend::fileUnchangedSinceLoad(const LoadedPlugin& loaded) const {
    std::error_code ec;
    const auto current = std::filesystem::last_write_time(loaded.path, ec);
    return !ec && current == loaded.loadedTime;
}

std::optional<Error> RadiclePluginBackend::writeGuard(const LoadedPlugin& loaded) const {
    if (!recoveryPlugin_.empty() && fo4::PluginKey::from_filename(recoveryPlugin_) == loaded.plugin.key)
        return Error{"JournalRecoveryRequired", "Plugin journal recovery is required"};
    if (!isEditable(loaded)) {
        return Error{
            "PluginReadOnly",
            "Refusing to write protected or game-Data plugin: " + loaded.name
        };
    }
    if (!fileUnchangedSinceLoad(loaded)) {
        return Error{
            "PluginChangedOnDisk",
            "Plugin was modified on disk after it was loaded; reload it before writing: " +
                loaded.path.string()
        };
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> RadiclePluginBackend::allowedSavePath(
    const std::filesystem::path& path) const {
    // Writable roots are the immutable snapshot captured from the trusted
    // environment.LoadMo2 plugin list. They are never derived from mutable
    // LoadedPlugin.path entries: an ad hoc OpenPlugin cannot relocate a root
    // by retargeting a same-named trusted plugin to another directory.
    std::error_code targetEc;
    const auto target = std::filesystem::weakly_canonical(path, targetEc);
    if (targetEc || target.empty()) return std::nullopt;  // fail closed

    // The canonical destination is the path that will actually be written.
    // Validate its extension too: a requested Foo.esp symlink must not be
    // allowed to resolve to notes.txt and bypass the plugin-only write rule.
    const auto canonicalExtension = asciiLower(target.extension().string());
    if (canonicalExtension != ".esp" && canonicalExtension != ".esm" &&
        canonicalExtension != ".esl") {
        return std::nullopt;
    }

    // Save As must never target the game's Data directory, even if a bad
    // environment snapshot once included a protected plugin from there.
    if (!dataDirectory_.empty()) {
        std::error_code dataEc;
        const auto dataDir = std::filesystem::weakly_canonical(dataDirectory_, dataEc);
        if (dataEc || dataDir.empty()) return std::nullopt;
        const auto dataEnd = std::mismatch(
            dataDir.begin(), dataDir.end(), target.begin(), target.end()).first;
        if (dataEnd == dataDir.end()) return std::nullopt;
    }

    for (const auto& root : trustedSaveRoots_) {
        // Each root was canonicalized exactly once during LoadMo2. Repeating
        // that work here would follow a symlink swapped in after the trusted
        // snapshot and silently relocate its Save-As authority.
        const auto rootEnd =
            std::mismatch(root.begin(), root.end(), target.begin(), target.end()).first;
        if (rootEnd == root.end()) {
            return target;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> RadiclePluginBackend::allowedLocalizationDirectory(
    const std::filesystem::path& path) const {
    std::error_code targetEc;
    const auto target = std::filesystem::weakly_canonical(path, targetEc);
    if (targetEc || target.empty() || !std::filesystem::is_directory(target)) {
        return std::nullopt;
    }

    if (!dataDirectory_.empty()) {
        std::error_code dataEc;
        const auto dataDir = std::filesystem::weakly_canonical(dataDirectory_, dataEc);
        if (dataEc || dataDir.empty()) return std::nullopt;
        const auto dataEnd = std::mismatch(
            dataDir.begin(), dataDir.end(), target.begin(), target.end()).first;
        if (dataEnd == dataDir.end()) return std::nullopt;
    }

    for (const auto& root : trustedSaveRoots_) {
        const auto rootEnd =
            std::mismatch(root.begin(), root.end(), target.begin(), target.end()).first;
        if (rootEnd == root.end()) return target;
    }
    return std::nullopt;
}

Result<nlohmann::json> RadiclePluginBackend::invoke(std::string_view operation,
                                                     const nlohmann::json& arguments) {
    if (const auto error = testRecoveryGate(operation, arguments)) return std::unexpected(*error);
    if (operation == "environment.LoadMo2") return loadEnvironment(arguments);
    if (operation == "strings.ListStringTables") return listStringTables(arguments);
    if (operation == "strings.GetStrings") return getStrings(arguments);
    if (operation == "strings.LookupString") return lookupString(arguments);
    if (operation == "strings.SetString") return setString(arguments);
    if (operation == "strings.AddString") return addString(arguments);
    if (operation == "strings.RemoveString") return removeString(arguments);
    if (operation == "strings.ImportStrings") return importStrings(arguments);
    if (operation == "strings.ExportStrings") return exportStrings(arguments);
    if (operation == "strings.SaveStringTables") return saveStringTables(arguments);
    if (operation == "strings.DiscardStringEdits") return discardStringEdits(arguments);
    if (operation == "appInterop.GetPlugins") return getPlugins();
    if (operation == "appInterop.GetChildren") return getChildren(arguments);
    if (operation == "appInterop.OpenRecord") return openRecord(arguments);
    if (operation == "appInterop.ScanConflicts") return scanConflicts();
    if (operation == "appInterop.ScanBrokenRefs") return scanBrokenRefs();
    if (operation == "backend.OpenPlugin") return openPlugin(arguments);
    if (operation == "backend.SavePlugin") return savePlugin(arguments);
    if (operation == "backend.RepairHedrCount") return repairHedrCount(arguments);
    if (operation == "backend.DiscardPlugin") return discardPlugin(arguments);
    // Scalar-schema aware on every entry path; setFieldValue falls back to
    // setField for non-schema fields.
    if (operation == "backend.SetField") return setFieldValue(arguments);
    if (operation == "backend.CopyAsOverride") return copyAsOverride(arguments);
    if (operation == "backend.CopyAsOverrideMany") return copyAsOverrideMany(arguments);
    if (operation == "backend.AddMasters") return addMasters(arguments);
    if (operation == "backend.CheckEslEligibility") return checkEslEligibility(arguments);
    if (operation == "backend.GetPluginRecordTypes") return getPluginRecordTypes(arguments);
    if (operation == "backend.GetRecordsOfType") return getRecordsOfType(arguments);
    if (operation == "backend.GetRecordsGrid") return getRecordsGrid(arguments);
    if (operation == "backend.GetRecordDetails") return getRecordDetails(arguments);
    if (operation == "backend.GetRecordPluginMatrix") return getRecordPluginMatrix(arguments);
    if (operation == "backend.GetContainmentPath") return getContainmentPath(arguments);
    if (operation == "backend.GetEditablePlugins") return getEditablePlugins();
    if (operation == "backend.GetActivePlugins") return getActivePlugins();
    if (operation == "backend.GetRecordTypeIndex") return getRecordTypeIndex();
    if (operation == "backend.GetLoadOrderSummary") return getLoadOrderSummary();
    if (operation == "backend.GetConflicts") return getConflicts();
    if (operation == "backend.GetConflictMatrix") return getConflictMatrix(arguments);
    if (operation == "backend.GetReferencedBy") return getReferencedBy(arguments);
    if (operation == "backend.SearchRecords") return searchRecords(arguments);
    if (operation == "backend.GetProblems") return getProblems(arguments);
    if (operation == "backend.GetDependencies") return getDependencies(arguments);
    if (operation == "backend.GetHistory") return getHistory(arguments);
    if (operation == "backend.GetPluginWorklist") return getPluginWorklist(arguments);
    if (operation == "masters.GetPlugins") return getEditablePlugins();
    if (operation == "masters.List") return listMasters(arguments);
    if (operation == "masters.Reorder") return reorderMasters(arguments);
    if (operation == "masters.SetLight") return setLight(arguments);
    if (operation == "masters.SavePlugin") return saveMastersPlugin(arguments);
    if (operation == "backend.GetConditions") return getConditions(arguments);
    if (operation == "backend.SetConditions") return setConditions(arguments);
    if (operation == "backend.GetConditionsAtPath") return getConditionsAtPath(arguments);
    if (operation == "backend.SetConditionsAtPath") return setConditionsAtPath(arguments);
    if (operation == "backend.GetConditionFunctions") return getConditionFunctions();
    if (operation == "backend.GetConditionRunOnTypes") return getConditionRunOnTypes();

    return std::unexpected(Error{
        "RadicleOperationNotImplemented",
        "Radicle backend operation is not implemented yet: " + std::string(operation)
    });
}

Result<nlohmann::json> RadiclePluginBackend::loadEnvironment(const nlohmann::json& arguments) {
    if (!arguments.is_object() || !arguments.contains("plugins") || !arguments["plugins"].is_array()) {
        return std::unexpected(Error{"InvalidEnvironment", "MO2 environment is missing its plugin list"});
    }
    for (const auto& entry : arguments["plugins"]) {
        if (!entry.is_object()) continue;
        if (const char* field = malformedEnvironmentField(entry)) {
            return std::unexpected(Error{
                "InvalidEnvironment",
                std::string("MO2 environment plugin field has the wrong JSON type: ") + field
            });
        }
    }
    const bool discardPending = arguments.contains("discardPending") &&
                                arguments["discardPending"].is_boolean() &&
                                arguments["discardPending"].get<bool>();
    std::uint64_t consentEpoch = 0;
    {
        OpLock lock(*this, OpLock::Mode::Read);
        consentEpoch = mutationEpoch_;
        if (!discardPending) {
            const auto pendingList = dirtyPluginListLocked();
            if (!pendingList.empty()) {
                return std::unexpected(Error{
                    "PluginHasPendingChanges",
                    "Pending plugin edits would be discarded by reloading the MO2 profile; "
                    "save or discard them first: " + pendingList
                });
            }
        }
    }
#ifdef FO4IDE_TEST_HOOKS
    if (loadMo2AfterPendingCheckHook) loadMo2AfterPendingCheckHook();
#endif
    std::unique_ptr<OpLock> recoveryState;
    std::unique_ptr<detail::JournalSession> recoverySession;
    std::filesystem::path configuredJournalRoot;
    nlohmann::json warnings = nlohmann::json::array();
    bool recoveryFailed = false;
    const auto gateRecovery = [&](const std::string& owner) {
        if (!recoveryFailed) recoveryPlugin_ = owner;
        recoveryFailed = true;
    };
#ifdef FO4IDE_TEST_HOOKS
    TestReadRoots candidateReadRoots;
#endif
    try {
        { OpLock lock(*this, OpLock::Mode::Read); configuredJournalRoot = testJournalRoot_; }
        if (!configuredJournalRoot.empty()) {
            recoveryState = std::make_unique<OpLock>(*this, OpLock::Mode::Lifecycle);
            if (testJournalRoot_ != configuredJournalRoot) throw std::runtime_error("journal root changed");
            if (!detail::journalHasRecoveryFiles(configuredJournalRoot)) recoveryState.reset();
        }
#ifdef FO4IDE_TEST_HOOKS
        if (!configuredJournalRoot.empty()) {
            for (const auto& entry : arguments["plugins"]) {
                if (!entry.is_object()) continue;
                const auto name = entry.value("name", std::string{});
                try {
                    testAuthorizeProfileReadRoots(nlohmann::json{{"plugins", nlohmann::json::array({entry})}}, candidateReadRoots);
                } catch (const std::exception& error) {
                    if (recoveryState) gateRecovery(name);
                    warnings.push_back(nlohmann::json{{"plugin", name}, {"warning", error.what()}});
                }
            }
        }
#endif
        if (recoveryState) {
            const auto pending = dirtyPluginListLocked();
            if (mutationEpoch_ != consentEpoch || (!discardPending && !pending.empty()))
                return std::unexpected(Error{"PluginHasPendingChanges", "Pending changes prevent journal recovery: " + pending});
            recoverySession = std::make_unique<detail::JournalSession>(configuredJournalRoot);
            std::vector<detail::JournalRoots> approved;
            std::vector<detail::JournalApprovedRoot> directories;
            for (const auto& entry : arguments["plugins"]) {
                if (!entry.is_object() || !entry.value("resolved", false)) continue;
                try {
                    LoadedPlugin candidate;
                    candidate.name = entry.value("name", std::string{});
                    candidate.source = entry.value("source", std::string{});
                    const auto path = entry.value("path", std::string{});
                    if (candidate.name.empty() || path.empty()) continue;
                    candidate.path = utf8Path(path);
                    if (!isEditable(candidate)) continue;
                    std::error_code ec;
                    const auto canonical = std::filesystem::absolute(candidate.path, ec).lexically_normal();
                    if (ec || canonical.empty() || asciiLower(canonical.filename().string()) != asciiLower(candidate.name))
                        throw std::runtime_error("cannot authorize journal plugin identity");
                    const auto status = std::filesystem::symlink_status(canonical, ec);
                    if (ec || !std::filesystem::is_regular_file(status)) throw std::runtime_error("unsafe journal plugin");
                    auto identity = detail::JournalDestination::captureRoot(canonical.parent_path());
                    const auto root = canonical.parent_path();
                    if (std::none_of(directories.begin(), directories.end(), [&](const auto& item) { return item.path == root; }))
                        directories.push_back({root, identity});
                    detail::JournalRoots item;
                    item.plugin = canonical; item.pluginDirectoryIdentity = std::move(identity);
                    approved.push_back(std::move(item));
                } catch (const std::exception& error) {
                    gateRecovery(entry.value("name", std::string{}));
                    warnings.push_back(nlohmann::json{{"plugin", entry.value("name", std::string{})}, {"warning", error.what()}});
                }
            }
            for (auto& item : approved) item.approvedRoots = directories;
            bool recovered = false;
            for (const auto& item : approved) {
                const std::vector<detail::JournalRoots> owner{item};
                const auto name = fo4::io::utf8(item.plugin.filename());
                try {
#ifdef FO4IDE_TEST_HOOKS
                    if (testRecoverJournal(*recoverySession, owner, arguments, candidateReadRoots)) recovered = true;
#else
                    if (recoverySession->recover(owner)) recovered = true;
#endif
                } catch (const std::exception& error) {
                    gateRecovery(name);
                    warnings.push_back(nlohmann::json{{"plugin", name}, {"warning", error.what()}});
                }
            }
            if (!recovered) recoverySession.reset();
        }
    } catch (const std::exception& error) {
        recoverySession.reset();
        if (recoveryState) gateRecovery(fo4::io::utf8(configuredJournalRoot));
        warnings.push_back(nlohmann::json{{"warning", error.what()}});
    }
    std::vector<LoadedPlugin> loaded;
    nlohmann::json errors = nlohmann::json::array();
    for (const auto& entry : arguments["plugins"]) {
        if (!entry.is_object() || !entry.value("resolved", false)) continue;
        const auto name = entry.value("name", std::string{});
        const auto path = entry.value("path", std::string{});
        if (name.empty() || path.empty()) continue;

        try {
            const auto filePath = utf8Path(path);
            std::error_code statEc;
            const auto readTime = std::filesystem::last_write_time(filePath, statEc);
            auto plugin = fo4::read_plugin_file(filePath);
#ifdef FO4IDE_TEST_HOOKS
            if (loadReadWindowHook != nullptr) loadReadWindowHook();
#endif
            if (const auto stable = requirePluginUnchangedSince(filePath, readTime); !stable) {
                errors.push_back({{"plugin", name}, {"path", path}, {"error", stable.error().message}});
                continue;
            }
            loaded.push_back({
                .name = name,
                .path = filePath,
                .source = entry.value("source", std::string{}),
                .loadedTime = readTime,
                .trusted = true,
                .plugin = std::move(plugin),
                .dirty = false,
                .stagedLocalization = std::nullopt,
            });
        } catch (const std::exception& error) {
            errors.push_back({{"plugin", name}, {"path", path}, {"error", error.what()}});
        }
    }

    recoveryState.reset();
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
    if (!configuredJournalRoot.empty() && !errors.empty()) return std::unexpected(Error{"JournalRecoveryProfileInvalid", "a profile plugin could not be loaded: " + errors.front().at("plugin").get<std::string>()});
    try { if (!configuredJournalRoot.empty()) candidateReadRoots = testFilterReadRoots(candidateReadRoots, loaded); }
    catch (const std::exception& error) { return std::unexpected(Error{"JournalRecoveryProfileInvalid", error.what()}); }
#endif
    std::vector<fo4::MasterDescriptor> knownKinds;
    knownKinds.reserve(loaded.size());
    for (const auto& item : loaded) {
        knownKinds.push_back({
            item.plugin.key,
            fo4::classify_plugin_kind(item.plugin.header.tes4_flags, item.plugin.key),
        });
    }
    for (auto& item : loaded) {
        fo4::apply_master_kinds(item.plugin, knownKinds);
    }
    const auto loadedCount = loaded.size();
    {
        OpLock lock(*this, OpLock::Mode::Lifecycle);
        // Capture the Save-As roots from this trusted snapshot once. They
        // stay fixed until the next LoadMo2; OpenPlugin can never add to or
        // move them.
        std::set<std::filesystem::path> uniqueRoots;
        std::vector<std::filesystem::path> roots;
        for (const auto& item : loaded) {
            // Protected Data/official plugins are readable environment
            // entries, never authorities for Save As destinations.
            if (!item.trusted || !isEditable(item)) continue;
            std::error_code rootEc;
            const auto root =
                std::filesystem::weakly_canonical(item.path.parent_path(), rootEc);
            if (rootEc || root.empty()) continue;  // fail closed: no root
            if (uniqueRoots.insert(root).second) roots.push_back(root);
        }
        const bool consentStale = mutationEpoch_ != consentEpoch;
        const auto pendingList = dirtyPluginListLocked();
        if (consentStale || (!discardPending && !pendingList.empty())) {
            return std::unexpected(Error{
                "PluginHasPendingChanges",
                pendingList.empty()
                    ? "A plugin was edited after reload consent; save or discard those edits first"
                    : "Pending plugin edits would be discarded by reloading the MO2 profile; "
                      "save or discard them first: " + pendingList
            });
        }
        plugins_ = std::move(loaded);
        ++mutationEpoch_;
#if defined(FO4IDE_TEST_HOOKS) && defined(__linux__)
        testReadRoots_ = std::move(candidateReadRoots);
#endif
        if (!recoveryFailed) recoveryPlugin_.clear();
        {
            std::scoped_lock staged(stagedStringsMutex_);
            stagedStrings_.clear();
        }
        trustedSaveRoots_ = std::move(roots);
        markGraphDirtyLocked();
    }
    return nlohmann::json{
        {"ok", true},
        {"loadedPlugins", loadedCount},
        {"failedPlugins", errors.size()},
        {"errors", std::move(errors)},
        {"warnings", std::move(warnings)}
    };
}

Result<nlohmann::json> RadiclePluginBackend::getChildren(const nlohmann::json& arguments) const {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return nlohmann::json("[]");
    const auto [pluginName, signature] = splitTreePath(path);

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(pluginName);
    // An unknown plugin must be an error, not an empty tree: the two are
    // indistinguishable for the client otherwise.
    if (!loaded) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }

    nlohmann::json rows = nlohmann::json::array();
    const auto records = fo4::collect_records(loaded->plugin);

    if (signature.empty()) {
        std::map<std::string, std::size_t> counts;
        for (const auto* record : records) {
            if (record->header.signature == fo4::sig_tes4) continue;
            ++counts[record->header.signature.to_string()];
        }
        for (const auto& [sig, count] : counts) {
            rows.push_back({
                {"Key", sig + " (" + std::to_string(count) + ")"},
                {"ConflictStatus", 0},
                {"IsRecordNode", false},
                {"HasChildren", count != 0},
                {"FilePath", loaded->path.string()}
            });
        }
        return nlohmann::json(rows.dump());
    }

    const auto space = signature.find(' ');
    const auto normalizedSignature = signature.substr(0, space);
    for (const auto* record : records) {
        if (record->header.signature.to_string() != normalizedSignature) continue;
        if (record->form_key.owner().empty()) continue;
        rows.push_back({
            {"Key", record->form_key.to_string()},
            {"ConflictStatus", 0},
            {"IsRecordNode", true},
            {"HasChildren", false},
            {"FilePath", loaded->path.string()}
        });
    }
    return nlohmann::json(rows.dump());
}

Result<nlohmann::json> RadiclePluginBackend::openRecord(const nlohmann::json& arguments) const {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return nlohmann::json("");
    const auto last = path.find_last_of("\\/");
    if (last == std::string::npos || last + 1 >= path.size()) return nlohmann::json("");
    return nlohmann::json(path.substr(last + 1));
}

Result<nlohmann::json> RadiclePluginBackend::openPlugin(const nlohmann::json& arguments) {
    const auto path = stringArgument(arguments, 0);
    if (path.empty()) return std::unexpected(Error{"PluginPathMissing", "Plugin path is empty"});

    static constexpr std::array<std::string_view, 3> k_pluginExtensions{".esp", ".esm", ".esl"};
    const auto lowerPath = asciiLower(path);
    const bool knownExtension =
        std::any_of(k_pluginExtensions.begin(), k_pluginExtensions.end(),
                    [&](const std::string_view extension) {
                        return lowerPath.ends_with(extension);
                    });
    if (!knownExtension) {
        return std::unexpected(Error{
            "InvalidPluginExtension",
            "Only .esp, .esm, and .esl files can be opened: " + path
        });
    }

    try {
        const auto filePath = utf8Path(path);
        std::error_code statEc;
        const auto readTime = std::filesystem::last_write_time(filePath, statEc);
        auto plugin = fo4::read_plugin_file(filePath);
#ifdef FO4IDE_TEST_HOOKS
        if (loadReadWindowHook != nullptr) loadReadWindowHook();
#endif
        if (const auto stable = requirePluginUnchangedSince(filePath, readTime); !stable) {
            return std::unexpected(stable.error());
        }
        std::string name = plugin.key.filename().empty()
            ? filePath.filename().string()
            : std::string(plugin.key.filename());

        // Reject a collision with an already-loaded plugin key at a
        // different location. Allowing it would retarget the existing entry
        // (including a trusted one) to this file's directory, which must not
        // influence save-root decisions or silently swap record data.
        std::error_code newEc;
        const auto newCanonical = std::filesystem::weakly_canonical(filePath, newEc);
        OpLock lock(*this, OpLock::Mode::Lifecycle);
        auto* existing = findPlugin(name);
        if (existing != nullptr) {
            std::error_code existingEc;
            const auto existingCanonical =
                std::filesystem::weakly_canonical(existing->path, existingEc);
            if (newEc || existingEc || newCanonical.empty() ||
                existingCanonical.empty() || newCanonical != existingCanonical) {
                return std::unexpected(Error{
                    "PluginAlreadyLoaded",
                    "A plugin with this name/key is already loaded from another "
                    "location; close it first: " +
                        (existing == nullptr ? name : existing->path.string())
                });
            }
            if (existing->dirty || hasStagedStringTables(existing->name)) {
                return std::unexpected(Error{
                    "PluginHasPendingChanges",
                    "Plugin has pending changes; save or discard them before reopening: " + name
                });
            }
        }

        if (existing != nullptr) {
            // Same canonical path: refresh the cached contents in place.
            // Trust and save roots are untouched by ad hoc loads.
            existing->loadedTime = readTime;
            existing->plugin = std::move(plugin); existing->diskBaseline = existing->plugin.original_data;
            existing->stagedLocalization.reset(); existing->stagedAllocation.reset(); existing->stagedPluginLanguages.clear(); existing->stagedPluginStringIds.clear();
            existing->stagedQuestFragments.reset();
            existing->dirty = false;
        } else {
            plugins_.push_back({
                name, filePath, {}, readTime, false, std::move(plugin), false, std::nullopt
            });
        }

        std::vector<fo4::MasterDescriptor> knownKinds;
        knownKinds.reserve(plugins_.size());
        for (const auto& item : plugins_) {
            knownKinds.push_back({
                item.plugin.key,
                fo4::classify_plugin_kind(item.plugin.header.tes4_flags, item.plugin.key),
            });
        }
        for (auto& item : plugins_) fo4::apply_master_kinds(item.plugin, knownKinds);
        markGraphDirtyLocked();
        return nlohmann::json{{"ok", true}, {"plugin", name}}.dump();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::savePlugin(const nlohmann::json& arguments) {
    const auto name = stringArgument(arguments, 0);
    const auto requestedPath = stringArgument(arguments, 1);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    bool overwrite = false;
    if (arguments.is_array() && arguments.size() > 2 && !arguments[2].is_null()) {
        if (!arguments[2].is_boolean()) {
            return std::unexpected(Error{"InvalidOverwriteFlag", "Overwrite flag must be boolean"});
        }
        overwrite = arguments[2].get<bool>();
    }

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    // Guards live here, not only in the RPC shim: every dispatch path into
    // savePlugin gets the same protection.
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    auto output = requestedPath.empty() ? loaded->path : utf8Path(requestedPath);
    // "Save As" may only produce a plugin file, and only inside a directory
    // hosting a trusted editable plugin of the active environment; otherwise
    // this endpoint would be an arbitrary-file overwrite primitive. In-place
    // saves (no requestedPath) target the plugin's own existing path and do
    // not consult the root set.
    if (!requestedPath.empty()) {
        static constexpr std::array<std::string_view, 3> k_pluginExtensions{".esp", ".esm", ".esl"};
        const auto lowerOutput = asciiLower(output.string());
        const bool pluginExtension =
            std::any_of(k_pluginExtensions.begin(), k_pluginExtensions.end(),
                        [&](const std::string_view extension) {
                            return lowerOutput.ends_with(extension);
                        });
        const auto allowedOutput = pluginExtension ? allowedSavePath(output)
                                                   : std::nullopt;
        if (!allowedOutput) {
            return std::unexpected(Error{
                "SavePathNotAllowed",
                "Refusing to save: target must be a .esp/.esm/.esl inside a directory "
                "hosting a trusted, editable loaded plugin: " +
                output.string()
            });
        }
        output = *allowedOutput;
        std::error_code loadedEc;
        const auto loadedCanonical = std::filesystem::weakly_canonical(loaded->path, loadedEc);
        const bool sameFile = !loadedEc && loadedCanonical == output;
        std::error_code existsEc;
        const bool outputExists = std::filesystem::exists(output, existsEc) || existsEc;
        if (!overwrite && !sameFile && outputExists) {
            return std::unexpected(Error{
                "SaveTargetExists",
                "Refusing to replace an existing plugin without overwrite: " + output.string()
            });
        }
    }
    if (const auto persisted = persistPluginLocked(*loaded, output); !persisted) {
        return std::unexpected(persisted.error());
    }
    return nlohmann::json{{"ok", true}, {"plugin", name}, {"path", loaded->path.string()}}.dump();
}

Result<nlohmann::json> RadiclePluginBackend::discardPlugin(const nlohmann::json& arguments) {
    const auto name = stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (!loaded) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    if (!loaded->dirty) {
        if (hasStagedStringTables(loaded->name)) {
            CommitScope commit;
            discardStagedStringTables(loaded->name);
        }
        return nlohmann::json{{"ok", true}, {"plugin", name}, {"pending", false}}.dump();
    }
    if (const auto error = writeGuard(*loaded)) return std::unexpected(*error);
    if (!diskMatchesBaseline(*loaded)) return std::unexpected(Error{"PluginChangedOnDisk", "Plugin bytes changed after loading: " + loaded->name});
    try {
        std::error_code statEc;
        const auto readTime = std::filesystem::last_write_time(loaded->path, statEc);
        auto reloaded = fo4::read_plugin_file(loaded->path);
        if (!reloaded.original_data || *reloaded.original_data != *loaded->diskBaseline)
            return std::unexpected(Error{"PluginChangedOnDisk", "Plugin bytes changed while reloading: " + loaded->name});
#ifdef FO4IDE_TEST_HOOKS
        if (loadReadWindowHook != nullptr) loadReadWindowHook();
#endif
        if (const auto stable = requirePluginUnchangedSince(loaded->path, readTime); !stable) {
            return std::unexpected(stable.error());
        }
        std::vector<fo4::MasterDescriptor> knownKinds;
        knownKinds.reserve(plugins_.size());
        for (const auto& item : plugins_) {
            knownKinds.push_back({
                item.plugin.key,
                fo4::classify_plugin_kind(item.plugin.header.tes4_flags, item.plugin.key),
            });
        }
        fo4::apply_master_kinds(reloaded, knownKinds);
        CommitScope commit;
        loaded->plugin = std::move(reloaded); loaded->diskBaseline = loaded->plugin.original_data;
        loaded->loadedTime = readTime;
        loaded->stagedLocalization.reset(); loaded->stagedAllocation.reset(); loaded->stagedPluginLanguages.clear(); loaded->stagedPluginStringIds.clear();
        loaded->stagedQuestFragments.reset();
        discardStagedStringTables(loaded->name);
        loaded->dirty = false;
        markGraphDirtyLocked();
        return nlohmann::json{{"ok", true}, {"plugin", name}, {"pending", false}}.dump();
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginReloadFailed", error.what()});
    }
}

RadiclePluginBackend::LoadedPlugin* RadiclePluginBackend::findPlugin(std::string_view name) {
    const auto key = fo4::PluginKey::from_filename(name);
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [&key, name](const LoadedPlugin& plugin) {
                                     return plugin.name == name || plugin.plugin.key == key;
                                 });
    return it == plugins_.end() ? nullptr : &*it;
}

const RadiclePluginBackend::LoadedPlugin* RadiclePluginBackend::findPlugin(std::string_view name) const {
    const auto key = fo4::PluginKey::from_filename(name);
    const auto it = std::find_if(plugins_.begin(), plugins_.end(),
                                 [&key, name](const LoadedPlugin& plugin) {
                                     return plugin.name == name || plugin.plugin.key == key;
                                 });
    return it == plugins_.end() ? nullptr : &*it;
}

void RadiclePluginBackend::markGraphDirtyLocked() const {
    std::scoped_lock lock(graphMutex_);
    graphDirty_ = true;
}

std::shared_ptr<const fo4::ConflictGraph> RadiclePluginBackend::conflictGraph() const {
    std::scoped_lock lock(graphMutex_);
    graphLastUse_ = std::chrono::steady_clock::now();
    if (!graphDirty_ && graphStorage_) return graphStorage_;
    std::vector<const fo4::Plugin*> loadOrder;
    loadOrder.reserve(plugins_.size());
    for (const auto& loaded : plugins_) loadOrder.push_back(&loaded.plugin);
    graphStorage_.reset();
    graphStorage_ = std::make_shared<const fo4::ConflictGraph>(fo4::ConflictGraph::build(loadOrder));
    graphDirty_ = false;
    return graphStorage_;
}

}
