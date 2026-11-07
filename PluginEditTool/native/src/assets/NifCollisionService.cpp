#include <fo4ide/assets/NifCollisionService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/error.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4/nif/collision.hpp>
#include <fo4/nif/collision_attach.hpp>
#include <fo4/nif/collision_lint.hpp>
#include <fo4/nif/hknp_decode.hpp>
#include <fo4/nif/hknp_encode.hpp>
#include <fo4/nif/hknp_hull.hpp>
#include <fo4/nif/hknp_packfile.hpp>
#include <fo4/nif/nif.hpp>
#include <fo4ide/assets/NifRender.hpp>

namespace fo4ide::assets {
namespace {

namespace fs = std::filesystem;
using fo4::nif::HknpShape;
using fo4::nif::HknpShapeKind;
using fo4::nif::HknpSystem;
using fo4::nif::HknpTriangle;
using fo4::nif::HknpVec3;
using fo4::nif::NifFile;
using fo4::nif::Transform;

constexpr std::uint32_t k_collision_material_crc = 0x064003D4U;
constexpr float k_min_box_half_extent = 0.01f;
constexpr std::uint32_t k_layer_mask = 0xFFU;

template <class Fn>
Result<std::string> guarded(Fn&& fn) {
    try {
        return fn();
    } catch (const fo4::parse_error& error) {
        return std::unexpected(Error{"NifCollisionFailed", error.parse_message()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"NifCollisionFailed", error.what()});
    }
}

std::string dumpJson(const nlohmann::json& value) {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

NifFile readNif(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open NIF: " + path.string());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return fo4::nif::read_nif(std::as_bytes(std::span(raw)));
}

Result<void> requireNif(const fs::path& path, std::string_view label) {
    if (path.empty()) return std::unexpected(Error{"InvalidPath", std::string(label) + " path is empty"});
    if (!fs::is_regular_file(path)) {
        return std::unexpected(Error{"FileNotFound", std::string(label) + " not found: " + path.string()});
    }
    return {};
}

bool isSourceFile(const fs::path& source, const fs::path& target) {
    std::error_code ec;
    if (fs::exists(target, ec) && fs::equivalent(source, target, ec)) return true;
    return fs::weakly_canonical(source, ec) == fs::weakly_canonical(target, ec);
}

Result<void> saveTarget(const fs::path& source, const fs::path& target) {
    if (auto valid = requireNif(source, "NIF"); !valid) return valid;
    if (target.empty()) return std::unexpected(Error{"InvalidPath", "Output NIF path is empty"});
    if (isSourceFile(source, target)) {
        return std::unexpected(Error{"NifCollisionRefused",
                                     "Refused: out_nif is the source NIF; choose a Save As path so the source is not overwritten"});
    }
    return {};
}

void commitNif(const std::vector<std::byte>& bytes, const fs::path& target) {
    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {
        auto backup = target;
        backup += ".bak";
        if (!fs::exists(backup, ec)) fs::copy_file(target, backup);
    }
    fo4::io::write_file_atomic(target, bytes);
}

std::string saved(std::string_view title, const std::vector<std::byte>& bytes, const fs::path& target) {
    commitNif(bytes, target);
    const auto verify = renderVerify(target);
    return std::string(title) + "\nSaved: " + target.string() + "\n" + (verify ? *verify : verify.error().message);
}

Transform compose(const Transform& outer, const Transform& inner) {
    Transform out;
    for (std::size_t i = 0; i < 3; ++i) {
        float offset = 0.0f;
        for (std::size_t k = 0; k < 3; ++k) offset += outer.r[i * 3 + k] * inner.t[k];
        out.t[i] = outer.s * offset + outer.t[i];
    }
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            float value = 0.0f;
            for (std::size_t k = 0; k < 3; ++k) value += outer.r[i * 3 + k] * inner.r[k * 3 + j];
            out.r[i * 3 + j] = value;
        }
    }
    out.s = outer.s * inner.s;
    return out;
}

std::optional<Transform> relativeTransform(const NifFile& nif, std::uint32_t block, std::uint32_t ancestor) {
    Transform relative;
    std::int32_t current = static_cast<std::int32_t>(block);
    for (std::size_t steps = 0; current >= 0 && steps <= nif.blocks().size(); ++steps) {
        if (static_cast<std::uint32_t>(current) == ancestor) return relative;
        relative = compose(fo4::nif::block_transform(nif, static_cast<std::uint32_t>(current)), relative);
        current = nif.parent(static_cast<std::uint32_t>(current));
    }
    return std::nullopt;
}

std::string_view nodeName(const NifFile& nif, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= nif.nodes().size()) return {};
    return nif.nodes()[static_cast<std::size_t>(index)].name;
}

std::uint32_t nodeBlock(const NifFile& nif, std::string_view name) {
    for (const auto& node : nif.nodes()) {
        if (node.name == name) return node.block;
    }
    throw std::invalid_argument("no node named '" + std::string(name) + "' in the NIF");
}

std::string hex(std::uint32_t value) {
    static constexpr std::string_view digits = "0123456789ABCDEF";
    std::string out = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) out += digits[(value >> static_cast<unsigned>(shift)) & 0xFU];
    return out;
}

nlohmann::json hexList(const std::set<std::uint32_t>& values) {
    auto out = nlohmann::json::array();
    for (const auto value : values) out.push_back(hex(value));
    return out;
}

std::string_view kindName(HknpShapeKind kind) {
    switch (kind) {
        case HknpShapeKind::convex: return "convex";
        case HknpShapeKind::sphere: return "sphere";
        case HknpShapeKind::capsule: return "capsule";
        case HknpShapeKind::scaled_convex: return "scaled_convex";
        case HknpShapeKind::compound: return "compound";
        case HknpShapeKind::compressed_mesh: return "compressed_mesh";
    }
    return "unknown";
}

std::string_view issueName(fo4::nif::CollisionIssue issue) {
    using fo4::nif::CollisionIssue;
    switch (issue) {
        case CollisionIssue::dangling_object: return "dangling_object";
        case CollisionIssue::system_without_object: return "system_without_object";
        case CollisionIssue::body_out_of_range: return "body_out_of_range";
        case CollisionIssue::layer_zero: return "layer_zero";
        case CollisionIssue::bsx_mismatch: return "bsx_mismatch";
        case CollisionIssue::one_child_compound: return "one_child_compound";
        case CollisionIssue::unsupported_shape: return "unsupported_shape";
        case CollisionIssue::unread_atom: return "unread_atom";
        case CollisionIssue::undecodable_system: return "undecodable_system";
    }
    return "unknown";
}

std::size_t vertexCount(const HknpShape& shape) {
    return shape.kind == HknpShapeKind::compressed_mesh ? shape.mesh_vertices.size() : shape.vertices.size();
}

std::size_t triangleCount(const HknpShape& shape) {
    if (shape.kind == HknpShapeKind::compressed_mesh) return shape.mesh_triangles.size();
    std::size_t count = 0;
    for (const auto& face : shape.faces) count += face.size() >= 3 ? face.size() - 2 : 0;
    return count;
}

std::vector<std::string> havokLayerNames(std::span<const fo4::nif::HavokLayerRow> layers) {
    std::vector<fo4::nif::HavokLayerRow> table;
    for (const auto& layer : layers) {
        if (layer.index > k_layer_mask) continue;
        for (auto slot = table.size(); slot <= layer.index; ++slot) {
            table.push_back({static_cast<std::uint32_t>(slot), {}});
        }
        table[layer.index].name = layer.name;
    }
    return fo4::nif::build_havok_layer_names(table);
}

nlohmann::json bodyJson(const HknpSystem& system, std::size_t index, const std::vector<std::string>& layerNames) {
    const auto& body = system.bodies[index];
    std::set<std::uint32_t> crcs{body.resolved_material_crc};
    nlohmann::json shape{{"kind", "unsupported"}, {"class", ""}, {"vertices", 0}, {"triangles", 0}};
    if (body.shape >= 0 && static_cast<std::size_t>(body.shape) < system.shapes.size()) {
        const auto& source = system.shapes[static_cast<std::size_t>(body.shape)];
        shape = {{"kind", kindName(source.kind)}, {"class", source.class_name},
                 {"vertices", vertexCount(source)}, {"triangles", triangleCount(source)}};
        crcs.insert(source.material_crc);
        for (const auto& triangle : source.mesh_triangles) crcs.insert(triangle.material_crc);
    }
    const bool hasMass = body.dynamic && body.motion_index < system.inertias.size();
    const auto layer = body.packed_filter & k_layer_mask;
    return {{"index", index},
            {"motion", body.dynamic ? "dynamic" : "static"},
            {"mass", hasMass ? system.inertias[body.motion_index].mass : 0.0f},
            {"layer", layer},
            {"layerName", layer < layerNames.size() ? layerNames[layer] : std::string{}},
            {"materialCrcs", hexList(crcs)},
            {"shape", shape}};
}

nlohmann::json infoJson(const NifFile& nif, const std::vector<std::string>& layerNames) {
    auto systems = nlohmann::json::array();
    for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
        nlohmann::json entry{{"block", blob.block},
                             {"owned", blob.owner >= 0},
                             {"bodyId", blob.body_id},
                             {"node", std::string(nodeName(nif, blob.target_node))},
                             {"bodies", nlohmann::json::array()},
                             {"constraints", nlohmann::json::array()},
                             {"unsupported", nlohmann::json::array()}};
        try {
            const auto system = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
            for (std::size_t i = 0; i < system.bodies.size(); ++i) {
                entry["bodies"].push_back(bodyJson(system, i, layerNames));
            }
            for (const auto& constraint : system.constraints) {
                entry["constraints"].push_back({{"kind", constraint.kind},
                                                {"child", constraint.child_body},
                                                {"parent", constraint.parent_body}});
            }
            for (const auto& unsupported : system.unsupported) {
                entry["unsupported"].push_back({{"class", unsupported.class_name}, {"object", unsupported.object}});
            }
        } catch (const fo4::parse_error& error) {
            entry["error"] = error.parse_message();
        }
        systems.push_back(std::move(entry));
    }
    auto findings = nlohmann::json::array();
    for (const auto& finding : fo4::nif::lint_collision(nif)) {
        findings.push_back({{"issue", issueName(finding.issue)}, {"block", finding.block}});
    }
    return {{"systems", systems}, {"findings", findings}};
}

nlohmann::json transformJson(const Transform& transform) {
    auto rotation = nlohmann::json::array();
    for (const float value : transform.r) rotation.push_back(value);
    return {{"t", {transform.t[0], transform.t[1], transform.t[2]}}, {"r", rotation}, {"s", transform.s}};
}

nlohmann::json flatPoints(const std::vector<HknpVec3>& points) {
    auto out = nlohmann::json::array();
    for (const auto& point : points) {
        out.push_back(point[0]);
        out.push_back(point[1]);
        out.push_back(point[2]);
    }
    return out;
}

nlohmann::json convexTriangles(const HknpShape& shape) {
    auto out = nlohmann::json::array();
    for (const auto& face : shape.faces) {
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            out.push_back(face[0]);
            out.push_back(face[i]);
            out.push_back(face[i + 1]);
        }
    }
    return out;
}

nlohmann::json meshTriangles(const HknpShape& shape) {
    auto out = nlohmann::json::array();
    for (const auto& triangle : shape.mesh_triangles) {
        const auto& i = triangle.indices;
        for (const auto index : {i[0], i[1], i[2]}) out.push_back(index);
        if (triangle.quad) {
            for (const auto index : {i[0], i[2], i[3]}) out.push_back(index);
        }
    }
    return out;
}

nlohmann::json geometryJson(const NifFile& nif) {
    auto bodies = nlohmann::json::array();
    auto skipped = nlohmann::json::array();
    for (const auto& blob : fo4::nif::read_collision_blobs(nif)) {
        const auto node = std::string(nodeName(nif, blob.target_node));
        if (blob.target_node < 0) {
            skipped.push_back({{"node", node}, {"kind", "unlinked"}});
            continue;
        }
        const auto world = fo4::nif::shape_to_global(nif, nif.nodes()[static_cast<std::size_t>(blob.target_node)].block);
        std::optional<HknpSystem> decoded;
        try {
            decoded = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blob.bytes));
        } catch (const fo4::parse_error&) {
            skipped.push_back({{"node", node}, {"kind", "undecodable"}});
            continue;
        }
        for (const auto& body : decoded->bodies) {
            if (body.shape < 0 || static_cast<std::size_t>(body.shape) >= decoded->shapes.size()) {
                skipped.push_back({{"node", node}, {"kind", "unsupported"}});
                continue;
            }
            const auto& shape = decoded->shapes[static_cast<std::size_t>(body.shape)];
            const bool convex = shape.kind == HknpShapeKind::convex;
            if (!convex && shape.kind != HknpShapeKind::compressed_mesh) {
                skipped.push_back({{"node", node}, {"kind", kindName(shape.kind)}});
                continue;
            }
            const auto vertices = convex ? flatPoints(shape.vertices) : flatPoints(shape.mesh_vertices);
            const auto triangles = convex ? convexTriangles(shape) : meshTriangles(shape);
            bodies.push_back({{"node", node},
                              {"motion", body.dynamic ? "dynamic" : "static"},
                              {"transform", transformJson(world)},
                              {"shapes", nlohmann::json::array({{{"vertices", vertices}, {"triangles", triangles}}})}});
        }
    }
    return {{"bodies", bodies}, {"skipped", skipped}};
}

struct NodeGeometry {
    std::vector<HknpVec3> points;
    std::vector<HknpTriangle> triangles;
};

NodeGeometry nodeGeometry(const NifFile& nif, std::uint32_t node) {
    NodeGeometry out;
    for (const auto& shape : fo4::nif::read_shapes(nif)) {
        const auto relative = relativeTransform(nif, shape.block, node);
        if (!relative) continue;
        const auto base = static_cast<std::uint32_t>(out.points.size());
        for (std::size_t i = 0; i + 2 < shape.verts.size(); i += 3) {
            HknpVec3 point{};
            for (std::size_t row = 0; row < 3; ++row) {
                float value = 0.0f;
                for (std::size_t k = 0; k < 3; ++k) value += relative->r[row * 3 + k] * shape.verts[i + k];
                point[row] = (relative->s * value + relative->t[row]) / fo4::nif::k_hknp_game_units_per_metre;
            }
            out.points.push_back(point);
        }
        for (std::size_t t = 0; t + 2 < shape.tris.size(); t += 3) {
            out.triangles.push_back({base + shape.tris[t], base + shape.tris[t + 1], base + shape.tris[t + 2]});
        }
    }
    return out;
}

HknpShape boxShape(const std::vector<HknpVec3>& points) {
    HknpVec3 low = points.front();
    HknpVec3 high = low;
    for (const auto& point : points) {
        for (std::size_t k = 0; k < 3; ++k) {
            low[k] = std::min(low[k], point[k]);
            high[k] = std::max(high[k], point[k]);
        }
    }
    HknpVec3 center{};
    HknpVec3 half{};
    for (std::size_t k = 0; k < 3; ++k) {
        center[k] = (low[k] + high[k]) / 2.0f;
        half[k] = std::max((high[k] - low[k]) / 2.0f, k_min_box_half_extent);
    }
    return fo4::nif::make_hknp_box(center, half, k_collision_material_crc);
}

HknpShape meshShape(const NodeGeometry& geometry) {
    HknpShape shape;
    shape.kind = HknpShapeKind::compressed_mesh;
    shape.class_name = "hknpCompressedMeshShape";
    shape.material_crc = k_collision_material_crc;
    shape.mesh_vertices = geometry.points;
    for (const auto& triangle : geometry.triangles) {
        fo4::nif::HknpMeshTriangle mesh;
        mesh.indices = {triangle[0], triangle[1], triangle[2], 0U};
        mesh.material_crc = k_collision_material_crc;
        shape.mesh_triangles.push_back(mesh);
    }
    return shape;
}

std::vector<std::byte> generatedPackfile(const NifFile& nif, const NifCollisionSet& request) {
    if (!std::isfinite(request.massKg) || request.massKg < 0.0) {
        throw std::invalid_argument("body mass must be a finite number of kilograms, and 0 keeps the body static");
    }
    const auto geometry = nodeGeometry(nif, nodeBlock(nif, request.node));
    if (geometry.points.empty()) {
        throw std::invalid_argument("node '" + request.node + "' has no shape geometry to build collision from");
    }
    if (request.shape == NifCollisionShape::mesh && request.massKg > 0.0) {
        throw std::invalid_argument("a mesh collision is static only; a body mass needs a box or hull");
    }
    fo4::nif::HknpSystemBody body;
    body.mass = static_cast<float>(request.massKg);
    switch (request.shape) {
        case NifCollisionShape::box: body.shape = boxShape(geometry.points); break;
        case NifCollisionShape::hull:
            body.shape = fo4::nif::make_hknp_hull(geometry.points, geometry.triangles, k_collision_material_crc,
                                                  &body.inertia_orientation);
            break;
        case NifCollisionShape::mesh: body.shape = meshShape(geometry); break;
        case NifCollisionShape::none: throw std::invalid_argument("kind must be box, hull or mesh");
    }
    return fo4::nif::write_hknp_packfile(fo4::nif::author_hknp_system({body}));
}

std::vector<std::byte> setCollision(const NifCollisionSet& request) {
    const auto source = readNif(request.nifPath);
    if (request.fromDonor) {
        const auto donor = readNif(request.donorPath);
        const auto blobs = fo4::nif::read_collision_blobs(donor);
        std::vector<const fo4::nif::CollisionBlob*> owned;
        for (const auto& blob : blobs) {
            if (blob.owner >= 0) owned.push_back(&blob);
        }
        if (owned.empty()) throw std::invalid_argument("donor NIF has no collision body to copy");
        const auto choice = request.donorBody.value_or(0);
        if (choice >= owned.size()) {
            throw std::invalid_argument("donor_body " + std::to_string(choice) +
                                        " is out of range: the donor has " + std::to_string(owned.size()) +
                                        " collision bodies");
        }
        return fo4::nif::set_node_collision(source, request.node, owned[choice]->bytes, owned[choice]->body_id);
    }
    if (request.shape == NifCollisionShape::none) throw std::invalid_argument("kind must be box, hull or mesh");
    return fo4::nif::set_node_collision(source, request.node, generatedPackfile(source, request), 0U);
}

}  // namespace

Result<std::string> NifCollisionService::info(const fs::path& nifPath,
                                              std::span<const fo4::nif::HavokLayerRow> layers) const {
    if (auto valid = requireNif(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return guarded([&nifPath, layers] { return dumpJson(infoJson(readNif(nifPath), havokLayerNames(layers))); });
}

Result<std::string> NifCollisionService::geometry(const fs::path& nifPath) const {
    if (auto valid = requireNif(nifPath, "NIF"); !valid) return std::unexpected(valid.error());
    return guarded([&nifPath] { return dumpJson(geometryJson(readNif(nifPath))); });
}

Result<std::string> NifCollisionService::set(const NifCollisionSet& request) const {
    if (auto valid = saveTarget(request.nifPath, request.outputPath); !valid) return std::unexpected(valid.error());
    if (request.donorBody && !request.fromDonor) {
        return std::unexpected(Error{"InvalidDonorBody", "donor_body applies only to a donor_nif source"});
    }
    if (request.fromDonor) {
        if (auto valid = requireNif(request.donorPath, "Donor NIF"); !valid) return std::unexpected(valid.error());
    }
    return guarded([&request] {
        return saved("RESULT: collision set OK (in-process)", setCollision(request), request.outputPath);
    });
}

Result<std::string> NifCollisionService::remove(const fs::path& nifPath, const std::string& node,
                                                const fs::path& outputPath) const {
    if (auto valid = saveTarget(nifPath, outputPath); !valid) return std::unexpected(valid.error());
    return guarded([&] {
        return saved("RESULT: collision removed (in-process)",
                     fo4::nif::remove_node_collision(readNif(nifPath), node), outputPath);
    });
}

}
