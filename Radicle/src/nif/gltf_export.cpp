#include "fo4/nif/gltf_export.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "gltf_internal.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using Matrix = std::array<float, 16>;

constexpr std::uint32_t k_glb_magic = 0x46546C67U;
constexpr std::uint32_t k_glb_version = 2;
constexpr std::uint32_t k_json_chunk = 0x4E4F534AU;
constexpr std::uint32_t k_bin_chunk = 0x004E4942U;
constexpr std::uint32_t k_unsigned_byte = 5121;
constexpr std::uint32_t k_unsigned_short = 5123;
constexpr std::uint32_t k_float = 5126;

constexpr Matrix k_identity{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
constexpr float k_metres_per_nif_unit = 0.0142875f;
constexpr Matrix k_nif_to_metres_y_up{k_metres_per_nif_unit, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -k_metres_per_nif_unit, 0.0f, 0.0f, k_metres_per_nif_unit, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

struct GltfNode {
  std::string name;
  std::size_t parent{0};
  std::optional<Matrix> matrix;
  std::optional<std::size_t> mesh;
  std::optional<std::size_t> skin;
};

void put_u16(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFU));
  out.push_back(static_cast<std::byte>(value >> 8U));
}

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (std::uint32_t shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::byte>(value >> shift));
}

template <typename T>
std::vector<std::byte> pack(const std::vector<T>& values) {
  std::vector<std::byte> out;
  for (const auto value : values) {
    if constexpr (std::is_same_v<T, float>) {
      put_u32(out, std::bit_cast<std::uint32_t>(value));
    } else if constexpr (sizeof(T) == 2) {
      put_u16(out, value);
    } else {
      out.push_back(static_cast<std::byte>(value));
    }
  }
  return out;
}

bool is_root(const NifFile& nif, std::uint32_t block) {
  return std::ranges::find(nif.roots(), block) != nif.roots().end();
}

Transform bind_transform(const Bone& bone) {
  Transform xf;
  std::ranges::copy(bone.translation, xf.t);
  std::ranges::copy(bone.rotation, xf.r);
  xf.s = bone.scale;
  return xf;
}

void require_finite(const Matrix& m, std::size_t offset) {
  if (!std::ranges::all_of(m, [](float value) { return std::isfinite(value); })) {
    throw parse_error(offset, "transform is not finite");
  }
}

Matrix column_major(const Transform& xf, std::size_t offset) {
  Matrix m{};
  for (std::size_t col = 0; col < 3; ++col) {
    for (std::size_t row = 0; row < 3; ++row) m[col * 4 + row] = xf.s * xf.r[row * 3 + col];
    m[12 + col] = xf.t[col];
  }
  m[15] = 1.0f;
  require_finite(m, offset);
  return m;
}

Matrix inverse_column_major(const Transform& xf, std::size_t offset) {
  if (xf.s == 0.0f) throw parse_error(offset, "bone transform has zero scale, so it has no inverse");
  Matrix m{};
  for (std::size_t row = 0; row < 3; ++row) {
    float along = 0.0f;
    for (std::size_t col = 0; col < 3; ++col) m[col * 4 + row] = xf.r[col * 3 + row] / xf.s;
    for (std::size_t j = 0; j < 3; ++j) along += xf.r[j * 3 + row] * xf.t[j];
    m[12 + row] = -along / xf.s;
  }
  m[15] = 1.0f;
  require_finite(m, offset);
  return m;
}

nlohmann::json position_bounds(const Shape& shape, std::size_t offset) {
  constexpr float inf = std::numeric_limits<float>::infinity();
  std::array<float, 3> low{inf, inf, inf};
  std::array<float, 3> high{-inf, -inf, -inf};
  for (std::size_t i = 0; i < shape.verts.size(); ++i) {
    const float value = shape.verts[i];
    if (!std::isfinite(value)) throw parse_error(offset, "vertex position is not finite");
    low[i % 3] = std::min(low[i % 3], value);
    high[i % 3] = std::max(high[i % 3], value);
  }
  return {{"min", low}, {"max", high}};
}

// Invariant: w is the sign that makes cross(normal, tangent) * w agree with the stored bitangent, which is glTF's TANGENT convention.
std::vector<float> tangent_frames(const Shape& shape) {
  const std::size_t count = shape.verts.size() / 3;
  std::vector<float> out;
  out.reserve(count * 4);
  for (std::size_t v = 0; v < count; ++v) {
    const std::size_t at = v * 3;
    const float* n = &shape.normals[at];
    const float* t = &shape.tangents[at];
    const float* b = &shape.bitangents[at];
    const float along_bitangent = (n[1] * t[2] - n[2] * t[1]) * b[0] + (n[2] * t[0] - n[0] * t[2]) * b[1] +
                                  (n[0] * t[1] - n[1] * t[0]) * b[2];
    out.insert(out.end(), {t[0], t[1], t[2], along_bitangent < 0.0f ? -1.0f : 1.0f});
  }
  return out;
}

std::vector<std::byte> assemble_glb(nlohmann::json& document, const std::vector<std::byte>& bin) {
  std::string json = document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  while (json.size() % 4 != 0) json.push_back(' ');
  const bool has_bin = !bin.empty();
  const std::size_t total = 12 + 8 + json.size() + (has_bin ? 8 + bin.size() : 0);

  std::vector<std::byte> out;
  out.reserve(total);
  put_u32(out, k_glb_magic);
  put_u32(out, k_glb_version);
  put_u32(out, static_cast<std::uint32_t>(total));
  put_u32(out, static_cast<std::uint32_t>(json.size()));
  put_u32(out, k_json_chunk);
  for (const char ch : json) out.push_back(static_cast<std::byte>(ch));
  if (has_bin) {
    put_u32(out, static_cast<std::uint32_t>(bin.size()));
    put_u32(out, k_bin_chunk);
    out.insert(out.end(), bin.begin(), bin.end());
  }
  return out;
}

}  // namespace

namespace gltf {

std::size_t BinaryChunk::add_view(const std::vector<std::byte>& data) {
  while (bytes_.size() % 4 != 0) bytes_.push_back(std::byte{0});
  views_.push_back({{"buffer", 0}, {"byteOffset", bytes_.size()}, {"byteLength", data.size()}});
  bytes_.insert(bytes_.end(), data.begin(), data.end());
  return views_.size() - 1;
}

std::size_t BinaryChunk::add_accessor(const std::vector<std::byte>& data, std::uint32_t component, std::size_t count,
                                      std::string_view type, nlohmann::json bounds) {
  const auto view = add_view(data);
  accessors_.push_back({{"bufferView", view},
                        {"componentType", component},
                        {"count", count},
                        {"type", std::string(type)}});
  accessors_.back().update(bounds);
  return accessors_.size() - 1;
}

void BinaryChunk::finish(nlohmann::json& document) {
  if (bytes_.empty()) return;
  while (bytes_.size() % 4 != 0) bytes_.push_back(std::byte{0});
  nlohmann::json buffers = nlohmann::json::array();
  buffers.push_back({{"byteLength", bytes_.size()}});
  document["buffers"] = buffers;
  document["bufferViews"] = views_;
  document["accessors"] = accessors_;
}

}  // namespace gltf

GltfExport export_glb(const NifFile& nif, const GltfOptions& options) {
  const auto shapes = read_shapes(nif);
  GltfExport result;

  std::vector<GltfNode> nodes(1);
  nodes[0].name = "nif_units_to_metres_z_up_to_y_up";
  nodes[0].matrix = k_nif_to_metres_y_up;

  std::unordered_map<std::string, std::size_t> node_by_name;
  for (const auto& node : nif.nodes()) {
    GltfNode out;
    out.name = node.name;
    out.parent = node.parent < 0 ? 0 : static_cast<std::size_t>(node.parent) + 1;
    out.matrix = is_root(nif, node.block) ? k_identity : column_major(node.local, nif.blocks().at(node.block).offset);
    node_by_name.emplace(node.name, nodes.size());
    nodes.push_back(std::move(out));
  }

  const std::size_t shape_base = nodes.size();
  for (const auto& shape : shapes) {
    GltfNode out;
    out.name = shape.name;
    out.parent = shape.parent_node < 0 ? 0 : static_cast<std::size_t>(shape.parent_node) + 1;
    out.matrix = is_root(nif, shape.block) ? k_identity
                                           : column_major(block_transform(nif, shape.block), nif.blocks().at(shape.block).offset);
    nodes.push_back(std::move(out));
  }

  gltf::BinaryChunk bin;
  gltf::TextureTable textures(options, bin, result);
  nlohmann::json meshes = nlohmann::json::array();
  nlohmann::json skins = nlohmann::json::array();
  nlohmann::json materials = nlohmann::json::array();

  for (std::size_t k = 0; k < shapes.size(); ++k) {
    const auto& shape = shapes[k];
    const std::size_t vertex_count = shape.verts.size() / 3;
    if (vertex_count == 0 || shape.tris.empty()) continue;
    const std::size_t offset = nif.blocks().at(shape.block).offset;
    const bool skinned = !shape.bones.empty() && shape.bone_indices.size() == vertex_count * 4 &&
                         shape.weights.size() == vertex_count * 4;

    nlohmann::json attributes = nlohmann::json::object();
    attributes["POSITION"] = bin.add_accessor(pack(shape.verts), k_float, vertex_count, "VEC3",
                                              position_bounds(shape, offset));
    if (shape.normals.size() == shape.verts.size()) {
      attributes["NORMAL"] = bin.add_accessor(pack(shape.normals), k_float, vertex_count, "VEC3");
      if (options.tangents && shape.tangents.size() == shape.verts.size() &&
          shape.bitangents.size() == shape.verts.size()) {
        attributes["TANGENT"] = bin.add_accessor(pack(tangent_frames(shape)), k_float, vertex_count, "VEC4");
      }
    }
    if (shape.uvs.size() == vertex_count * 2) {
      attributes["TEXCOORD_0"] = bin.add_accessor(pack(shape.uvs), k_float, vertex_count, "VEC2");
    }
    if (options.vertex_colours && shape.colors.size() == vertex_count * 4) {
      attributes["COLOR_0"] = bin.add_accessor(pack(shape.colors), k_float, vertex_count, "VEC4");
    }

    const std::size_t mesh_index = shape_base + k;
    if (skinned) {
      attributes["JOINTS_0"] = bin.add_accessor(pack(shape.bone_indices), k_unsigned_byte, vertex_count, "VEC4");
      attributes["WEIGHTS_0"] = bin.add_accessor(pack(shape.weights), k_float, vertex_count, "VEC4");

      nlohmann::json joints = nlohmann::json::array();
      std::vector<float> inverse_binds;
      for (const auto& bone : shape.bones) {
        const auto bind = bind_transform(bone);
        const auto found = node_by_name.find(bone.name);
        if (!bone.name.empty() && found != node_by_name.end()) {
          joints.push_back(found->second);
        } else {
          GltfNode joint;
          joint.name = bone.name;
          joint.matrix = inverse_column_major(bind, offset);
          joints.push_back(nodes.size());
          nodes.push_back(std::move(joint));
        }
        const auto matrix = column_major(bind, offset);
        inverse_binds.insert(inverse_binds.end(), matrix.begin(), matrix.end());
      }
      const auto ibm = bin.add_accessor(pack(inverse_binds), k_float, shape.bones.size(), "MAT4");
      skins.push_back({{"joints", joints}, {"inverseBindMatrices", ibm}});
      nodes[mesh_index].skin = skins.size() - 1;
      nodes[mesh_index].matrix.reset();
    }

    const auto indices = bin.add_accessor(pack(shape.tris), k_unsigned_short, shape.tris.size(), "SCALAR");
    nlohmann::json primitive;
    primitive["attributes"] = attributes;
    primitive["indices"] = indices;
    if (options.materials) {
      materials.push_back(gltf::shape_material(nif, shape, options, textures, result));
      primitive["material"] = materials.size() - 1;
    }
    nlohmann::json primitives = nlohmann::json::array();
    primitives.push_back(primitive);
    meshes.push_back({{"name", shape.name}, {"primitives", primitives}});
    nodes[mesh_index].mesh = meshes.size() - 1;
  }

  std::vector<std::vector<std::size_t>> children(nodes.size());
  for (std::size_t i = 1; i < nodes.size(); ++i) children[nodes[i].parent].push_back(i);

  nlohmann::json json_nodes = nlohmann::json::array();
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const auto& node = nodes[i];
    nlohmann::json out{{"name", node.name}};
    if (node.matrix) out["matrix"] = *node.matrix;
    if (node.mesh) out["mesh"] = *node.mesh;
    if (node.skin) out["skin"] = *node.skin;
    if (!children[i].empty()) out["children"] = children[i];
    json_nodes.push_back(std::move(out));
  }

  nlohmann::json scenes = nlohmann::json::array();
  scenes.push_back({{"nodes", nlohmann::json::array({0})}});
  nlohmann::json document{
      {"asset", {{"version", "2.0"}, {"generator", "Radicle nif glTF export"}}},
      {"scene", 0},
      {"scenes", scenes},
      {"nodes", json_nodes},
  };
  if (!meshes.empty()) document["meshes"] = meshes;
  if (!skins.empty()) document["skins"] = skins;
  if (!materials.empty()) document["materials"] = materials;
  textures.write(document);
  bin.finish(document);
  result.glb = assemble_glb(document, bin.bytes());
  return result;
}

}  // namespace fo4::nif
