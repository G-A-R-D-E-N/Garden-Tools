#include <fo4ide/app/SwfTarget.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/c_api.h>
#include <fo4/c_api_swf.h>
#include <fo4/io/atomic_write.hpp>
#include <fo4ide/core/PayloadLimits.hpp>

#include "ServiceTargetSupport.hpp"
#include "SwfTargetSupport.hpp"

namespace fo4ide::app {
namespace fs = std::filesystem;

namespace {

using swftarget::BytesPtr;
using swftarget::errorStatus;
using swftarget::errorText;
using swftarget::radicleError;
using swftarget::takeError;
using swftarget::takeText;
using swftarget::utf8Path;
using swftarget::utf8String;

}  // namespace

std::vector<fs::path> swfRootsFor(const fo4ide::settings::Settings& settings) {
    return {settings.dataFolder, settings.fallout4Path, settings.mo2InstancePath, settings.outputFolder};
}

void SwfTarget::closeDocument() {
    if (history_ != nullptr) radicle_swf_history_destroy(history_);
    if (movie_ != nullptr) radicle_swf_destroy(movie_);
    history_ = nullptr;
    movie_ = nullptr;
    path_.reset();
    saved_.clear();
    current_.clear();
}

bool SwfTarget::requireUnderRoots(const std::string& path, std::string& rooted) const {
    std::error_code code;
    const fs::path full = fs::weakly_canonical(utf8Path(path), code);
    if (code) return false;
    if (browsed_ && full == *browsed_) {
        rooted = utf8String(full);
        return true;
    }
    for (const auto& root : roots_()) {
        // Constraint: a blank root is skipped, as ToolPaths.RequireUnderRoots skips IsNullOrWhiteSpace roots.
        if (support::trim(root.string()).empty()) continue;
        std::error_code rootCode;
        const fs::path base = fs::weakly_canonical(root, rootCode);
        if (rootCode) continue;
        const auto [baseEnd, fullEnd] =
            std::mismatch(base.begin(), base.end(), full.begin(), full.end());
        if (baseEnd == base.end() && fullEnd != full.end()) {
            rooted = utf8String(full);
            return true;
        }
    }
    return false;
}

Result<nlohmann::json> SwfTarget::open(const nlohmann::json& args) {
    closeDocument();
    const std::string path = support::stringArgument(args, 0);
    std::string rooted;
    if (!requireUnderRoots(path, rooted)) {
        return errorText("the path is outside the configured Data and project folders");
    }
    std::error_code code;
    const auto size = fs::file_size(utf8Path(rooted), code);
    if (code) return errorText("no SWF file was found at that path: " + path);
    if (size > core::kMaxSwfFileBytes) {
        return errorText("the SWF file is larger than the 256 MiB limit: " + path);
    }
    std::ifstream stream(utf8Path(rooted), std::ios::binary);
    if (!stream) return errorText("no SWF file was found at that path: " + path);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.bad() || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return errorText("the SWF file could not be read: " + path);
    }

    radicle_swf* movie = nullptr;
    const auto* data = bytes.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(bytes.data());
    radicle_status status = radicle_swf_open_bytes(data, bytes.size(), &movie);
    if (status != RADICLE_STATUS_OK || movie == nullptr) {
        const std::string detail = takeError();
        if (movie != nullptr) radicle_swf_destroy(movie);
        return errorStatus(status, detail);
    }
    radicle_swf_history* history = nullptr;
    status = radicle_swf_history_create(movie, &history);
    if (status != RADICLE_STATUS_OK || history == nullptr) {
        const std::string detail = takeError();
        if (history != nullptr) radicle_swf_history_destroy(history);
        radicle_swf_destroy(movie);
        return errorStatus(status, detail);
    }
    movie_ = movie;
    history_ = history;
    path_ = rooted;
    saved_ = bytes;
    current_ = std::move(bytes);
    return snapshot(false);
}

Result<nlohmann::json> SwfTarget::save() {
    if (!path_) return errorText("the document has no path to save to");
    std::uint8_t* raw = nullptr;
    std::size_t size = 0;
    if (radicle_swf_history_write_bytes(history_, &raw, &size) != RADICLE_STATUS_OK) {
        if (raw != nullptr) radicle_free_bytes(raw);
        return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
    }
    BytesPtr holder(raw);
    radicle_swf* probe = nullptr;
    if (radicle_swf_open_bytes(raw, size, &probe) != RADICLE_STATUS_OK || probe == nullptr) {
        const std::string detail = takeError();
        if (probe != nullptr) radicle_swf_destroy(probe);
        return errorText("the edited movie does not reopen, so nothing was saved: " + detail);
    }
    radicle_swf_destroy(probe);
    const std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(raw),
                                       reinterpret_cast<const std::byte*>(raw) + size);
    // Constraint: the first save must keep a .bak of the on-disk original, and later saves must never overwrite it.
    const std::string backup = *path_ + ".bak";
    std::error_code code;
    if (fs::exists(utf8Path(*path_), code) && !fs::exists(utf8Path(backup), code)) {
        fs::copy_file(utf8Path(*path_), utf8Path(backup), code);
        if (code) {
            return errorText("the SWF backup could not be written: " + backup + ": " + code.message());
        }
    }
    try {
        fo4::io::write_file_atomic(utf8Path(*path_), bytes);
    } catch (const std::exception& ex) {
        return errorText(std::string("the SWF file could not be written: ") + *path_ + ": " + ex.what());
    }
    saved_ = bytes;
    current_ = bytes;
    return nlohmann::json(nlohmann::json{{"saved", *path_}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

Result<nlohmann::json> SwfTarget::snapshot(bool changed) {
    if (history_ == nullptr) return errorText("no SWF document is open");
    std::uint8_t* raw = nullptr;
    std::size_t size = 0;
    const radicle_status status = radicle_swf_history_write_bytes(history_, &raw, &size);
    if (status != RADICLE_STATUS_OK) {
        if (raw != nullptr) radicle_free_bytes(raw);
        return radicleError(status);
    }
    BytesPtr holder(raw);
    const std::vector<std::byte> bytes(reinterpret_cast<const std::byte*>(raw),
                                       reinterpret_cast<const std::byte*>(raw) + size);
    current_.assign(bytes.begin(), bytes.end());
    std::size_t undo = 0;
    std::size_t redo = 0;
    if (radicle_swf_history_undo_count(history_, &undo) != RADICLE_STATUS_OK ||
        radicle_swf_history_redo_count(history_, &redo) != RADICLE_STATUS_OK) {
        return radicleError(RADICLE_STATUS_INTERNAL_ERROR);
    }
    nlohmann::json body{{"changed", changed},
                        {"revision", radicle_swf_history_revision(history_)},
                        {"undo", undo},
                        {"redo", redo},
                        {"dirty", current_ != saved_}};
    return nlohmann::json(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

bool SwfTarget::reopenHistory(std::string& message) {
    std::uint8_t* raw = nullptr;
    std::size_t size = 0;
    if (radicle_swf_history_write_bytes(history_, &raw, &size) != RADICLE_STATUS_OK) {
        if (raw != nullptr) radicle_free_bytes(raw);
        const char* detail = radicle_last_error();
        message = detail ? detail : "the history bytes could not be read";
        return false;
    }
    BytesPtr holder(raw);
    radicle_swf* next = nullptr;
    const radicle_status status = radicle_swf_open_bytes(raw, size, &next);
    if (status == RADICLE_STATUS_OK && next != nullptr) {
        if (movie_ != nullptr) radicle_swf_destroy(movie_);
        movie_ = next;
        current_.assign(reinterpret_cast<const std::byte*>(raw), reinterpret_cast<const std::byte*>(raw) + size);
        return true;
    }
    const char* detail = radicle_last_error();
    message = detail ? detail : "the edited movie did not decode";
    if (next != nullptr) radicle_swf_destroy(next);
    return false;
}

bool SwfTarget::refreshFromHistory(std::string& message, bool undid) {
    if (reopenHistory(message)) return true;
    radicle_swf_plan_result stepped{};
    const radicle_status step = undid ? radicle_swf_history_redo(history_, &stepped)
                                      : radicle_swf_history_undo(history_, &stepped);
    if (step == RADICLE_STATUS_OK) {
        std::string ignored;
        reopenHistory(ignored);
    }
    return false;
}

}  // namespace fo4ide::app
