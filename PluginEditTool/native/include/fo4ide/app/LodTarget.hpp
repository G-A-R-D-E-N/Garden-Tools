#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include <fo4ide/rpc/RpcDispatcher.hpp>

struct radicle_lod;

namespace fo4ide::app {

// Constraint: "lod" must answer Progress and CancelScoped while a bake holds mutex_.

// Invariant: only the newest previews are kept, so a path a tool returned earlier is still readable.
std::size_t rememberPreview(std::deque<std::filesystem::path>& previews, std::filesystem::path path,
                            std::size_t limit);

// Constraint: a preview is deleted only through the paths this host wrote, never a path it was handed.
void deletePreviews(std::deque<std::filesystem::path>& previews);
class LodTarget final : public rpc::Target {
public:
    LodTarget() = default;
    ~LodTarget() override;

    LodTarget(const LodTarget&) = delete;
    LodTarget& operator=(const LodTarget&) = delete;

    Result<nlohmann::json> invoke(std::string_view method, const nlohmann::json& args) override;

private:
    // Invariant: one open session per plugin content/data/archive triple, so repeated calls do not reload plugins.
    radicle_lod* session(const std::string& pluginPaths, const std::string& dataDir,
                         const std::string& archivePaths);
    void close();
    // Constraint: a poll must answer while invoke holds mutex_, so progress and cancel never take it.
    std::string progressJson() const;
    // Constraint: a run must clear the previous cancel and counters before it opens the session, so this takes no lock.
    void resetProgress();
    static void onProgress(void* user, const char* world, const char* kind, std::int32_t level, std::int32_t x,
                           std::int32_t y, std::size_t done, std::size_t total);

    std::mutex mutex_;
    radicle_lod* handle_{nullptr};
    std::string key_;
    // Invariant: the newest previews this host wrote, oldest first, so the oldest can be dropped at the limit.
    std::deque<std::filesystem::path> previewPaths_;
    // Constraint: the native writer requires a plain four-byte cancel flag at this address.
    std::int32_t cancel_flag_{0};
    std::atomic<std::size_t> progress_done_{0};
    std::atomic<std::size_t> progress_total_{0};
    std::atomic<std::int32_t> progress_level_{0};
    std::atomic<int> progress_kind_{0};
};

}  // namespace fo4ide::app
