#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <utility>

#include <fo4/io/atomic_write.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {

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

Result<void> RadiclePluginBackend::persistPluginLocked(
    LoadedPlugin& loaded, const std::filesystem::path& output) {
    try {
        StateReleased released;
        std::unique_lock saveIo(saveIoMutex_);
#ifdef FO4IDE_TEST_HOOKS
        if (saveIoLockedHook != nullptr) saveIoLockedHook();
#endif
        if (loaded.stagedLocalization.has_value()) {
            if (output != loaded.path) {
                return std::unexpected(Error{
                    "SavePathNotAllowed",
                    "Refusing Save As while localization sidecars are pending; "
                    "save in place or discard first: " + loaded.name
                });
            }
            const auto& staged = *loaded.stagedLocalization;
            if (readFileBytes(staged.stringsPath) != staged.originalStringsBytes ||
                readFileBytes(staged.dlstringsPath) != staged.originalDlstringsBytes ||
                readFileBytes(staged.ilstringsPath) != staged.originalIlstringsBytes) {
                return std::unexpected(Error{
                    "LocalizedSidecarChangedOnDisk",
                    "localized sibling table changed after it was staged; "
                    "discard or reload before saving"
                });
            }
            const auto pluginBytes = fo4::write_plugin(loaded.plugin, output);
            fo4::io::write_file_pair_dependency_first(
                staged.dlstringsPath,
                staged.dlstringsBytes,
                output,
                pluginBytes);
        } else {
            fo4::write_plugin_file(loaded.plugin, output);
        }
        std::error_code ec;
        const auto writtenTime = std::filesystem::last_write_time(output, ec);
        saveIo.unlock();
        CommitScope commit;
        loaded.path = output;
        loaded.loadedTime = writtenTime;
        loaded.stagedLocalization.reset();
        loaded.dirty = false;
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(Error{"PluginWriteFailed", error.what()});
    }
}

}
