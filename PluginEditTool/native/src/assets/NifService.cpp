#include <fo4ide/assets/NifService.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/nif/block_ops.hpp>
#include <fo4/nif/connect_points.hpp>
#include <fo4/nif/controller_keys.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_assembly.hpp>
#include <fo4/nif/nif_fix.hpp>
#include <fo4/nif/nif_import.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/nif/shape_join.hpp>
#include <fo4/nif/shape_model_ops.hpp>
#include <fo4/nif/shape_ops.hpp>
#include <fo4/nif/skin.hpp>
#include <fo4/nif/skin_validate.hpp>
#include <fo4/nif/structure.hpp>
#include <fo4ide/assets/NifRender.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fo4ide::assets {
namespace {

std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

fo4::nif::NifFile readNif(const std::filesystem::path& path) {
    const auto raw = readFile(path);
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

Result<void> requireUnchangedSource(const std::filesystem::path& nifPath,
                                    std::span<const std::byte> source,
                                    std::string_view expectedSource) {
    if (expectedSource.empty() || fo4::io::sha256_hex(source) == expectedSource) return {};
    return std::unexpected(Error{"NifChanged", "Not saved: " + nifPath.string() +
        " changed on disk after it was loaded -- reload it before saving"});
}

fo4::io::StagedFile stageNif(const std::vector<std::byte>& bytes, const std::filesystem::path& target) {
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path());
    auto staged = fo4::io::StagedFile::create(target);
    staged.write(bytes);
    staged.finish();
    staged.verify(bytes.size());
    return staged;
}

Error outputExists(const std::filesystem::path& target) {
    return outputExistsError(target);
}

Result<void> refuseExisting(const std::filesystem::path& target, bool overwrite) {
    std::error_code present;
    if (!overwrite && std::filesystem::exists(target, present)) return std::unexpected(outputExists(target));
    return {};
}

Result<void> publishNif(fo4::io::StagedFile&& staged, const std::filesystem::path& target, bool overwrite) {
    if (overwrite) {
        std::move(staged).commit();
        return {};
    }
    if (!std::move(staged).commit_if_absent()) return std::unexpected(outputExists(target));
    return {};
}

Result<std::string> writeVerifiedNif(const std::vector<std::byte>& bytes, const std::filesystem::path& target,
                                     bool overwrite, const std::optional<std::filesystem::path>& baseline) {
    try {
        auto staged = stageNif(bytes, target);
        const auto fresh = renderNewVerifyFailures(staged.path(), baseline);
        if (!fresh.empty()) {
            std::string list;
            for (const auto& failure : fresh) list += (list.empty() ? "" : "; ") + failure;
            return std::unexpected(Error{"NifVerifyFailed", "Not written: the NIF would fail verification: " + list});
        }
        auto verify = renderVerify(staged.path());
        if (auto published = publishNif(std::move(staged), target, overwrite); !published) {
            return std::unexpected(published.error());
        }
        return verify;
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
}

fo4::nif::ImportOptions toRadicleImport(const NifImportOptions& options) {
    fo4::nif::ImportOptions out;
    out.material = options.material;
    out.tex_diffuse = options.diffuseTexture;
    out.tex_normal = options.normalTexture;
    out.box_collision = options.collision == NifCollisionShape::box;
    out.hull_collision = options.collision == NifCollisionShape::hull;
    out.mesh_collision = options.collision == NifCollisionShape::mesh;
    out.from_blender = options.fromBlender;
    out.body_mass = static_cast<float>(options.massKg);
    return out;
}

double displayFloat(float value) {
    char text[32];
    for (int digits = 1; digits <= 9; ++digits) {
        std::snprintf(text, sizeof text, "%.*g", digits, static_cast<double>(value));
        if (std::strtof(text, nullptr) == value) break;
    }
    return std::strtod(text, nullptr);
}

nlohmann::json floats(const float* values, std::size_t count) {
    auto out = nlohmann::json::array();
    for (std::size_t i = 0; i < count; ++i) out.push_back(displayFloat(values[i]));
    return out;
}

const char* resolutionName(fo4::nif::ConnectParentResolution resolution) {
    switch (resolution) {
        case fo4::nif::ConnectParentResolution::local: return "local";
        case fo4::nif::ConnectParentResolution::resolved: return "resolved";
        case fo4::nif::ConnectParentResolution::unresolved: return "unresolved";
        case fo4::nif::ConnectParentResolution::ambiguous: return "ambiguous";
    }
    return "unresolved";
}

nlohmann::json connectPointsJson(const fo4::nif::NifFile& nif) {
    auto blocks = nlohmann::json::array();
    for (const auto block : fo4::nif::connect_point_blocks(nif)) {
        const auto& type = nif.blocks()[block].type;
        if (type == fo4::nif::k_connect_parents_type) {
            const auto value = fo4::nif::read_connect_parents(nif, block);
            auto points = nlohmann::json::array();
            for (const auto& point : value.points) {
                const auto resolution = fo4::nif::connect_parent_resolution(nif, point);
                auto entry = nlohmann::json{{"parent", point.parent}, {"name", point.name},
                                            {"rotation", floats(point.rotation, 4)},
                                            {"translation", floats(point.translation, 3)},
                                            {"scale", displayFloat(point.scale)},
                                            {"resolution", resolutionName(resolution)}};
                if (resolution == fo4::nif::ConnectParentResolution::local ||
                    resolution == fo4::nif::ConnectParentResolution::resolved) {
                    const auto world = fo4::nif::connect_point_world_position(nif, point);
                    entry["world"] = floats(world.data(), world.size());
                }
                points.push_back(std::move(entry));
            }
            blocks.push_back({{"block", block}, {"type", type}, {"name", value.name}, {"points", points}});
        } else {
            const auto value = fo4::nif::read_connect_children(nif, block);
            blocks.push_back({{"block", block}, {"type", type}, {"name", value.name},
                              {"skinned", value.skinned}, {"names", value.names}});
        }
    }
    return blocks;
}

fo4::nif::Shape shapeForBlock(const fo4::nif::NifFile& nif, std::uint32_t block) {
    if (block >= nif.blocks().size() || !fo4::nif::is_shape_block_type(nif.blocks()[block].type)) {
        throw std::invalid_argument("block " + std::to_string(block) + " is not a tri-shape");
    }
    return fo4::nif::read_shape(nif, block);
}

nlohmann::json boneNamesJson(const fo4::nif::Shape& shape) {
    auto names = nlohmann::json::array();
    for (const auto& bone : shape.bones) names.push_back(bone.name);
    return names;
}

nlohmann::json influencesJson(const fo4::nif::NifFile& nif, std::uint32_t block, std::uint32_t vertex) {
    const auto shape = shapeForBlock(nif, block);
    if (vertex >= shape.verts.size() / 3) {
        throw std::invalid_argument("there is no vertex " + std::to_string(vertex));
    }
    auto list = nlohmann::json::array();
    for (const auto& influence : fo4::nif::influences_of(shape, vertex)) {
        const auto bone = static_cast<std::size_t>(influence.bone);
        list.push_back({{"bone", influence.bone},
                        {"name", bone < shape.bones.size() ? shape.bones[bone].name : std::string{}},
                        {"weight", displayFloat(influence.weight)}});
    }
    return nlohmann::json{{"block", block},       {"name", shape.name},   {"vertex", vertex},
                          {"bones", boneNamesJson(shape)}, {"influences", list}};
}

nlohmann::json influencedByJson(const fo4::nif::NifFile& nif, std::uint32_t block, std::int32_t bone,
                                float minimum) {
    const auto shape = shapeForBlock(nif, block);
    const auto found = fo4::nif::influenced_by(shape, bone, minimum);
    auto vertices = nlohmann::json::array();
    for (const auto vertex : found.vertices) vertices.push_back(vertex);
    auto out = nlohmann::json{{"block", block},      {"name", shape.name},          {"bone", found.bone},
                              {"bone_name", found.name}, {"vertices", vertices},     {"weight", displayFloat(found.weight)},
                              {"minimum", displayFloat(minimum)}};
    if (found.any()) {
        out["min"] = floats(found.min.data(), 3);
        out["max"] = floats(found.max.data(), 3);
    }
    return out;
}

nlohmann::json skinReportJson(const fo4::nif::NifFile& nif, std::uint32_t block) {
    const auto shape = shapeForBlock(nif, block);
    const auto report = fo4::nif::validate_skin(nif, shape);
    auto findings = nlohmann::json::array();
    for (const auto& finding : report.findings) {
        findings.push_back({{"level", finding.level == fo4::nif::SkinFindingLevel::error ? "error" : "warning"},
                            {"where", finding.where},
                            {"what", finding.what}});
    }
    return nlohmann::json{{"block", report.block},
                          {"name", shape.name},
                          {"bones", report.bones},
                          {"bind_transforms", report.bind_transforms},
                          {"used_bones", report.used_bones},
                          {"ok", report.ok()},
                          {"errors", report.errors()},
                          {"warnings", report.warnings()},
                          {"findings", findings}};
}

Result<std::string> commitShapeOp(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath,
                                  std::string_view opName, const std::vector<std::byte>& bytes,
                                  const fo4::nif::ShapeOpReport& report, const std::string& detail, bool overwrite) {
    auto verify = writeVerifiedNif(bytes, outputPath, overwrite, nifPath);
    if (!verify) return std::unexpected(verify.error());
    std::string changed;
    for (const auto block : report.changed) changed += (changed.empty() ? "" : ", ") + std::to_string(block);
    std::string skipped;
    for (const auto& entry : report.skipped) skipped += (skipped.empty() ? "" : "; ") + entry;
    std::string warnings;
    for (const auto& entry : report.warnings) warnings += "\nWarning: " + entry;
    const auto counts = "\nVertices: " + std::to_string(report.vertices_before) + " -> " +
        std::to_string(report.vertices_after) + ", triangles: " + std::to_string(report.triangles_before) + " -> " +
        std::to_string(report.triangles_after);
    const auto displacement =
        report.max_error > 0.0f ? "\nMax vertex displacement: " + std::to_string(report.max_error) : std::string{};
    const auto weights = report.vertices_changed > 0
        ? "\nWeights changed on " + std::to_string(report.vertices_changed) + " vertices, largest change " +
            std::to_string(report.max_weight_change)
        : std::string{};
    return "RESULT: shape op " + std::string(opName) + " OK (in-process)\nSaved: " +
        outputPath.string() + "\nChanged blocks: " + (changed.empty() ? "none" : changed) + "\nSkipped: " +
        (skipped.empty() ? "none" : skipped) + warnings + counts + displacement + weights + detail + "\n" + *verify;
}

Result<std::string> shapeOpInProcess(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath,
                                     const fo4::nif::ShapeOpRequest& request, bool overwrite) {
    std::vector<std::byte> bytes;
    fo4::nif::ShapeOpReport report;
    try {
        bytes = fo4::nif::apply_shape_op(readNif(nifPath), request, &report);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifShapeOpFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifShapeOpFailed", ex.what()});
    }
    return commitShapeOp(nifPath, outputPath, fo4::nif::shape_op_name(request.op), bytes, report, {}, overwrite);
}

Result<fo4::nif::ModelOpRequest> modelOpRequest(fo4::nif::ModelOp op, const std::vector<std::uint32_t>& blocks,
                                                const NifModelOpArgs& model) {
    const std::string name = fo4::nif::model_op_name(op);
    if (blocks.size() != 1) {
        return std::unexpected(Error{"InvalidArgument", name + " needs exactly one block in blocks"});
    }
    const auto missing = [&](const char* what) {
        return std::unexpected(Error{"InvalidArgument", name + " needs " + what});
    };
    fo4::nif::ModelOpRequest request;
    request.op = op;
    request.block = blocks[0];
    switch (op) {
        case fo4::nif::ModelOp::extrude_faces:
            if (model.tris.empty() || !model.distance) return missing("tris and distance");
            request.tris = model.tris;
            request.distance = *model.distance;
            break;
        case fo4::nif::ModelOp::bridge_loops:
            if (model.loopA.empty() || model.loopB.empty()) return missing("loop_a and loop_b");
            request.loop_a = model.loopA;
            request.loop_b = model.loopB;
            break;
        case fo4::nif::ModelOp::knife_plane:
            if (!model.normal || !model.offset) return missing("normal and offset");
            request.normal = *model.normal;
            request.offset = *model.offset;
            break;
        case fo4::nif::ModelOp::cut_loop:
            if (model.edges.empty()) return missing("edges");
            request.edges = model.edges;
            break;
        case fo4::nif::ModelOp::bevel_edges:
            if (model.edges.empty() || !model.width) return missing("edges and width");
            request.edges = model.edges;
            request.width = *model.width;
            break;
    }
    return request;
}

Result<std::string> modelOpInProcess(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath,
                                     const fo4::nif::ModelOpRequest& request, bool overwrite) {
    std::vector<std::byte> bytes;
    fo4::nif::ModelOpReport report;
    try {
        bytes = fo4::nif::apply_model_op(readNif(nifPath), request, &report);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifShapeOpFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifShapeOpFailed", ex.what()});
    }
    fo4::nif::ShapeOpReport shaped;
    shaped.changed = {request.block};
    shaped.vertices_before = report.vertices_before;
    shaped.vertices_after = report.vertices_after;
    shaped.triangles_before = report.triangles_before;
    shaped.triangles_after = report.triangles_after;
    const auto detail = request.op == fo4::nif::ModelOp::knife_plane
        ? "\nKnife pieces: " + std::to_string(report.triangles_added) + " added, " +
            std::to_string(report.degenerate_dropped) + " zero-area dropped"
        : std::string{};
    return commitShapeOp(nifPath, outputPath, fo4::nif::model_op_name(request.op), bytes, shaped, detail, overwrite);
}

std::vector<fo4::nif::Pose> readPoseLibrary(const std::filesystem::path& path) {
    const auto raw = readFile(path);
    return fo4::nif::read_pose_library(std::string_view(raw.data(), raw.size()));
}

std::string poseNames(const std::vector<fo4::nif::Pose>& poses) {
    std::string names;
    for (const auto& pose : poses) names += (names.empty() ? "" : ", ") + pose.name;
    return names.empty() ? "none" : names;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string joined;
    for (const auto& name : names) joined += (joined.empty() ? "" : ", ") + name;
    return joined.empty() ? "none" : joined;
}

// Invariant: a key is `components` numbers, three times that when quadratic (a quaternion key has no tangents) and `components` plus tension, continuity and bias when TBC.
std::size_t keyNumbers(std::uint32_t type, std::size_t components, bool quaternion) {
    if (type == fo4::nif::k_key_quadratic) return quaternion ? components : components * 3;
    if (type == fo4::nif::k_key_tbc) return components + 3;
    return components;
}

// Requires one of the float key blocks; the value bytes are the caller's floats packed little-endian, never interpreted by Radicle.
std::vector<std::byte> replaceKeyChannel(const fo4::nif::NifFile& nif, std::uint32_t block, std::string_view channel,
                                         std::optional<std::uint32_t> interpolation,
                                         const std::vector<NifKeyArg>& keys) {
    auto current = fo4::nif::read_controller_keys(nif, block);
    const auto type = current.block_type;
    if (type == "NiBoolData" || type == "NiTextKeyExtraData") {
        throw std::invalid_argument(type + " keys are not float values, so set_keys does not write them");
    }
    const bool transform = type == "NiTransformData" || type == "NiKeyframeData";
    fo4::nif::KeyChannel* target = nullptr;
    std::size_t components = 1;
    bool quaternion = false;
    bool xyz = false;
    if (transform) {
        if (channel == "translation") {
            target = &current.translations;
            components = 3;
        } else if (channel == "scale") {
            target = &current.scales;
        } else if (channel == "rotation_quat") {
            target = &current.rotation_quat;
            components = 4;
            quaternion = true;
        } else if (channel == "rotation_x" || channel == "rotation_y" || channel == "rotation_z") {
            target = &current.rotation_xyz[static_cast<std::size_t>(channel.back() - 'x')];
            xyz = true;
        }
    } else if (channel == "data") {
        target = &current.data;
        components = type == "NiPosData" ? 3 : 1;
    }
    if (target == nullptr) {
        throw std::invalid_argument("channel '" + std::string(channel) + "' does not exist on " + type + "; use " +
            (transform ? "translation, scale, rotation_quat, rotation_x, rotation_y or rotation_z" : "data"));
    }
    const bool xyzNow = current.rotation_type == fo4::nif::k_key_xyz;
    if (quaternion && xyzNow) {
        throw std::invalid_argument("block " + std::to_string(block) + " uses XYZ rotations; write rotation_x, rotation_y and rotation_z");
    }
    if (xyz && !xyzNow && !current.rotation_quat.keys.empty()) {
        throw std::invalid_argument("block " + std::to_string(block) + " uses quaternion rotations; write rotation_quat");
    }
    const auto kind = interpolation.value_or(quaternion ? current.rotation_type : target->interpolation);
    if (kind == 0) throw std::invalid_argument("set_keys needs interpolation: the channel has no keys to take it from");
    if (kind < fo4::nif::k_key_linear || kind > fo4::nif::k_key_const || kind == fo4::nif::k_key_xyz) {
        throw std::invalid_argument("interpolation must be 1 (linear), 2 (quadratic), 3 (TBC) or 5 (constant)");
    }
    const auto numbers = keyNumbers(kind, components, quaternion);
    std::vector<fo4::nif::KeyRecord> records;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].value.size() != numbers) {
            throw std::invalid_argument("key " + std::to_string(i) + " needs " + std::to_string(numbers) +
                                        " numbers for interpolation " + std::to_string(kind) + " on " + std::string(channel));
        }
        fo4::nif::KeyRecord record;
        record.time = keys[i].time;
        for (const float number : keys[i].value) {
            const auto bits = std::bit_cast<std::uint32_t>(number);
            for (std::size_t shift = 0; shift < 4; ++shift) {
                record.value.push_back(std::byte{static_cast<std::uint8_t>(bits >> (8 * shift))});
            }
        }
        records.push_back(std::move(record));
    }
    target->interpolation = kind;
    target->keys = std::move(records);
    if (quaternion) current.rotation_type = kind;
    if (xyz) current.rotation_type = fo4::nif::k_key_xyz;
    return fo4::nif::write_controller_keys(nif, block, current);
}

Result<std::string> importInProcess(const std::filesystem::path& objPath, const std::filesystem::path& outputPath,
                                    const NifImportOptions& options, bool overwrite) {
    std::ifstream stream(objPath, std::ios::binary);
    if (!stream) return std::unexpected(Error{"FileNotFound", "Could not open OBJ: " + objPath.string()});
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const auto nifOptions = toRadicleImport(options);
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::import_obj(text, nifOptions);
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifImportFailed", ex.what()});
    }
    auto verify = writeVerifiedNif(bytes, outputPath, overwrite, std::nullopt);
    if (!verify) return std::unexpected(verify.error());
    return "RESULT: import OK (in-process)\nSaved: " + outputPath.string() + "\n" + *verify;
}

#ifdef _WIN32
std::filesystem::path openedPath(HANDLE handle) {
    std::vector<wchar_t> text(32768);
    const auto length = GetFinalPathNameByHandleW(handle, text.data(), static_cast<DWORD>(text.size()), FILE_NAME_NORMALIZED);
    if (length == 0 || length >= text.size()) throw std::runtime_error("cannot resolve the glTF buffer path");
    std::wstring_view view(text.data(), length);
    if (view.starts_with(L"\\\\?\\")) view.remove_prefix(4);
    return std::filesystem::path(view);
}

std::unique_ptr<void, BOOL (*)(HANDLE)> openForRead(const std::filesystem::path& path, DWORD flags) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, flags, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("glTF buffer not found: " + path.string());
    return {handle, &CloseHandle};
}

std::vector<std::byte> readBufferInside(const std::filesystem::path& folder, const std::filesystem::path& target) {
    const auto base = openForRead(folder, FILE_FLAG_BACKUP_SEMANTICS);
    const auto file = openForRead(target, FILE_ATTRIBUTE_NORMAL);
    const auto inside = openedPath(file.get()).lexically_relative(openedPath(base.get()));
    if (inside.empty() || *inside.begin() == "..") {
        throw std::runtime_error("glTF buffer '" + target.string() + "' resolves outside the glTF folder");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart > std::numeric_limits<DWORD>::max()) {
        throw std::runtime_error("glTF buffer is too large: " + target.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()) {
        throw std::runtime_error("cannot read glTF buffer: " + target.string());
    }
    return bytes;
}
#else
struct FileDescriptor {
    int value;
    ~FileDescriptor() { if (value >= 0) ::close(value); }
};

std::vector<std::byte> readBufferInside(const std::filesystem::path& folder, const std::filesystem::path& target) {
    const FileDescriptor file{::open(target.c_str(), O_RDONLY)};
    if (file.value < 0) throw std::runtime_error("glTF buffer not found: " + target.string());
    struct stat opened{};
    if (::fstat(file.value, &opened) != 0) throw std::runtime_error("cannot read glTF buffer: " + target.string());
    std::error_code ec;
    const auto resolved = std::filesystem::canonical(target, ec);
    struct stat current{};
    if (ec || ::stat(resolved.c_str(), &current) != 0 || current.st_dev != opened.st_dev || current.st_ino != opened.st_ino) {
        throw std::runtime_error("glTF buffer changed while it was being read: " + target.string());
    }
    const auto inside = resolved.lexically_relative(std::filesystem::canonical(folder));
    if (inside.empty() || *inside.begin() == "..") {
        throw std::runtime_error("glTF buffer '" + target.string() + "' resolves outside the glTF folder");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(opened.st_size));
    std::size_t done = 0;
    while (done < bytes.size()) {
        const auto got = ::read(file.value, bytes.data() + done, bytes.size() - done);
        if (got <= 0) throw std::runtime_error("cannot read glTF buffer: " + target.string());
        done += static_cast<std::size_t>(got);
    }
    return bytes;
}
#endif

bool namesSameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    if (std::filesystem::exists(left, ec) && std::filesystem::exists(right, ec) &&
        std::filesystem::equivalent(left, right, ec)) {
        return true;
    }
    const auto canonicalLeft = std::filesystem::weakly_canonical(left, ec);
    if (ec) return false;
    const auto canonicalRight = std::filesystem::weakly_canonical(right, ec);
    return !ec && canonicalLeft == canonicalRight;
}

fo4::nif::GltfUriLoader bufferLoader(const std::filesystem::path& folder, const std::filesystem::path& outputPath) {
    return [folder, outputPath](std::string_view uri) {
        const std::filesystem::path relative{std::string(uri)};
        if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
            throw std::runtime_error("glTF buffer '" + std::string(uri) + "' is not a relative path");
        }
        for (const auto& part : relative) {
            if (part == "..") throw std::runtime_error("glTF buffer '" + std::string(uri) + "' climbs out of its folder with '..'");
        }
        const auto base = std::filesystem::weakly_canonical(folder);
        const auto target = std::filesystem::weakly_canonical(folder / relative);
        const auto inside = target.lexically_relative(base);
        if (inside.empty() || *inside.begin() == "..") {
            throw std::runtime_error("glTF buffer '" + std::string(uri) + "' resolves outside the glTF folder");
        }
        if (!std::filesystem::is_regular_file(target)) {
            throw std::runtime_error("glTF buffer not found: " + target.string());
        }
        if (namesSameFile(target, outputPath)) {
            throw std::runtime_error("output NIF '" + outputPath.string() + "' is the glTF buffer '" + target.string() + "'");
        }
        return readBufferInside(folder, target);
    };
}

Result<std::string> importGltfInProcess(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath,
                                        const NifImportOptions& options, bool overwrite) {
    if (namesSameFile(outputPath, gltfPath)) {
        return std::unexpected(Error{"InvalidPath", "Output NIF path is the glTF source: " + outputPath.string()});
    }
    if (!options.skeleton.empty() && namesSameFile(outputPath, options.skeleton)) {
        return std::unexpected(Error{"InvalidPath", "Output NIF path is the skeleton NIF: " + outputPath.string()});
    }
    const auto raw = readFile(gltfPath);
    auto nifOptions = toRadicleImport(options);
    if (!options.skeleton.empty()) {
        const auto skeleton = readFile(options.skeleton);
        const auto bytes = std::as_bytes(std::span(skeleton));
        nifOptions.skeleton.assign(bytes.begin(), bytes.end());
    }
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::import_gltf(std::as_bytes(std::span(raw)), nifOptions, bufferLoader(gltfPath.parent_path(), outputPath));
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifImportFailed", ex.what()});
    }
    auto verify = writeVerifiedNif(bytes, outputPath, overwrite, std::nullopt);
    if (!verify) return std::unexpected(verify.error());
    return "RESULT: import glTF OK (in-process)\nSaved: " + outputPath.string() + "\n" + *verify;
}

Result<std::string> fixInProcess(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath,
                                 std::string_view check, bool overwrite) {
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::fix_nif(readNif(nifPath), check);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifFixFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifFixFailed", ex.what()});
    }
    auto verify = writeVerifiedNif(bytes, outputPath, overwrite, nifPath);
    if (!verify) return std::unexpected(verify.error());
    return "RESULT: fix OK (in-process)\nSaved: " + outputPath.string() + "\n" + *verify;
}

}  // namespace

Result<void> NifService::requireFile(const std::filesystem::path& path, std::string_view label) const {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", std::string(label) + " path is empty"});
    if (!std::filesystem::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", std::string(label) + " not found: " + path.string()});
    }
    return {};
}

Result<std::string> NifService::inspect(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderInspect(nifPath);
}

Result<std::string> NifService::geometry(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderGeometry(nifPath);
}

Result<std::string> NifService::verify(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderVerifyJson(nifPath);
}

Result<std::string> NifService::tree(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderTree(nifPath);
}

Result<std::string> NifService::segments(const std::filesystem::path& nifPath, std::uint32_t block) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return renderSegments(nifPath, block);
}

Result<std::string> NifService::fix(const std::filesystem::path& nifPath,
                                    const std::filesystem::path& outputPath, std::string_view check,
                                    bool overwrite) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    if (auto absent = refuseExisting(outputPath, overwrite); !absent) return std::unexpected(absent.error());
    return fixInProcess(nifPath, outputPath, check, overwrite);
}

Result<std::string> NifService::importObj(const std::filesystem::path& objPath,
                                          const std::filesystem::path& outputPath,
                                          const NifImportOptions& options,
                                          bool overwrite) const {
    if (auto valid = requireFile(objPath, "OBJ"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    if (auto absent = refuseExisting(outputPath, overwrite); !absent) return std::unexpected(absent.error());
    return importInProcess(objPath, outputPath, options, overwrite);
}

Result<std::string> NifService::importGltf(const std::filesystem::path& gltfPath,
                                           const std::filesystem::path& outputPath,
                                           const NifImportOptions& options,
                                           bool overwrite) const {
    if (auto valid = requireFile(gltfPath, "glTF"); !valid) return std::unexpected(valid.error());
    if (!options.skeleton.empty()) {
        if (auto valid = requireFile(options.skeleton, "skeleton NIF"); !valid) return std::unexpected(valid.error());
    }
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    if (auto absent = refuseExisting(outputPath, overwrite); !absent) return std::unexpected(absent.error());
    return importGltfInProcess(gltfPath, outputPath, options, overwrite);
}

Result<std::string> NifService::applyEdits(const std::filesystem::path& nifPath,
                                           const nlohmann::json& edits,
                                           const std::filesystem::path& outputPath,
                                           std::string_view expectedSource) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (edits.empty()) return std::unexpected(Error{"NoEdits", "No NIF edits to apply"});

    const auto target = outputPath.empty() ? nifPath : outputPath;
    std::vector<std::byte> bytes;
    try {
        const auto raw = readFile(nifPath);
        const auto source = std::as_bytes(std::span(raw));
        if (auto unchanged = requireUnchangedSource(nifPath, source, expectedSource); !unchanged) {
            return std::unexpected(unchanged.error());
        }
        bytes = fo4::nif::apply_edits_json(fo4::nif::read_nif(source), edits.dump());
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeVerifiedNif(bytes, target, true, nifPath); !committed) {
        return std::unexpected(committed.error());
    }
    return "set OK (in-process)\nSaved: " + target.string();
}

Result<std::string> NifService::createTextureSet(const std::filesystem::path& nifPath,
                                                 std::uint32_t block,
                                                 const std::vector<std::string>& textures,
                                                 const std::filesystem::path& outputPath,
                                                 std::string_view expectedSource,
                                                 bool overwrite) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());

    const auto target = outputPath.empty() ? nifPath : outputPath;
    const bool replace = overwrite || isInPlaceSave(nifPath, outputPath);
    if (auto absent = refuseExisting(target, replace); !absent) return std::unexpected(absent.error());
    std::vector<std::byte> bytes;
    try {
        const auto raw = readFile(nifPath);
        const auto source = std::as_bytes(std::span(raw));
        if (auto unchanged = requireUnchangedSource(nifPath, source, expectedSource); !unchanged) {
            return std::unexpected(unchanged.error());
        }
        bytes = fo4::nif::create_texture_set(fo4::nif::read_nif(source), block, textures);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeVerifiedNif(bytes, target, replace, nifPath); !committed) {
        return std::unexpected(committed.error());
    }
    return "texture set OK (in-process)\nSaved: " + target.string();
}

Result<std::string> NifService::attachAlpha(const std::filesystem::path& nifPath,
                                            std::uint32_t block,
                                            std::uint16_t flags,
                                            std::uint8_t threshold,
                                            const std::filesystem::path& outputPath,
                                            std::string_view expectedSource,
                                            bool overwrite) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());

    const auto target = outputPath.empty() ? nifPath : outputPath;
    const bool replace = overwrite || isInPlaceSave(nifPath, outputPath);
    if (auto absent = refuseExisting(target, replace); !absent) return std::unexpected(absent.error());
    std::vector<std::byte> bytes;
    try {
        const auto raw = readFile(nifPath);
        const auto source = std::as_bytes(std::span(raw));
        if (auto unchanged = requireUnchangedSource(nifPath, source, expectedSource); !unchanged) {
            return std::unexpected(unchanged.error());
        }
        bytes = fo4::nif::attach_alpha_property(fo4::nif::read_nif(source), block, flags, threshold);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeVerifiedNif(bytes, target, replace, nifPath); !committed) {
        return std::unexpected(committed.error());
    }
    return "attach alpha OK (in-process)\nSaved: " + target.string();
}

Result<std::string> NifService::removeBlock(const std::filesystem::path& nifPath,
                                            std::uint32_t block,
                                            const std::filesystem::path& outputPath,
                                            std::string_view expectedSource,
                                            bool overwrite) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());

    const auto target = outputPath.empty() ? nifPath : outputPath;
    const bool replace = overwrite || isInPlaceSave(nifPath, outputPath);
    if (auto absent = refuseExisting(target, replace); !absent) return std::unexpected(absent.error());
    std::string removed;
    try {
        const auto raw = readFile(nifPath);
        const auto source = std::as_bytes(std::span(raw));
        if (auto unchanged = requireUnchangedSource(nifPath, source, expectedSource); !unchanged) {
            return std::unexpected(unchanged.error());
        }
        const auto removal = fo4::nif::remove_block(fo4::nif::read_nif(source), block);
        removed = std::to_string(block);
        for (const auto dependent : removal.removed) removed += ", " + std::to_string(dependent);
        if (auto committed = writeVerifiedNif(removal.bytes, target, replace, nifPath); !committed) {
            return std::unexpected(committed.error());
        }
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    return "remove block OK (in-process)\nSaved: " + target.string() + "\nRemoved: " + removed;
}

Result<std::string> NifService::blockOp(const std::filesystem::path& nifPath,
                                        std::string_view op,
                                        std::optional<std::uint32_t> block,
                                        std::optional<std::uint32_t> child,
                                        const std::filesystem::path& donorPath,
                                        const std::vector<std::uint32_t>& blocks,
                                        const std::filesystem::path& outputPath,
                                        std::string_view expectedSource,
                                        bool overwrite,
                                        const std::vector<std::vector<std::uint32_t>>& groups,
                                        const std::filesystem::path& posePath,
                                        std::string_view pose,
                                        std::string_view channel,
                                        std::optional<std::uint32_t> interpolation,
                                        const std::vector<NifKeyArg>& keys) const {
    if (op != "duplicate_block" && op != "append_child" && op != "sort_children_by_name" &&
        op != "merge_nif_into" && op != "bake_shape_transforms" && op != "join_shapes" &&
        op != "separate_shape" && op != "separate_by_bone" && op != "apply_pose" && op != "set_keys") {
        return std::unexpected(Error{"InvalidBlockOp", "Unknown block op '" + std::string(op) +
            "'. Valid ops: duplicate_block, append_child, sort_children_by_name, merge_nif_into, bake_shape_transforms, "
            "join_shapes, separate_shape, separate_by_bone, apply_pose, set_keys"});
    }
    const bool bake = op == "bake_shape_transforms";
    const bool join = op == "join_shapes";
    if (!bake && !join && !block) return std::unexpected(Error{"InvalidArgument", std::string(op) + " needs block"});
    if (op == "append_child" && !child) return std::unexpected(Error{"InvalidArgument", "append_child needs child"});
    if (op == "merge_nif_into" && donorPath.empty()) {
        return std::unexpected(Error{"InvalidArgument", "merge_nif_into needs donor_path"});
    }
    if ((bake || join) && blocks.empty()) return std::unexpected(Error{"InvalidArgument", std::string(op) + " needs blocks"});
    if (op == "separate_shape" && groups.empty()) return std::unexpected(Error{"InvalidArgument", "separate_shape needs groups"});
    if (op == "apply_pose" && (posePath.empty() || pose.empty())) {
        return std::unexpected(Error{"InvalidArgument", "apply_pose needs pose_path and pose"});
    }
    if (op == "set_keys" && (channel.empty() || keys.empty())) {
        return std::unexpected(Error{"InvalidArgument", "set_keys needs channel and keys"});
    }
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (op == "merge_nif_into") {
        if (auto valid = requireFile(donorPath, "Donor NIF"); !valid) return std::unexpected(valid.error());
    }
    if (op == "apply_pose") {
        if (auto valid = requireFile(posePath, "Pose library"); !valid) return std::unexpected(valid.error());
    }

    const auto target = outputPath.empty() ? nifPath : outputPath;
    const bool replace = overwrite || isInPlaceSave(nifPath, outputPath);
    if (auto absent = refuseExisting(target, replace); !absent) return std::unexpected(absent.error());
    std::vector<std::byte> bytes;
    std::string detail;
    try {
        const auto raw = readFile(nifPath);
        const auto source = std::as_bytes(std::span(raw));
        if (auto unchanged = requireUnchangedSource(nifPath, source, expectedSource); !unchanged) {
            return std::unexpected(unchanged.error());
        }
        const auto nif = fo4::nif::read_nif(source);
        if (op == "duplicate_block") {
            bytes = fo4::nif::duplicate_block(nif, *block);
        } else if (op == "append_child") {
            bytes = fo4::nif::append_child(nif, *block, *child);
        } else if (op == "sort_children_by_name") {
            bytes = fo4::nif::sort_children_by_name(nif, *block);
        } else if (op == "merge_nif_into") {
            bytes = fo4::nif::merge_nif_into(nif, readNif(donorPath), *block);
        } else if (join) {
            fo4::nif::JoinReport report;
            bytes = fo4::nif::join_shapes(nif, fo4::nif::JoinRequest{blocks}, &report);
            detail = "\nTarget: " + std::to_string(report.target) + "\nRemoved: ";
            for (const auto removed : report.removed) detail += std::to_string(removed) + (removed == report.removed.back() ? "" : ", ");
            detail += "\nVertices: " + std::to_string(report.vertices_before) + " -> " + std::to_string(report.vertices_after) +
                      "\nTriangles: " + std::to_string(report.triangles_before) + " -> " + std::to_string(report.triangles_after);
        } else if (op == "separate_shape" || op == "separate_by_bone") {
            fo4::nif::SeparateReport report;
            bytes = op == "separate_shape" ? fo4::nif::separate_shape(nif, fo4::nif::SeparateRequest{*block, groups}, &report)
                                           : fo4::nif::separate_by_bone(nif, *block, &report);
            for (const auto shape : report.blocks) detail += (detail.empty() ? "\nShapes: " : ", ") + std::to_string(shape);
        } else if (op == "apply_pose") {
            const auto poses = readPoseLibrary(posePath);
            const auto found = std::find_if(poses.begin(), poses.end(), [&](const fo4::nif::Pose& entry) { return entry.name == pose; });
            if (found == poses.end()) {
                return std::unexpected(Error{"NifEditFailed", "Pose '" + std::string(pose) + "' is not in the library. Poses: " + poseNames(poses)});
            }
            fo4::nif::PoseApplyReport report;
            bytes = fo4::nif::apply_pose_to_skin(nif, *block, *found, &report);
            detail = "\nApplied: " + std::to_string(report.applied) + "\nUnknown pose bones: " +
                     joinNames(report.unknown_pose_bones) + "\nUnposed skin bones: " + joinNames(report.unposed_bones);
            if (report.applied == 0) return std::unexpected(Error{"NifEditFailed", "Nothing posed." + detail});
        } else if (op == "set_keys") {
            bytes = replaceKeyChannel(nif, *block, channel, interpolation, keys);
            detail = "\nChannel: " + std::string(channel) + "\nKeys: " + std::to_string(keys.size());
        } else {
            fo4::nif::BakeReport report;
            bytes = fo4::nif::bake_shape_transforms(nif, blocks, &report);
            for (const auto baked : report.baked) detail += (detail.empty() ? "\nBaked: " : ", ") + std::to_string(baked);
            for (const auto& skipped : report.skipped) detail += "\nSkipped: " + skipped;
            if (report.baked.empty()) {
                return std::unexpected(Error{"NifEditFailed", "Nothing baked." + detail});
            }
        }
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeVerifiedNif(bytes, target, replace, nifPath); !committed) {
        return std::unexpected(committed.error());
    }
    return std::string(op) + " OK (in-process)\nSaved: " + target.string() + detail;
}

Result<std::string> NifService::listPoses(const std::filesystem::path& posePath, const std::filesystem::path& nifPath,
                                          std::optional<std::uint32_t> block) const {
    if (nifPath.empty() != !block) {
        return std::unexpected(Error{"InvalidArgument", "path and block go together: give both to dry-run the poses against a skin, or neither"});
    }
    if (auto valid = requireFile(posePath, "Pose library"); !valid) return std::unexpected(valid.error());
    try {
        const auto poses = readPoseLibrary(posePath);
        std::optional<fo4::nif::NifFile> nif;
        if (block) {
            if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
            nif = readNif(nifPath);
        }
        auto list = nlohmann::json::array();
        for (const auto& pose : poses) {
            nlohmann::json entry{{"name", pose.name}, {"bones", pose.bones.size()}};
            if (nif) {
                try {
                    fo4::nif::PoseApplyReport report;
                    fo4::nif::apply_pose_to_skin(*nif, *block, pose, &report);
                    entry["applied"] = report.applied;
                    entry["unknown_pose_bones"] = report.unknown_pose_bones;
                    entry["unposed_bones"] = report.unposed_bones;
                } catch (const fo4::parse_error& error) {
                    entry["error"] = error.parse_message();
                } catch (const std::exception& ex) {
                    entry["error"] = ex.what();
                }
            }
            list.push_back(std::move(entry));
        }
        return nlohmann::json{{"poses", std::move(list)}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifReadFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifReadFailed", ex.what()});
    }
}

Result<std::string> NifService::shapeOp(const std::filesystem::path& nifPath,
                                        const std::filesystem::path& outputPath,
                                        std::string_view op,
                                        std::vector<std::uint32_t> blocks,
                                        float creaseDegrees,
                                        bool overwrite,
                                        float ratio,
                                        std::uint32_t maxInfluences,
                                        float pruneThreshold,
                                        std::optional<std::uint32_t> sourceBlock,
                                        const NifModelOpArgs& model) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    if (const auto modelOp = fo4::nif::parse_model_op(op)) {
        auto request = modelOpRequest(*modelOp, blocks, model);
        if (!request) return std::unexpected(request.error());
        if (auto absent = refuseExisting(outputPath, overwrite); !absent) return std::unexpected(absent.error());
        return modelOpInProcess(nifPath, outputPath, *request, overwrite);
    }
    const auto parsed = fo4::nif::parse_shape_op(op);
    if (!parsed) {
        return std::unexpected(Error{"InvalidShapeOp", "Unknown shape op '" + std::string(op) +
            "'. Valid ops: recalc_normals, recalc_tangents, flip_normals, flip_winding, apply_transform, refresh_bounds, weld_vertices, remove_degenerate_triangles, remove_unused_vertices, simplify, sanitize, normalize_weights, limit_influences, prune_weights, copy_weights, extrude_faces, bridge_loops, knife_plane, cut_loop, bevel_edges"});
    }
    if (auto absent = refuseExisting(outputPath, overwrite); !absent) return std::unexpected(absent.error());
    return shapeOpInProcess(nifPath, outputPath,
                            fo4::nif::ShapeOpRequest{*parsed, std::move(blocks), creaseDegrees, ratio, maxInfluences,
                                                     pruneThreshold, sourceBlock},
                            overwrite);
}

Result<std::string> NifService::connectPoints(const std::filesystem::path& nifPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    try {
        return connectPointsJson(readNif(nifPath)).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifReadFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifReadFailed", ex.what()});
    }
}

Result<std::string> NifService::influences(const std::filesystem::path& nifPath, std::uint32_t block,
                                           std::uint32_t vertex) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    try {
        return influencesJson(readNif(nifPath), block, vertex)
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifReadFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifReadFailed", ex.what()});
    }
}

Result<std::string> NifService::influencedBy(const std::filesystem::path& nifPath, std::uint32_t block,
                                             std::int32_t bone, float minimum) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    try {
        return influencedByJson(readNif(nifPath), block, bone, minimum)
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifReadFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifReadFailed", ex.what()});
    }
}

Result<std::string> NifService::validateSkin(const std::filesystem::path& nifPath, std::uint32_t block) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    try {
        return skinReportJson(readNif(nifPath), block)
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifReadFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifReadFailed", ex.what()});
    }
}

Result<std::string> NifService::applyConnectPoints(const std::filesystem::path& nifPath,
                                                   const std::string& opsText,
                                                   const std::filesystem::path& outputPath,
                                                   bool overwrite) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    const bool replace = overwrite || isInPlaceSave(nifPath, outputPath);
    if (auto absent = refuseExisting(outputPath, replace); !absent) return std::unexpected(absent.error());
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::apply_connect_point_edits_json(readNif(nifPath), opsText);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeVerifiedNif(bytes, outputPath, replace, nifPath); !committed) {
        return std::unexpected(committed.error());
    }
    return "connect points OK (in-process)\nSaved: " + outputPath.string();
}

std::optional<NifCollisionShape> parseCollisionShape(std::string_view text) {
    if (text == "none") return NifCollisionShape::none;
    if (text == "box") return NifCollisionShape::box;
    if (text == "hull") return NifCollisionShape::hull;
    if (text == "mesh") return NifCollisionShape::mesh;
    return std::nullopt;
}

}
