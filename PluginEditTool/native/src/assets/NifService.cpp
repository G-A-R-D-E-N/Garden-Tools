#include <fo4ide/assets/NifService.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/nif/connect_points.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4/nif/nif_fix.hpp>
#include <fo4/nif/nif_import.hpp>
#include <fo4/nif/nif_write.hpp>
#include <fo4/nif/shape_ops.hpp>
#include <fo4/nif/structure.hpp>
#include <fo4ide/assets/NifRender.hpp>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

Result<void> writeNif(const std::vector<std::byte>& bytes, const std::filesystem::path& target) {
    try {
        fo4::io::write_file_atomic(target, bytes);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifEditFailed", error.what()});
    }
    return {};
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

nlohmann::json connectPointsJson(const fo4::nif::NifFile& nif) {
    auto blocks = nlohmann::json::array();
    for (const auto block : fo4::nif::connect_point_blocks(nif)) {
        const auto& type = nif.blocks()[block].type;
        if (type == fo4::nif::k_connect_parents_type) {
            const auto value = fo4::nif::read_connect_parents(nif, block);
            auto points = nlohmann::json::array();
            for (const auto& point : value.points) {
                const auto world = fo4::nif::connect_point_world_position(nif, point);
                points.push_back({{"parent", point.parent}, {"name", point.name},
                                  {"rotation", floats(point.rotation, 4)},
                                  {"translation", floats(point.translation, 3)}, {"scale", displayFloat(point.scale)},
                                  {"world", floats(world.data(), world.size())}});
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

Result<std::string> shapeOpInProcess(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath,
                                     const fo4::nif::ShapeOpRequest& request) {
    std::vector<std::byte> bytes;
    fo4::nif::ShapeOpReport report;
    try {
        bytes = fo4::nif::apply_shape_op(readNif(nifPath), request, &report);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifShapeOpFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifShapeOpFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, outputPath); !committed) {
        return std::unexpected(committed.error());
    }
    std::string changed;
    for (const auto block : report.changed) changed += (changed.empty() ? "" : ", ") + std::to_string(block);
    std::string skipped;
    for (const auto& entry : report.skipped) skipped += (skipped.empty() ? "" : "; ") + entry;
    const auto verify = renderVerify(outputPath);
    const auto counts = "\nVertices: " + std::to_string(report.vertices_before) + " -> " +
        std::to_string(report.vertices_after) + ", triangles: " + std::to_string(report.triangles_before) + " -> " +
        std::to_string(report.triangles_after);
    const auto displacement =
        report.max_error > 0.0f ? "\nMax vertex displacement: " + std::to_string(report.max_error) : std::string{};
    const auto weights = report.vertices_changed > 0
        ? "\nWeights changed on " + std::to_string(report.vertices_changed) + " vertices, largest change " +
            std::to_string(report.max_weight_change)
        : std::string{};
    return "RESULT: shape op " + std::string(fo4::nif::shape_op_name(request.op)) + " OK (in-process)\nSaved: " +
        outputPath.string() + "\nChanged blocks: " + (changed.empty() ? "none" : changed) + "\nSkipped: " +
        (skipped.empty() ? "none" : skipped) + counts + displacement + weights + "\n" +
        (verify ? *verify : verify.error().message);
}

Result<std::string> importInProcess(const std::filesystem::path& objPath, const std::filesystem::path& outputPath,
                                    const NifImportOptions& options) {
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
    if (auto committed = writeNif(bytes, outputPath); !committed) {
        return std::unexpected(committed.error());
    }
    const auto verify = renderVerify(outputPath);
    return "RESULT: import OK (in-process)\nSaved: " + outputPath.string() + "\n" +
        (verify ? *verify : verify.error().message);
}

fo4::nif::GltfUriLoader bufferLoader(const std::filesystem::path& folder) {
    return [folder](std::string_view uri) {
        const std::filesystem::path relative{std::string(uri)};
        if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
            throw std::runtime_error("glTF buffer '" + std::string(uri) + "' is not a relative path");
        }
        const auto target = folder / relative;
        if (!std::filesystem::is_regular_file(target)) {
            throw std::runtime_error("glTF buffer not found: " + target.string());
        }
        const auto raw = readFile(target);
        const auto bytes = std::as_bytes(std::span(raw));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
}

Result<std::string> importGltfInProcess(const std::filesystem::path& gltfPath, const std::filesystem::path& outputPath,
                                        const NifImportOptions& options) {
    const auto raw = readFile(gltfPath);
    auto nifOptions = toRadicleImport(options);
    if (!options.skeleton.empty()) {
        const auto skeleton = readFile(options.skeleton);
        const auto bytes = std::as_bytes(std::span(skeleton));
        nifOptions.skeleton.assign(bytes.begin(), bytes.end());
    }
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::import_gltf(std::as_bytes(std::span(raw)), nifOptions, bufferLoader(gltfPath.parent_path()));
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifImportFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, outputPath); !committed) {
        return std::unexpected(committed.error());
    }
    const auto verify = renderVerify(outputPath);
    return "RESULT: import glTF OK (in-process)\nSaved: " + outputPath.string() + "\n" +
        (verify ? *verify : verify.error().message);
}

Result<std::string> fixInProcess(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath,
                                 std::string_view check) {
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::fix_nif(readNif(nifPath), check);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifFixFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifFixFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, outputPath); !committed) {
        return std::unexpected(committed.error());
    }
    const auto verify = renderVerify(outputPath);
    return "RESULT: fix OK (in-process)\nSaved: " + outputPath.string() + "\n" +
        (verify ? *verify : verify.error().message);
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
                                    const std::filesystem::path& outputPath, std::string_view check) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    return fixInProcess(nifPath, outputPath, check);
}

Result<std::string> NifService::importObj(const std::filesystem::path& objPath,
                                          const std::filesystem::path& outputPath,
                                          const NifImportOptions& options) const {
    if (auto valid = requireFile(objPath, "OBJ"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    return importInProcess(objPath, outputPath, options);
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
    std::error_code present;
    if (!overwrite && std::filesystem::exists(outputPath, present)) {
        return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + outputPath.string()});
    }
    return importGltfInProcess(gltfPath, outputPath, options);
}

Result<std::string> NifService::applyEdits(const std::filesystem::path& nifPath,
                                           const nlohmann::json& edits,
                                           const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (edits.empty()) return std::unexpected(Error{"NoEdits", "No NIF edits to apply"});

    const auto target = outputPath.empty() ? nifPath : outputPath;
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::apply_edits_json(readNif(nifPath), edits.dump());
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, target); !committed) {
        return std::unexpected(committed.error());
    }
    return "set OK (in-process)\nSaved: " + target.string();
}

Result<std::string> NifService::createTextureSet(const std::filesystem::path& nifPath,
                                                 std::uint32_t block,
                                                 const std::vector<std::string>& textures,
                                                 const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());

    const auto target = outputPath.empty() ? nifPath : outputPath;
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::create_texture_set(readNif(nifPath), block, textures);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, target); !committed) {
        return std::unexpected(committed.error());
    }
    return "texture set OK (in-process)\nSaved: " + target.string();
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
                                        std::optional<std::uint32_t> sourceBlock) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    const auto parsed = fo4::nif::parse_shape_op(op);
    if (!parsed) {
        return std::unexpected(Error{"InvalidShapeOp", "Unknown shape op '" + std::string(op) +
            "'. Valid ops: recalc_normals, recalc_tangents, flip_normals, flip_winding, apply_transform, refresh_bounds, weld_vertices, remove_degenerate_triangles, remove_unused_vertices, simplify, sanitize, normalize_weights, limit_influences, prune_weights, copy_weights"});
    }
    std::error_code present;
    if (!overwrite && std::filesystem::exists(outputPath, present)) {
        return std::unexpected(Error{"OutputExists", "Refusing to replace an existing file without overwrite: " + outputPath.string()});
    }
    return shapeOpInProcess(nifPath, outputPath,
                            fo4::nif::ShapeOpRequest{*parsed, std::move(blocks), creaseDegrees, ratio, maxInfluences,
                                                     pruneThreshold, sourceBlock});
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

Result<std::string> NifService::applyConnectPoints(const std::filesystem::path& nifPath,
                                                   const std::string& opsText,
                                                   const std::filesystem::path& outputPath) const {
    if (auto valid = requireFile(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    if (outputPath.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    std::vector<std::byte> bytes;
    try {
        bytes = fo4::nif::apply_connect_point_edits_json(readNif(nifPath), opsText);
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifEditFailed", error.parse_message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"NifEditFailed", ex.what()});
    }
    if (auto committed = writeNif(bytes, outputPath); !committed) {
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
