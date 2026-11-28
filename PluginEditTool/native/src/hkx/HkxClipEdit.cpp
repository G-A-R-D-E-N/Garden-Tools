#include <fo4ide/hkx/HkxService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <fo4/hkx/animation_edit.hpp>
#include <fo4/hkx/packfile.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/io/sha256.hpp>

namespace fo4ide::hkx {
namespace {

using fo4::hkx::HkxClipEdit;
using fo4::hkx::HkxEditReport;
using fo4::hkx::HkxEditStatus;
using fo4::hkx::HkxKey;
using fo4::hkx::HkxMotionKind;
using fo4::hkx::HkxRetimeBudget;
using fo4::hkx::HkxSaveOptions;

using Json = nlohmann::json;

void allow(const Json& op, std::initializer_list<std::string_view> keys) {
    for (const auto& item : op.items()) {
        if (item.key() == "op") continue;
        if (std::ranges::find(keys, std::string_view(item.key())) == keys.end()) {
            throw std::invalid_argument("unknown field '" + item.key() + "'");
        }
    }
}

const Json& field(const Json& op, const char* key) {
    const auto it = op.find(key);
    if (it == op.end() || it->is_null()) throw std::invalid_argument(std::string(key) + " is required");
    return *it;
}

float number(const Json& value, const std::string& label) {
    if (!value.is_number()) throw std::invalid_argument(label + " must be a number");
    const auto parsed = value.get<double>();
    if (!std::isfinite(parsed) || std::fabs(parsed) > std::numeric_limits<float>::max()) {
        throw std::invalid_argument(label + " must be a finite number");
    }
    return static_cast<float>(parsed);
}

float numberField(const Json& op, const char* key) { return number(field(op, key), key); }

std::size_t indexField(const Json& op, const char* key) {
    const auto& value = field(op, key);
    if (!value.is_number_integer()) throw std::invalid_argument(std::string(key) + " must be a whole number");
    const auto parsed = value.get<std::int64_t>();
    if (parsed < 0) throw std::invalid_argument(std::string(key) + " must be zero or more");
    return static_cast<std::size_t>(parsed);
}

std::string textField(const Json& op, const char* key) {
    const auto& value = field(op, key);
    if (!value.is_string()) throw std::invalid_argument(std::string(key) + " must be text");
    return value.get<std::string>();
}

template <std::size_t N>
std::optional<std::array<float, N>> vectorField(const Json& op, const char* key) {
    const auto it = op.find(key);
    if (it == op.end() || it->is_null()) return std::nullopt;
    if (!it->is_array() || it->size() != N) {
        throw std::invalid_argument(std::string(key) + " must be a list of " + std::to_string(N) + " numbers");
    }
    std::array<float, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = number((*it)[i], key);
    return out;
}

float optionalLimit(const Json& op, const char* key) {
    const auto it = op.find(key);
    if (it == op.end() || it->is_null()) return std::numeric_limits<float>::infinity();
    return number(*it, key);
}

void merge(HkxEditReport& total, const HkxEditReport& part) {
    total.annotations_dropped += part.annotations_dropped;
    total.resampled = total.resampled || part.resampled;
    total.position_error = std::max(total.position_error, part.position_error);
    total.rotation_error = std::max(total.rotation_error, part.rotation_error);
    total.scale_error = std::max(total.scale_error, part.scale_error);
}

HkxEditStatus apply(HkxClipEdit& clip, const Json& op, HkxEditReport& total) {
    if (!op.is_object()) throw std::invalid_argument("each op must be an object");
    const auto name = textField(op, "op");
    HkxEditReport part;
    HkxEditStatus status{};
    if (name == "trim") {
        allow(op, {"first", "last"});
        status = fo4::hkx::trim_clip(clip, indexField(op, "first"), indexField(op, "last"), &part);
    } else if (name == "retime") {
        allow(op, {"scale", "duration", "keep_frame_rate", "max_position", "max_rotation", "max_scale"});
        const auto keep = op.find("keep_frame_rate");
        if (keep != op.end() && !keep->is_boolean()) throw std::invalid_argument("keep_frame_rate must be true or false");
        const HkxRetimeBudget budget{optionalLimit(op, "max_position"), optionalLimit(op, "max_rotation"),
                                     optionalLimit(op, "max_scale")};
        const bool keepRate = keep != op.end() && keep->get<bool>();
        const bool hasScale = op.contains("scale") && !op["scale"].is_null();
        const bool hasDuration = op.contains("duration") && !op["duration"].is_null();
        if (hasScale == hasDuration) throw std::invalid_argument("retime needs exactly one of scale and duration");
        status = hasScale ? fo4::hkx::retime_clip(clip, numberField(op, "scale"), keepRate, budget, &part)
                          : fo4::hkx::retime_clip_to_duration(clip, numberField(op, "duration"), keepRate, budget, &part);
    } else if (name == "loop") {
        allow(op, {"blend_frames"});
        status = fo4::hkx::loop_clip(clip, indexField(op, "blend_frames"), &part);
    } else if (name == "set_key") {
        allow(op, {"track", "frame", "translation", "rotation", "scale"});
        HkxKey key;
        key.translation = vectorField<3>(op, "translation");
        key.rotation = vectorField<4>(op, "rotation");
        key.scale = vectorField<3>(op, "scale");
        status = fo4::hkx::set_key(clip, indexField(op, "track"), indexField(op, "frame"), key);
    } else if (name == "copy_pose") {
        allow(op, {"from", "to"});
        status = fo4::hkx::copy_pose(clip, indexField(op, "from"), indexField(op, "to"));
    } else if (name == "insert_frames") {
        allow(op, {"at", "count"});
        status = fo4::hkx::insert_frames(clip, indexField(op, "at"), indexField(op, "count"));
    } else if (name == "delete_frames") {
        allow(op, {"first", "count"});
        status = fo4::hkx::delete_frames(clip, indexField(op, "first"), indexField(op, "count"), &part);
    } else if (name == "move_frames") {
        allow(op, {"first", "count", "to"});
        status = fo4::hkx::move_frames(clip, indexField(op, "first"), indexField(op, "count"), indexField(op, "to"));
    } else if (name == "add_annotation") {
        allow(op, {"track_name", "time", "text"});
        status = fo4::hkx::add_annotation(clip, textField(op, "track_name"), numberField(op, "time"),
                                          textField(op, "text"));
    } else if (name == "edit_annotation") {
        allow(op, {"track", "index", "time", "text"});
        status = fo4::hkx::edit_annotation(clip, indexField(op, "track"), indexField(op, "index"),
                                           numberField(op, "time"), textField(op, "text"));
    } else if (name == "remove_annotation") {
        allow(op, {"track", "index"});
        status = fo4::hkx::remove_annotation(clip, indexField(op, "track"), indexField(op, "index"));
    } else {
        throw std::invalid_argument("unknown op '" + name + "'");
    }
    merge(total, part);
    return status;
}

std::string_view motionName(HkxMotionKind kind) {
    switch (kind) {
        case HkxMotionKind::None: return "none";
        case HkxMotionKind::Default: return "default";
        case HkxMotionKind::Unsupported: return "unsupported";
    }
    return "unknown";
}

Json clipSummary(const HkxClipEdit& clip) {
    auto tracks = Json::array();
    for (const auto& track : clip.animation.annotation_tracks) {
        tracks.push_back({{"name", track.name}, {"annotations", track.annotations.size()}});
    }
    return {{"frame_count", clip.animation.frame_count},
            {"duration", clip.animation.duration},
            {"track_count", clip.animation.tracks.size()},
            {"motion", std::string(motionName(clip.motion.kind))},
            {"annotation_tracks", std::move(tracks)}};
}

Error refused(std::size_t op, HkxEditStatus status) {
    return Error{"HkxClipEdit", "op " + std::to_string(op) + " refused: " + fo4::hkx::hkx_edit_status_name(status)};
}

}  // namespace

// Invariant: nothing is written until every op and the verified save succeeded; a target that exists needs overwrite.
Result<nlohmann::json> HkxService::clipEdit(const std::filesystem::path& path, const nlohmann::json& ops,
                                            const ClipSaveOptions& save, const std::filesystem::path& outPath,
                                            std::string_view expectedSource, bool overwrite) const {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", "HKX path is empty"});
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", "HKX not found: " + path.string()});
    }
    if (!ops.is_array() || ops.empty()) {
        return std::unexpected(Error{"InvalidClipEdit", "ops must be a non-empty list of edit objects"});
    }
    if (!outPath.empty()) {
        if (auto allowed = checkSaveTarget(outPath, "An animation"); !allowed) return std::unexpected(allowed.error());
    }
    HkxSaveOptions options;
    for (const auto& [limit, target, label] :
         {std::tuple{save.maxPositionError, &options.max_position_error, "max_position_error"},
          std::tuple{save.maxRotationError, &options.max_rotation_error, "max_rotation_error"},
          std::tuple{save.maxScaleError, &options.max_scale_error, "max_scale_error"}}) {
        if (!limit) continue;
        if (!std::isfinite(*limit) || *limit < 0.0f) {
            return std::unexpected(Error{"InvalidClipEdit", std::string(label) + " must be zero or more"});
        }
        *target = *limit;
    }

    auto read = readHkxBytes(path);
    if (!read) return std::unexpected(read.error());
    const std::vector<std::byte>& source = *read;

    const auto digest = fo4::io::sha256_hex(source);
    if (!expectedSource.empty() && digest != expectedSource) {
        return std::unexpected(Error{"HkxChanged", "Not saved: " + path.string() +
                                                       " changed on disk after it was loaded -- reload it before saving"});
    }

    HkxClipEdit clip;
    if (const auto status = fo4::hkx::open_clip_edit(source, clip); status != HkxEditStatus::Ok) {
        auto reason = fo4::hkx::describe_packfile_refusal(source);
        return std::unexpected(Error{"HkxClipEdit", std::string("clip open refused: ") +
                                                        fo4::hkx::hkx_edit_status_name(status) +
                                                        (reason.empty() ? "" : " (" + reason + ")")});
    }

    HkxEditReport report;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        try {
            if (const auto status = apply(clip, ops[i], report); status != HkxEditStatus::Ok) {
                return std::unexpected(refused(i, status));
            }
        } catch (const std::invalid_argument& error) {
            return std::unexpected(Error{"InvalidClipEdit", "op " + std::to_string(i) + ": " + error.what()});
        }
    }

    options.spline = save.spline.value_or(clip.animation.kind == fo4::hkx::HkxAnimationKind::Spline);
    std::vector<std::byte> bytes;
    HkxEditReport saved;
    if (const auto status = fo4::hkx::save_clip_edit(source, clip, options, bytes, &saved);
        status != HkxEditStatus::Ok) {
        std::string message = std::string("save refused: ") + fo4::hkx::hkx_edit_status_name(status);
        if (status == HkxEditStatus::BudgetExceeded) {
            message += " (position error " + std::to_string(saved.position_error) + " of " +
                       std::to_string(options.max_position_error) + ", rotation error " +
                       std::to_string(saved.rotation_error) + " of " + std::to_string(options.max_rotation_error) +
                       ", scale error " + std::to_string(saved.scale_error) + " of " +
                       std::to_string(options.max_scale_error) + ")";
        }
        return std::unexpected(Error{"HkxClipEdit", std::move(message)});
    }

    const auto reportJson = [](const HkxEditReport& value) {
        return Json{{"annotations_dropped", value.annotations_dropped},
                    {"resampled", value.resampled},
                    {"position_error", value.position_error},
                    {"rotation_error", value.rotation_error},
                    {"scale_error", value.scale_error}};
    };
    Json reply = clipSummary(clip);
    reply["source"] = digest;
    reply["ops"] = ops.size();
    reply["bytes"] = bytes.size();
    reply["spline"] = options.spline;
    reply["saved"] = false;
    reply["report"] = reportJson(report);
    reply["save_report"] = reportJson(saved);
    if (outPath.empty()) return reply;

    std::error_code same;
    const bool inPlace = std::filesystem::equivalent(path, outPath, same);
    if (inPlace) {
        auto again = readHkxBytes(path);
        if (!again || fo4::io::sha256_hex(*again) != digest) {
            return std::unexpected(Error{"HkxChanged", "Not saved: " + path.string() +
                                                           " changed on disk while it was being edited"});
        }
    }
    std::error_code present;
    if (overwrite && std::filesystem::exists(outPath, present)) {
        auto replaced = inPlace ? Result<std::vector<std::byte>>(source) : readHkxBytes(outPath);
        if (!replaced) {
            return std::unexpected(Error{"BackupFailed", "Not saved: " + outPath.string() +
                                                             " could not be read to keep a backup; it was left as it was"});
        }
        try {
            fo4::io::write_rotating_backup(outPath, *replaced);
        } catch (const std::exception& error) {
            return std::unexpected(Error{"BackupFailed", std::string("Not saved: the backup of ") + outPath.string() +
                                                             " could not be written (" + error.what() +
                                                             "); the existing file was left as it was"});
        }
        reply["backup"] = outPath.string() + ".bak";
    }
    if (auto published = publishHkxBytes(outPath, bytes, overwrite, "HkxSaveFailed"); !published) {
        return std::unexpected(published.error());
    }
    reply["saved"] = true;
    reply["out"] = outPath.string();
    return reply;
}

}  // namespace fo4ide::hkx
