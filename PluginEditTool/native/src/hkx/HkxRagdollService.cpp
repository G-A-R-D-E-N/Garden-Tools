#include <fo4ide/hkx/HkxService.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/hkx/packfile.hpp>
#include <fo4/hkx/physics_preview.hpp>
#include <fo4/hkx/ragdoll_edit.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/io/sha256.hpp>

namespace fo4ide::hkx {
namespace {

namespace fs = std::filesystem;

Result<std::vector<std::byte>> readFileBytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return std::unexpected(Error{"FileNotFound", "Could not open " + path.string()});
    const auto size = static_cast<std::size_t>(stream.tellg());
    std::vector<std::byte> bytes(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!stream) return std::unexpected(Error{"ReadFailed", "Could not read " + path.string()});
    return bytes;
}

Result<void> requireRagdollFile(const fs::path& path) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", "Ragdoll HKX path is empty"});
    if (!fs::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", "Ragdoll HKX not found: " + path.string()});
    }
    return {};
}

bool isSameFile(const fs::path& source, const fs::path& target) {
    std::error_code ec;
    if (fs::exists(target, ec) && fs::equivalent(source, target, ec)) return true;
    return fs::weakly_canonical(source, ec) == fs::weakly_canonical(target, ec);
}

// Constraint: false means the replaced file has no usable .bak, and the caller must then refuse the overwrite.
bool backupExisting(const fs::path& target) {
    std::error_code ec;
    if (!fs::is_regular_file(target, ec)) return true;
    auto backup = target;
    backup += ".bak";
    if (fs::exists(backup, ec)) return fs::is_regular_file(backup, ec);
    return fs::copy_file(target, backup, ec) && !ec;
}

std::string digestOf(const std::vector<std::byte>& bytes) {
    return fo4::io::sha256_hex(std::span<const std::byte>(bytes));
}

const nlohmann::json* member(const nlohmann::json& row, const char* key) {
    const auto found = row.find(key);
    if (found == row.end() || found->is_null()) return nullptr;
    return &*found;
}

std::optional<float> optionalNumber(const nlohmann::json& row, const char* key) {
    const auto* value = member(row, key);
    if (value == nullptr) return std::nullopt;
    if (!value->is_number()) throw std::invalid_argument(std::string(key) + " must be a number");
    return value->get<float>();
}

// Invariant: an integer is range-checked in 64 bits before the 32-bit cast, so a huge value is refused instead of wrapping to a valid id.
std::int32_t int32Within(const nlohmann::json& row, const char* key, std::int32_t lo, std::int32_t hi) {
    const auto* value = member(row, key);
    if (value == nullptr || !value->is_number_integer()) throw std::invalid_argument(std::string(key) + " must be an integer");
    const bool tooBig = value->is_number_unsigned() && value->get<std::uint64_t>() > static_cast<std::uint64_t>(hi);
    const std::int64_t number = tooBig ? static_cast<std::int64_t>(hi) + 1 : value->get<std::int64_t>();
    if (number < lo || number > hi) {
        throw std::invalid_argument(std::string(key) + " must be between " + std::to_string(lo) + " and " + std::to_string(hi));
    }
    return static_cast<std::int32_t>(number);
}

std::int32_t requiredId(const nlohmann::json& row, const char* key) {
    return int32Within(row, key, 0, std::numeric_limits<std::int32_t>::max());
}

std::optional<std::int32_t> optionalId(const nlohmann::json& row, const char* key) {
    if (member(row, key) == nullptr) return std::nullopt;
    return requiredId(row, key);
}

std::vector<float> numberList(const nlohmann::json& value, const char* key) {
    if (!value.is_array()) throw std::invalid_argument(std::string(key) + " must be an array of numbers");
    std::vector<float> out;
    for (const auto& entry : value) {
        if (!entry.is_number()) throw std::invalid_argument(std::string(key) + " must be an array of numbers");
        out.push_back(entry.get<float>());
    }
    return out;
}

template <std::size_t N>
std::optional<std::array<float, N>> optionalArray(const nlohmann::json& row, const char* key) {
    const auto* value = member(row, key);
    if (value == nullptr) return std::nullopt;
    const auto list = numberList(*value, key);
    if (list.size() != N) throw std::invalid_argument(std::string(key) + " must hold " + std::to_string(N) + " numbers");
    std::array<float, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = list[i];
    return out;
}

std::optional<std::vector<float>> optionalList(const nlohmann::json& row, const char* key) {
    const auto* value = member(row, key);
    if (value == nullptr) return std::nullopt;
    return numberList(*value, key);
}

const nlohmann::json& rowsOf(const nlohmann::json& source, const char* key) {
    static const nlohmann::json none = nlohmann::json::array();
    const auto* value = member(source, key);
    if (value == nullptr) return none;
    if (!value->is_array()) throw std::invalid_argument(std::string(key) + " must be an array");
    return *value;
}

fo4::hkx::HkxRagdollEdits parseRagdollEdits(const nlohmann::json& source) {
    if (!source.is_object()) throw std::invalid_argument("edits must be a JSON object");
    fo4::hkx::HkxRagdollEdits edits;
    for (const auto& row : rowsOf(source, "bodies")) {
        fo4::hkx::HkxRagdollBodyEdit edit;
        edit.body = requiredId(row, "body");
        edit.position = optionalArray<3>(row, "position");
        edit.rotation = optionalArray<4>(row, "rotation");
        edit.center_of_mass = optionalArray<3>(row, "center_of_mass");
        edit.mass = optionalNumber(row, "mass");
        edits.bodies.push_back(edit);
    }
    for (const auto& row : rowsOf(source, "constraints")) {
        fo4::hkx::HkxRagdollConstraintEdit edit;
        edit.constraint = requiredId(row, "constraint");
        edit.body_a = optionalId(row, "body_a");
        edit.body_b = optionalId(row, "body_b");
        edit.twist_min_angle = optionalNumber(row, "twist_min");
        edit.twist_max_angle = optionalNumber(row, "twist_max");
        edit.cone_max_angle = optionalNumber(row, "cone_max");
        edit.min_angle = optionalNumber(row, "hinge_min");
        edit.max_angle = optionalNumber(row, "hinge_max");
        edit.plane_min_angle = optionalNumber(row, "plane_min");
        edit.plane_max_angle = optionalNumber(row, "plane_max");
        edit.friction = optionalNumber(row, "friction");
        edit.frame_a = optionalList(row, "frame_a");
        edit.frame_b = optionalList(row, "frame_b");
        edits.constraints.push_back(std::move(edit));
    }
    for (const auto& row : rowsOf(source, "bone_map")) {
        edits.bone_map.push_back({requiredId(row, "bone"), requiredId(row, "body")});
    }
    for (const auto& row : rowsOf(source, "shapes")) {
        fo4::hkx::HkxRagdollShapeEdit edit;
        edit.shape = requiredId(row, "shape");
        edit.a = optionalArray<3>(row, "a");
        edit.b = optionalArray<3>(row, "b");
        edit.radius = optionalNumber(row, "radius");
        edits.shapes.push_back(edit);
    }
    for (const auto& row : rowsOf(source, "materials")) {
        fo4::hkx::HkxRagdollMaterialEdit edit;
        edit.material = requiredId(row, "material");
        edit.dynamic_friction = optionalNumber(row, "dynamic_friction");
        edit.static_friction = optionalNumber(row, "static_friction");
        edit.restitution = optionalNumber(row, "restitution");
        if (const auto* shared = member(row, "allow_shared")) {
            if (!shared->is_boolean()) throw std::invalid_argument("allow_shared must be true or false");
            edit.allow_shared = shared->get<bool>();
        }
        edits.materials.push_back(edit);
    }
    return edits;
}

}  // namespace

// Constraint: edits and options may arrive as a JSON object or as JSON text, because an MCP caller passes text.
Result<nlohmann::json> HkxService::saveRagdoll(const std::filesystem::path& path,
                                               const std::filesystem::path& outPath,
                                               const nlohmann::json& editsJson,
                                               bool overwrite,
                                               std::string_view expectedSource) const {
    if (auto valid = requireRagdollFile(path); !valid) return std::unexpected(valid.error());
    if (outPath.empty()) return std::unexpected(Error{"InvalidPath", "Output HKX path is empty"});
    if (isSameFile(path, outPath)) {
        return std::unexpected(Error{"RagdollRefused",
                                     "Refused: out_path is the source file; choose a Save As path so the source is not overwritten"});
    }
    if (auto allowed = checkSaveTarget(outPath, "A ragdoll"); !allowed) return std::unexpected(allowed.error());
    std::error_code ec;
    if (!overwrite && fs::exists(outPath, ec)) {
        return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + outPath.string()});
    }

    fo4::hkx::HkxRagdollEdits edits;
    try {
        edits = parseRagdollEdits(editsJson.is_string() ? nlohmann::json::parse(editsJson.get<std::string>()) : editsJson);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidEdits", error.what()});
    }

    auto source = readFileBytes(path);
    if (!source) return std::unexpected(source.error());
    const std::string sourceDigest = digestOf(*source);
    if (!expectedSource.empty() && sourceDigest != expectedSource) {
        return std::unexpected(Error{"SourceChanged", "Not saved: " + path.string() +
                                                          " changed on disk after it was loaded; reload it before saving"});
    }

    const auto outcome = fo4::hkx::apply_ragdoll_edits(std::span<const std::byte>(*source), edits);
    if (outcome.status != fo4::hkx::HkxRagdollEditStatus::Ok) {
        return std::unexpected(Error{"RagdollRefused", std::string(fo4::hkx::ragdoll_edit_status_name(outcome.status)) + ": " +
                                                           outcome.message + "; nothing was written"});
    }

    try {
        auto staged = fo4::io::StagedFile::create(outPath);
        staged.write(std::span<const std::byte>(outcome.bytes));
        staged.finish();
        const auto reread = readFileBytes(staged.path());
        if (!reread || *reread != outcome.bytes) {
            std::move(staged).cancel();
            return std::unexpected(Error{"RagdollVerifyFailed", "The staged file did not read back as the edited bytes; nothing was written"});
        }
        if (!overwrite) {
            if (!std::move(staged).commit_if_absent()) {
                return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + outPath.string()});
            }
        } else {
            if (!backupExisting(outPath)) {
                std::move(staged).cancel();
                return std::unexpected(Error{"BackupFailed", "Not saved: the backup of " + outPath.string() +
                                                                 " could not be written; the existing file was left as it was"});
            }
            std::move(staged).commit();
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"WriteFailed", error.what()});
    }

    return nlohmann::json{{"saved", true},
                          {"path", outPath.string()},
                          {"bytes", outcome.bytes.size()},
                          {"sha256", digestOf(outcome.bytes)},
                          {"source_sha256", sourceDigest},
                          {"message", outcome.message}};
}

namespace {

constexpr std::int32_t kPreviewDefaultSteps = 120;
constexpr std::int32_t kPreviewMaxSteps = 2400;
constexpr std::int32_t kPreviewMaxFrames = 240;
constexpr std::array<const char*, 6> kPreviewKindNames{"unknown", "fixed", "hinge", "limited_hinge", "ball_and_socket", "ragdoll"};

bool optionalBool(const nlohmann::json& row, const char* key, bool fallback) {
    const auto* value = member(row, key);
    if (value == nullptr) return fallback;
    if (!value->is_boolean()) throw std::invalid_argument(std::string(key) + " must be true or false");
    return value->get<bool>();
}

std::int32_t boundedInt(const nlohmann::json& row, const char* key, std::int32_t fallback, std::int32_t lo, std::int32_t hi) {
    if (member(row, key) == nullptr) return fallback;
    return int32Within(row, key, lo, hi);
}

nlohmann::json optionalValue(const std::optional<float>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json bodyJson(const fo4::hkx::HkxPreviewBodyState& state, bool full) {
    nlohmann::json out{{"body", state.body}, {"position", state.position}, {"rotation", state.rotation}};
    if (full) {
        out["mass"] = state.mass;
        out["center_of_mass"] = state.center_of_mass;
        out["linear_velocity"] = state.linear_velocity;
        out["angular_velocity"] = state.angular_velocity;
        out["grounded"] = state.grounded;
    }
    return out;
}

nlohmann::json constraintJson(const fo4::hkx::HkxPreviewConstraintState& state) {
    return {{"constraint", state.constraint},
            {"kind", std::string(kPreviewKindNames[static_cast<std::size_t>(state.kind)])},
            {"anchor_error", state.anchor_error},
            {"twist_angle", optionalValue(state.twist_angle)},
            {"cone_angle", optionalValue(state.cone_angle)},
            {"hinge_angle", optionalValue(state.hinge_angle)},
            {"twist_min", optionalValue(state.twist_min)},
            {"twist_max", optionalValue(state.twist_max)},
            {"cone_max", optionalValue(state.cone_max)},
            {"hinge_min", optionalValue(state.hinge_min)},
            {"hinge_max", optionalValue(state.hinge_max)},
            {"violation", state.violation},
            {"overridden", state.overridden}};
}

Error refusal(fo4::hkx::HkxPreviewStatus status, const std::string& message) {
    return Error{"PreviewRefused", std::string(fo4::hkx::preview_status_name(status)) + ": " + message + "; nothing was changed"};
}

}  // namespace

// Constraint: a preview never writes; it opens a private simulation from the file, runs a capped number of steps and returns the samples.
Result<nlohmann::json> HkxService::previewRagdoll(const std::filesystem::path& path, const nlohmann::json& optionsJson) const {
    if (auto valid = requireRagdollFile(path); !valid) return std::unexpected(valid.error());

    fo4::hkx::HkxPreviewOptions options;
    std::int32_t steps = kPreviewDefaultSteps;
    std::int32_t frameCount = 0;
    std::vector<fo4::hkx::HkxRagdollConstraintEdit> overrides;
    struct Push {
        std::int32_t body{0};
        fo4::hkx::HkxVec3 linear{};
        std::optional<fo4::hkx::HkxVec3> angular;
    };
    std::vector<Push> pushes;
    std::vector<fo4::hkx::HkxBoneFrame> pose;
    try {
        const nlohmann::json source = optionsJson.is_string() ? nlohmann::json::parse(optionsJson.get<std::string>())
                                      : optionsJson.is_null() ? nlohmann::json::object()
                                                              : optionsJson;
        if (!source.is_object()) throw std::invalid_argument("options must be a JSON object");
        options.gravity = optionalNumber(source, "gravity").value_or(options.gravity);
        options.ground_z = optionalNumber(source, "ground_z").value_or(options.ground_z);
        options.ground = optionalBool(source, "ground", options.ground);
        options.body_collision = optionalBool(source, "body_collision", options.body_collision);
        steps = boundedInt(source, "steps", kPreviewDefaultSteps, 0, kPreviewMaxSteps);
        frameCount = boundedInt(source, "frames", std::clamp<std::int32_t>(steps, 1, 30), 1, kPreviewMaxFrames);
        for (const auto& row : rowsOf(source, "limit_overrides")) {
            fo4::hkx::HkxRagdollConstraintEdit edit;
            edit.constraint = requiredId(row, "constraint");
            edit.twist_min_angle = optionalNumber(row, "twist_min");
            edit.twist_max_angle = optionalNumber(row, "twist_max");
            edit.cone_max_angle = optionalNumber(row, "cone_max");
            edit.min_angle = optionalNumber(row, "hinge_min");
            edit.max_angle = optionalNumber(row, "hinge_max");
            overrides.push_back(std::move(edit));
        }
        for (const auto& row : rowsOf(source, "impulses")) {
            Push push;
            push.body = requiredId(row, "body");
            push.linear = optionalArray<3>(row, "linear").value_or(fo4::hkx::HkxVec3{});
            push.angular = optionalArray<3>(row, "angular");
            pushes.push_back(push);
        }
        for (const auto& row : rowsOf(source, "pose")) {
            const auto position = optionalArray<3>(row, "position");
            const auto rotation = optionalArray<4>(row, "rotation");
            if (!position || !rotation) throw std::invalid_argument("each pose row needs position and rotation");
            pose.push_back({*position, *rotation});
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InvalidOptions", error.what()});
    }

    auto bytes = readFileBytes(path);
    if (!bytes) return std::unexpected(bytes.error());
    fo4::hkx::HkxPackfile pack;
    if (fo4::hkx::read_packfile(std::span<const std::byte>(*bytes), pack) != fo4::hkx::HkxStatus::Ok) {
        return std::unexpected(Error{"HkxRead", "packfile read failed: " + path.string()});
    }
    fo4::hkx::HkxRagdollModel model;
    fo4::hkx::HkxPhysicsLoadReport report;
    if (fo4::hkx::read_ragdoll(pack, model, report) != fo4::hkx::HkxStatus::Ok) {
        return std::unexpected(Error{"NotARagdoll", report.headline()});
    }
    fo4::hkx::HkxRagdollMeasures measures;
    if (fo4::hkx::read_ragdoll_measures(pack, measures) != fo4::hkx::HkxStatus::Ok) {
        return std::unexpected(Error{"HkxRead", "ragdoll measures could not be read: " + path.string()});
    }

    fo4::hkx::HkxPhysicsPreview preview;
    std::string message;
    if (const auto status = preview.open(model, measures, options, &message); status != fo4::hkx::HkxPreviewStatus::Ok) {
        return std::unexpected(refusal(status, message));
    }
    if (!pose.empty()) {
        if (const auto status = preview.set_pose(pose, &message); status != fo4::hkx::HkxPreviewStatus::Ok) {
            return std::unexpected(refusal(status, message));
        }
    }
    for (const auto& edit : overrides) {
        if (const auto status = preview.override_limits(edit, &message); status != fo4::hkx::HkxPreviewStatus::Ok) {
            return std::unexpected(refusal(status, message));
        }
    }
    for (const Push& push : pushes) {
        if (const auto status = preview.apply_impulse(push.body, push.linear, push.angular); status != fo4::hkx::HkxPreviewStatus::Ok) {
            return std::unexpected(refusal(status, "impulse names an unknown body or a non-finite value"));
        }
    }

    const auto sample = [&](nlohmann::json& frames) {
        nlohmann::json bodies = nlohmann::json::array();
        for (const auto& state : preview.bodies()) bodies.push_back(bodyJson(state, false));
        frames.push_back({{"step", preview.steps()}, {"bodies", std::move(bodies)}});
    };
    nlohmann::json frames = nlohmann::json::array();
    sample(frames);
    const std::int32_t chunk = std::max<std::int32_t>(1, (steps + frameCount - 1) / frameCount);
    std::int32_t done = 0;
    while (done < steps) {
        const std::int32_t run = std::min(chunk, steps - done);
        if (const auto status = preview.step(run, &message); status != fo4::hkx::HkxPreviewStatus::Ok) {
            return std::unexpected(refusal(status, message));
        }
        done += run;
        sample(frames);
    }

    nlohmann::json finalBodies = nlohmann::json::array();
    for (const auto& state : preview.bodies()) finalBodies.push_back(bodyJson(state, true));
    nlohmann::json constraints = nlohmann::json::array();
    for (const auto& state : preview.constraints()) constraints.push_back(constraintJson(state));
    return nlohmann::json{{"previewed", true},
                          {"saved", false},
                          {"steps", done},
                          {"step_seconds", fo4::hkx::kPreviewStep},
                          {"gravity", options.gravity},
                          {"ground", options.ground},
                          {"ground_z", options.ground_z},
                          {"frames", std::move(frames)},
                          {"bodies", std::move(finalBodies)},
                          {"constraints", std::move(constraints)}};
}

}
