#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <utility>

#include <fo4/io/atomic_write.hpp>
#include <fo4/plugin/writer.hpp>

#include "RadiclePluginSupport.hpp"

namespace fo4ide::plugin {

std::string RadiclePluginBackend::dirtyPluginListLocked() const {
    std::string pendingList;
    for (const auto& item : plugins_) {
        if (!item.dirty && !hasStagedStringTables(item.name)) continue;
        if (!pendingList.empty()) pendingList += ", ";
        pendingList += item.name;
    }
    return pendingList;
}


#ifdef FO4IDE_TEST_HOOKS
void RadiclePluginBackend::testSetJournalRoot(std::filesystem::path root) {
    OpLock lock(*this, OpLock::Mode::Lifecycle);
    testJournalRoot_ = std::move(root);
}
#endif

#ifdef FO4IDE_TEST_HOOKS
namespace {
WriteOpLockedHook writeOpLockedHook = nullptr;
WriteOpLockedHook saveIoLockedHook = nullptr;
}

void setWriteOpLockedHook(WriteOpLockedHook hook) {
    writeOpLockedHook = hook;
}

void setSaveIoLockedHook(WriteOpLockedHook hook) {
    saveIoLockedHook = hook;
}
#endif

namespace {

std::vector<std::byte> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("failed to read " + path.string());
    const auto size = stream.tellg();
    if (size < 0) throw std::runtime_error("failed to size " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("failed to read " + path.string());
    }
    return bytes;
}

}

bool RadiclePluginBackend::diskMatchesBaseline(const LoadedPlugin& loaded) const {
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(loaded.path, error);
        return loaded.diskBaseline && !error && size == loaded.diskBaseline->size() &&
               readFileBytes(loaded.path) == *loaded.diskBaseline;
    } catch (const std::exception&) {
        return false;
    }
}

thread_local RadiclePluginBackend::OpLock* RadiclePluginBackend::OpLock::current = nullptr;

RadiclePluginBackend::OpLock::OpLock(const RadiclePluginBackend& backend, Mode mode,
                                     const std::vector<std::string_view>& names)
    : backend(backend), mode(mode), previous(current) {
    if (mode == Mode::Lifecycle) {
        backend.lifecycleMutex_.lock();
        state = StateHold::Exclusive;
        current = this;
        return;
    }
    backend.lifecycleMutex_.lock_shared();
    try {
        if (mode == Mode::Write) {
            std::vector<std::mutex*> ordered;
            {
                std::shared_lock resolve(backend.stateMutex_);
                std::vector<const LoadedPlugin*> targets;
                for (const auto name : names) {
                    if (const auto* loaded = backend.findPlugin(name)) targets.push_back(loaded);
                }
                std::sort(targets.begin(), targets.end(),
                          [](const LoadedPlugin* a, const LoadedPlugin* b) {
                              if (a->plugin.key < b->plugin.key) return true;
                              if (b->plugin.key < a->plugin.key) return false;
                              return std::less<const LoadedPlugin*>{}(a, b);
                          });
                targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
                for (const auto* loaded : targets) ordered.push_back(loaded->writeMutex.get());
            }
            for (auto* owner : ordered) owners.emplace_back(*owner);
        }
        backend.stateMutex_.lock_shared();
    } catch (...) {
        owners.clear();
        backend.lifecycleMutex_.unlock_shared();
        throw;
    }
    state = StateHold::Shared;
    current = this;
#ifdef FO4IDE_TEST_HOOKS
    if (mode == Mode::Write && writeOpLockedHook != nullptr) writeOpLockedHook();
#endif
}

RadiclePluginBackend::OpLock::~OpLock() {
    current = previous;
    if (mode == Mode::Lifecycle) {
        backend.lifecycleMutex_.unlock();
        return;
    }
    if (state == StateHold::Shared) backend.stateMutex_.unlock_shared();
    if (state == StateHold::Exclusive) backend.stateMutex_.unlock();
    owners.clear();
    backend.lifecycleMutex_.unlock_shared();
}

RadiclePluginBackend::CommitScope::CommitScope()
    : op_(OpLock::current),
      restore_(op_ != nullptr ? op_->state : StateHold::None) {
    if (op_ == nullptr || restore_ == StateHold::Exclusive) return;
    if (restore_ == StateHold::Shared) op_->backend.stateMutex_.unlock_shared();
    op_->backend.stateMutex_.lock();
    op_->state = StateHold::Exclusive;
}

RadiclePluginBackend::CommitScope::~CommitScope() {
    if (op_ == nullptr || restore_ == StateHold::Exclusive) return;
    op_->backend.stateMutex_.unlock();
    if (restore_ == StateHold::Shared) op_->backend.stateMutex_.lock_shared();
    op_->state = restore_;
}

RadiclePluginBackend::StateReleased::StateReleased()
    : op_(OpLock::current),
      restore_(op_ != nullptr ? op_->state : StateHold::None) {
    if (op_ == nullptr || restore_ != StateHold::Shared) return;
    op_->backend.stateMutex_.unlock_shared();
    op_->state = StateHold::None;
}

RadiclePluginBackend::StateReleased::~StateReleased() {
    if (op_ == nullptr || restore_ != StateHold::Shared) return;
    op_->backend.stateMutex_.lock_shared();
    op_->state = StateHold::Shared;
}

void RadiclePluginBackend::stagePluginMutationLocked(LoadedPlugin& loaded,
                                                     fo4::Plugin mutated) {
    CommitScope commit;
    loaded.plugin = std::move(mutated);
    dropBorrowedEnvironmentLocked();
    loaded.dirty = true;
    ++mutationEpoch_;
    markGraphDirtyLocked();
}

void RadiclePluginBackend::stageLocalizationMutationLocked(
    LoadedPlugin& loaded, StagedLocalization staged) {
    CommitScope commit;
    loaded.stagedLocalization = std::move(staged);
    loaded.dirty = true;
    ++mutationEpoch_;
}

bool RadiclePluginBackend::stageQuestFragmentMutationLocked(
    LoadedPlugin& loaded, fo4::Plugin mutated, StagedQuestFragments fragments) {
    CommitScope commit;
    for (const auto& other : plugins_) {
        if (&other == &loaded || !other.stagedQuestFragments) continue;
        const auto& pending = *other.stagedQuestFragments;
        if (support::asciiLower(pending.sourcePath.generic_string()) ==
                support::asciiLower(fragments.sourcePath.generic_string()) ||
            support::asciiLower(pending.pexPath.generic_string()) ==
                support::asciiLower(fragments.pexPath.generic_string())) return false;
    }
    loaded.plugin = std::move(mutated);
    dropBorrowedEnvironmentLocked();
    loaded.stagedQuestFragments = std::move(fragments);
    loaded.dirty = true;
    ++mutationEpoch_;
    markGraphDirtyLocked();
    return true;
}

Result<void> RadiclePluginBackend::persistPluginLocked(
    LoadedPlugin& loaded, const std::filesystem::path& output) {
    if (!testJournalRoot_.empty() && (loaded.stagedQuestFragments || loaded.stagedLocalization || hasStagedStringTables(loaded.name)))
        return persistJournalLocked(loaded, output);
    if (loaded.stagedQuestFragments.has_value()) {
        return std::unexpected(Error{"QuestFragmentSaveUnavailable",
            "Quest fragments cannot be saved until recoverable script and plugin publication is available; discard the pending edit."});
    }
    if (loaded.stagedLocalization.has_value() || hasStagedStringTables(loaded.name)) {
        return std::unexpected(Error{"LocalizedSaveUnavailable", "Pending string tables require recoverable publication."});
    }
    try {
        StateReleased released;
        std::unique_lock saveIo(saveIoMutex_);
#ifdef FO4IDE_TEST_HOOKS
        if (saveIoLockedHook != nullptr) saveIoLockedHook();
#endif
        if (!diskMatchesBaseline(loaded)) {
            return std::unexpected(Error{"PluginChangedOnDisk", "Plugin bytes changed after loading: " + loaded.name});
        }
        const auto pluginBytes = std::make_shared<const std::vector<std::byte>>(fo4::write_plugin(loaded.plugin, output));
        fo4::io::write_file_atomic(output, *pluginBytes);
        std::error_code ec;
        const auto writtenTime = std::filesystem::last_write_time(output, ec);
        saveIo.unlock();
        CommitScope commit;
        loaded.path = output;
        loaded.diskBaseline = pluginBytes;
        loaded.loadedTime = writtenTime;
        loaded.stagedLocalization.reset(); loaded.stagedAllocation.reset(); loaded.stagedPluginLanguages.clear(); loaded.stagedPluginStringIds.clear();
        loaded.dirty = false;
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginWriteFailed", error.what()});
    }
}

}
