#include <fo4ide/app/LodTarget.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fo4/c_api_lod.h>

#include "ServiceTargetSupport.hpp"

namespace fo4ide::app {
namespace {

// Invariant: the panel and an MCP client can each read a preview, so more than the newest one is kept.
constexpr std::size_t k_previewFilesKept = 8;

const char* statusText(radicle_status status) {
    switch (status) {
        case RADICLE_STATUS_OK: return "ok";
        case RADICLE_STATUS_INVALID_ARGUMENT: return "invalid argument";
        case RADICLE_STATUS_IO_ERROR: return "i/o error";
        case RADICLE_STATUS_PARSE_ERROR: return "parse error";
        case RADICLE_STATUS_NOT_FOUND: return "not found";
        case RADICLE_STATUS_OUT_OF_MEMORY: return "out of memory";
        case RADICLE_STATUS_INTERNAL_ERROR: return "internal error";
        default: break;
    }
    return "failure";
}

Result<nlohmann::json> lodFailure(radicle_status status, std::string_view what) {
    std::string message = std::string{what} + ": " + statusText(status);
    const char* detail = radicle_last_error();
    if (detail != nullptr && *detail != '\0') message += std::string{" ("} + detail + ")";
    return std::unexpected(Error{"LodFailed", std::move(message)});
}

std::string escapeJson(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(static_cast<unsigned char>(ch) >> 4) & 0xF]);
                    out.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
                } else {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

Result<nlohmann::json> takeText(std::uint8_t* bytes, std::size_t size, std::string_view what) {
    if (bytes == nullptr && size == 0) return std::string{};
    if (bytes == nullptr) return std::unexpected(Error{"LodFailed", std::string{what} + " returned no payload"});
    std::string text(reinterpret_cast<const char*>(bytes), size);
    radicle_free_bytes(bytes);
    return text;
}

std::string payloadText(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size == 0) return {};
    return std::string(reinterpret_cast<const char*>(bytes), size);
}

std::int32_t intArg(const nlohmann::json& args, std::size_t index) {
    const auto* value = support::argument(args, index);
    return value == nullptr ? 0 : static_cast<std::int32_t>(core::jsonInt(*value).value_or(0));
}

// Invariant: an edited plugin must not be answered from a session opened before the edit.
std::string fingerprint(const std::string& pluginPaths) {
    std::string out;
    std::size_t start = 0;
    while (start < pluginPaths.size()) {
        auto end = pluginPaths.find('\n', start);
        if (end == std::string::npos) end = pluginPaths.size();
        std::string path = pluginPaths.substr(start, end - start);
        start = end + 1;
        if (!path.empty() && path.back() == '\r') path.pop_back();
        if (path.empty()) continue;
        out += path;
        out += '@';
        std::error_code code;
        const auto stamp = std::filesystem::last_write_time(path, code);
        out += code ? "0" : std::to_string(stamp.time_since_epoch().count());
        out += '+';
        const auto size = std::filesystem::file_size(path, code);
        out += code ? "0" : std::to_string(size);
        out += '\n';
    }
    return out;
}

}  // namespace

std::size_t rememberPreview(std::deque<std::filesystem::path>& previews, std::filesystem::path path,
                            std::size_t limit) {
    previews.push_back(std::move(path));
    std::size_t deleted = 0;
    while (previews.size() > limit) {
        std::error_code ignored;
        std::filesystem::remove(previews.front(), ignored);
        previews.pop_front();
        ++deleted;
    }
    return deleted;
}

std::string LodTarget::progressJson() const {
    const auto kind = progress_kind_.load(std::memory_order_relaxed);
    const std::string name{kind == 1 ? "terrain" : kind == 2 ? "object" : ""};
    return "{\"done\":" + std::to_string(progress_done_.load(std::memory_order_relaxed)) + ",\"total\":" +
           std::to_string(progress_total_.load(std::memory_order_relaxed)) + ",\"kind\":\"" + name +
           "\",\"level\":" + std::to_string(progress_level_.load(std::memory_order_relaxed)) + "}";
}

void LodTarget::resetProgress() {
    std::atomic_ref<std::int32_t>(cancel_flag_).store(0, std::memory_order_relaxed);
    progress_done_.store(0, std::memory_order_relaxed);
    progress_total_.store(0, std::memory_order_relaxed);
    progress_level_.store(0, std::memory_order_relaxed);
    progress_kind_.store(0, std::memory_order_relaxed);
}

// Constraint: the callback must not block, so it copies the kind and stores atomics only.
void LodTarget::onProgress(void* user, const char*, const char* kind, std::int32_t level, std::int32_t,
                           std::int32_t, std::size_t done, std::size_t total) {
    auto* self = static_cast<LodTarget*>(user);
    const std::string_view name{kind == nullptr ? "" : kind};
    self->progress_kind_.store(name == "object" ? 2 : name == "terrain" ? 1 : 0, std::memory_order_relaxed);
    self->progress_level_.store(level, std::memory_order_relaxed);
    self->progress_done_.store(done, std::memory_order_relaxed);
    self->progress_total_.store(total, std::memory_order_relaxed);
}

void deletePreviews(std::deque<std::filesystem::path>& previews) {
    for (const auto& path : previews) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    previews.clear();
}

// Constraint: the destructor deletes the remaining previews, because only this host may remove them.
LodTarget::~LodTarget() {
    close();
    deletePreviews(previewPaths_);
}

void LodTarget::close() {
    // Constraint: a closed session keeps its previews, because a tool call still reads the paths it was given.
    if (handle_ == nullptr) return;
    radicle_lod_destroy(handle_);
    handle_ = nullptr;
    key_.clear();
}

radicle_lod* LodTarget::session(const std::string& pluginPaths, const std::string& dataDir,
                                const std::string& archivePaths) {
    const std::string wanted = fingerprint(pluginPaths) + "\n" + dataDir + "\n" + archivePaths;
    if (handle_ != nullptr && wanted == key_) return handle_;
    close();
    radicle_lod* opened = nullptr;
    const auto status =
        radicle_lod_open(pluginPaths.c_str(), dataDir.c_str(), archivePaths.c_str(), &opened);
    if (status != RADICLE_STATUS_OK || opened == nullptr) return nullptr;
    handle_ = opened;
    key_ = wanted;
    return handle_;
}

Result<nlohmann::json> LodTarget::invoke(std::string_view method, const nlohmann::json& args) {
    const std::string name(method);
    // Constraint: a bake holds mutex_ for its whole run, so progress and cancel must answer without it.
    if (name == "Progress") return progressJson();
    if (name == "CancelScoped") {
        std::atomic_ref<std::int32_t>(cancel_flag_).store(1, std::memory_order_relaxed);
        return nlohmann::json{};
    }
    // Invariant: a cancel pressed while the session opens must survive, so the reset happens before mutex_.
    if (name == "GenerateScoped") resetProgress();
    std::lock_guard<std::mutex> guard(mutex_);
    const std::string pluginPaths = support::stringArgument(args, 0);
    const std::string dataDir = support::stringArgument(args, 1);
    const std::string archivePaths = support::stringArgument(args, 2);

    if (name == "Commit") {
        const std::string ledger = support::stringArgument(args, 0);
        const std::string commit = support::stringArgument(args, 1);
        std::uint8_t* bytes = nullptr;
        std::size_t size = 0;
        const auto status = radicle_lod_commit_json(ledger.c_str(), commit.c_str(), &bytes, &size);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "committing the LOD ledger failed");
        return takeText(bytes, size, "committing the LOD ledger");
    }

    if (name != "Worlds" && name != "Plan" && name != "Preview" && name != "Candidates" &&
        name != "Generate" && name != "GenerateWorld" && name != "GenerateScoped" && name != "Verify" &&
        name != "VerifyWorld") {
        return std::unexpected(Error{"UnknownMethod", "Unknown lod RPC method: " + name});
    }

    if (session(pluginPaths, dataDir, archivePaths) == nullptr) {
        // Constraint: only a failed open is retried, so a bad plugin list cannot poison the cache.
        close();
        return std::unexpected(Error{"LodOpenFailed", "the LOD session could not be opened"});
    }

    if (name == "Worlds") {
        std::uint8_t* bytes = nullptr;
        std::size_t size = 0;
        const auto status = radicle_lod_worlds_json(handle_, &bytes, &size);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "listing LOD worlds failed");
        return takeText(bytes, size, "listing LOD worlds");
    }

    if (name == "Plan") {
        const std::string ledger = support::stringArgument(args, 3);
        std::uint8_t* plan = nullptr;
        std::size_t planSize = 0;
        std::uint8_t* kept = nullptr;
        std::size_t keptSize = 0;
        std::uint8_t* commit = nullptr;
        std::size_t commitSize = 0;
        const auto status = radicle_lod_plan_json(handle_, ledger.c_str(), &plan, &planSize, &kept, &keptSize,
                                                  &commit, &commitSize);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "planning LOD rebakes failed");
        const std::string planText = payloadText(plan, planSize);
        const std::string ledgerText = payloadText(kept, keptSize);
        const std::string commitText = payloadText(commit, commitSize);
        if (plan != nullptr) radicle_free_bytes(plan);
        if (kept != nullptr) radicle_free_bytes(kept);
        if (commit != nullptr) radicle_free_bytes(commit);
        return "{\"plan\":" + planText + ",\"ledger\":\"" + escapeJson(ledgerText) + "\",\"commit\":\"" +
               escapeJson(commitText) + "\"}";
    }

    if (name == "Preview") {
        const std::string world = support::stringArgument(args, 3);
        const auto level = intArg(args, 4);
        const auto x = intArg(args, 5);
        const auto y = intArg(args, 6);
        // Invariant: an object tile previews the BTO the writer bakes, never the terrain tile at its ids.
        const bool object = support::boolArgument(args, 7);
        std::uint8_t* bytes = nullptr;
        std::size_t size = 0;
        const auto status = object
                                ? radicle_lod_preview_object_nif(handle_, world.c_str(), level, x, y, &bytes, &size)
                                : radicle_lod_preview_nif(handle_, world.c_str(), level, x, y, &bytes, &size);
        if (status != RADICLE_STATUS_OK) {
            return lodFailure(status, object ? "previewing the object LOD tile failed"
                                            : "previewing the LOD tile failed");
        }
        const std::string payload = payloadText(bytes, size);
        if (bytes != nullptr) radicle_free_bytes(bytes);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code code;
        const auto directory = std::filesystem::temp_directory_path(code);
        if (code) return std::unexpected(Error{"LodPreviewFailed", "no temporary folder for the preview"});
        const auto path = directory / ("garden-lod-" + std::to_string(stamp) + (object ? ".bto" : ".btr"));
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        out.close();
        if (!out) {
            std::error_code partial;
            std::filesystem::remove(path, partial);
            return std::unexpected(Error{"LodPreviewFailed", "writing the preview NIF failed"});
        }
        // Invariant: the newest previews stay readable, so a preview then nif_geometry on it still works.
        rememberPreview(previewPaths_, path, k_previewFilesKept);
        return path.string();
    }

    if (name == "Candidates") {
        const std::string world = support::stringArgument(args, 3);
        const auto level = intArg(args, 4);
        std::uint8_t* bytes = nullptr;
        std::size_t size = 0;
        const auto status = radicle_lod_candidates_json(handle_, world.c_str(), level, &bytes, &size);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "listing LOD candidates failed");
        return takeText(bytes, size, "listing LOD candidates");
    }

    if (name == "Generate") {
        const std::string ledger = support::stringArgument(args, 3);
        const bool apply = support::boolArgument(args, 4);
        std::uint8_t* report = nullptr;
        std::size_t reportSize = 0;
        std::uint8_t* kept = nullptr;
        std::size_t keptSize = 0;
        const auto status = radicle_lod_generate_json(handle_, ledger.c_str(), apply ? 1 : 0, &report, &reportSize,
                                                      &kept, &keptSize);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "generating LOD tiles failed");
        const std::string reportText = payloadText(report, reportSize);
        const std::string ledgerText = payloadText(kept, keptSize);
        if (report != nullptr) radicle_free_bytes(report);
        if (kept != nullptr) radicle_free_bytes(kept);
        return "{\"report\":" + reportText + ",\"ledger\":\"" + escapeJson(ledgerText) + "\"}";
    }

    if (name == "GenerateWorld") {
        const std::string world = support::stringArgument(args, 3);
        const std::string ledger = support::stringArgument(args, 4);
        const bool apply = support::boolArgument(args, 5);
        std::uint8_t* report = nullptr;
        std::size_t reportSize = 0;
        std::uint8_t* kept = nullptr;
        std::size_t keptSize = 0;
        const auto status = radicle_lod_generate_world_json(handle_, world.c_str(), ledger.c_str(), apply ? 1 : 0,
                                                            &report, &reportSize, &kept, &keptSize);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "generating one world's LOD tiles failed");
        const std::string reportText = payloadText(report, reportSize);
        const std::string keptText = payloadText(kept, keptSize);
        if (report != nullptr) radicle_free_bytes(report);
        if (kept != nullptr) radicle_free_bytes(kept);
        return "{\"report\":" + reportText + ",\"ledger\":\"" + escapeJson(keptText) + "\"}";
    }

    if (name == "GenerateScoped") {
        const std::string world = support::stringArgument(args, 3);
        const std::string ledger = support::stringArgument(args, 4);
        const bool apply = support::boolArgument(args, 5);
        // Constraint: a level bound of 0 keeps the world's own range, so an omitted level scopes nothing away.
        radicle_lod_scope scope{};
        scope.level_min = intArg(args, 6);
        scope.level_max = intArg(args, 7);
        scope.progress = &LodTarget::onProgress;
        scope.user = this;
        scope.cancel = &cancel_flag_;
        std::uint8_t* report = nullptr;
        std::size_t reportSize = 0;
        std::uint8_t* kept = nullptr;
        std::size_t keptSize = 0;
        const auto status = radicle_lod_generate_scoped_json(handle_, ledger.c_str(), apply ? 1 : 0, world.c_str(),
                                                             &scope, &report, &reportSize, &kept, &keptSize);
        if (status != RADICLE_STATUS_OK) return lodFailure(status, "generating one world's LOD levels failed");
        const std::string reportText = payloadText(report, reportSize);
        const std::string keptText = payloadText(kept, keptSize);
        if (report != nullptr) radicle_free_bytes(report);
        if (kept != nullptr) radicle_free_bytes(kept);
        return "{\"report\":" + reportText + ",\"ledger\":\"" + escapeJson(keptText) + "\"}";
    }

    if (name == "VerifyWorld") {
        const std::string world = support::stringArgument(args, 3);
        const std::string ledger = support::stringArgument(args, 4);
        std::uint8_t* bytes = nullptr;
        std::size_t size = 0;
        const auto status = radicle_lod_verify_world_json(handle_, world.c_str(), ledger.c_str(), &bytes, &size);
        if (status != RADICLE_STATUS_OK) {
            auto failure = lodFailure(status, "verifying one world's LOD tiles failed");
            if (bytes != nullptr) radicle_free_bytes(bytes);
            return failure;
        }
        return takeText(bytes, size, "verifying one world's LOD tiles");
    }

    const std::string ledger = support::stringArgument(args, 3);
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    const auto status = radicle_lod_verify_json(handle_, ledger.c_str(), &bytes, &size);
    // Constraint: the error text is read before the free, because radicle_free_bytes clears it.
    if (status != RADICLE_STATUS_OK) {
        auto failure = lodFailure(status, "verifying LOD tiles failed");
        if (bytes != nullptr) radicle_free_bytes(bytes);
        return failure;
    }
    return takeText(bytes, size, "verifying LOD tiles");
}

}  // namespace fo4ide::app
