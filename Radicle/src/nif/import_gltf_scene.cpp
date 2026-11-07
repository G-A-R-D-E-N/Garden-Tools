#include "import_gltf_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fo4::nif::import_detail {
namespace {

using gltf_detail::apply;
using gltf_detail::Document;
using gltf_detail::element;
using gltf_detail::Frame;
using gltf_detail::Matrix;
using gltf_detail::multiply;
using gltf_detail::node_matrix;

constexpr std::size_t k_none = static_cast<std::size_t>(-1);
constexpr double k_frame_tolerance = 1e-5;
constexpr double k_metres_per_unit = 0.0142875;
// Invariant: the exporter's synthetic root, metres per NIF unit with the Y-up turn, in column-major order.
constexpr Matrix k_nif_units_root{k_metres_per_unit, 0.0, 0.0, 0.0, 0.0, 0.0, -k_metres_per_unit, 0.0,
                                  0.0, k_metres_per_unit, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
constexpr Matrix k_identity{1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};

bool is_nif_units_root(const Matrix& m) {
  for (std::size_t i = 0; i < 16; ++i) {
    if (std::fabs(m[i] - k_nif_units_root[i]) > k_frame_tolerance) return false;
  }
  return true;
}

std::vector<std::size_t> children_of(const nlohmann::json& node, std::size_t count) {
  std::vector<std::size_t> children;
  for (const auto& child : node.value("children", nlohmann::json::array())) {
    const auto index = child.get<std::size_t>();
    if (index >= count) throw std::runtime_error("glTF node lists child " + std::to_string(index) + " that does not exist");
    children.push_back(index);
  }
  return children;
}

class Walk {
public:
  Walk(const Document& doc, const ImportOptions& options, const BoneNames* bones, Frame frame,
       std::vector<bool> joints)
      : doc_(doc), options_(options), bones_(bones), frame_(frame), joints_(std::move(joints)),
        seen_(doc.json.at("nodes").size(), false) {
    constexpr float inf = std::numeric_limits<float>::infinity();
    scene_.bounds = {Vec3{inf, inf, inf}, Vec3{-inf, -inf, -inf}};
  }

  GltfScene run(const std::vector<std::size_t>& roots, std::size_t frame_node);

private:
  void visit(std::size_t index, std::int32_t emitted_parent, const Matrix& parent_world);
  void add_primitives(const nlohmann::json& node, std::size_t self, const Matrix& world);

  const Document& doc_;
  const ImportOptions& options_;
  const BoneNames* bones_;
  Frame frame_;
  std::vector<bool> joints_;
  std::vector<bool> seen_;
  GltfScene scene_;
};

GltfScene Walk::run(const std::vector<std::size_t>& roots, std::size_t frame_node) {
  for (const auto root : roots) {
    if (root == frame_node) {
      const auto& json = element(doc_.json, "nodes", root);
      if (json.contains("mesh")) {
        throw std::runtime_error("glTF NIF-units root node '" + json.value("name", std::string{}) +
                                 "' carries a mesh, which the importer does not place");
      }
      for (const auto child : children_of(json, seen_.size())) visit(child, -1, k_identity);
    } else {
      visit(root, -1, k_identity);
    }
  }
  const auto primitive_count = std::accumulate(scene_.nodes.begin(), scene_.nodes.end(), std::size_t{0},
                                               [](std::size_t total, const GltfNode& node) {
                                                 return total + node.primitives.size();
                                               });
  if (primitive_count == 0) throw std::runtime_error("glTF has no triangle meshes to import");
  return std::move(scene_);
}

void Walk::visit(std::size_t index, std::int32_t emitted_parent, const Matrix& parent_world) {
  if (seen_[index]) {
    throw std::runtime_error("glTF node " + std::to_string(index) + " has more than one parent or sits in a cycle");
  }
  seen_[index] = true;
  const auto& json = element(doc_.json, "nodes", index);
  const auto name = json.value("name", std::string{});
  const auto local = node_matrix(json);
  const auto world = multiply(parent_world, local);
  if (joints_[index]) {
    if (json.contains("mesh")) {
      throw std::runtime_error("glTF node '" + name + "' is a joint and also holds a mesh; move the mesh to its own node");
    }
    for (const auto child : children_of(json, seen_.size())) visit(child, -1, world);
    return;
  }

  GltfNode node;
  node.name = name;
  node.parent = emitted_parent;
  node.local = frame_.transform(emitted_parent >= 0 ? local : world, "node '" + name + "'");
  const auto self = scene_.nodes.size();
  scene_.nodes.push_back(std::move(node));
  if (json.contains("mesh")) add_primitives(json, self, world);
  for (const auto child : children_of(json, seen_.size())) visit(child, static_cast<std::int32_t>(self), world);
}

void Walk::add_primitives(const nlohmann::json& node, std::size_t self, const Matrix& world) {
  const auto node_name = node.value("name", std::string{});
  const auto& mesh = element(doc_.json, "meshes", node.at("mesh").get<std::size_t>());
  const auto mesh_name = mesh.value("name", std::string{});
  std::optional<std::size_t> skin;
  if (node.contains("skin")) skin = node.at("skin").get<std::size_t>();
  const auto& primitives = mesh.at("primitives");
  for (std::size_t p = 0; p < primitives.size(); ++p) {
    std::string shape = mesh_name.empty() ? node_name : mesh_name;
    if (p > 0) shape += "_" + std::to_string(p);
    auto primitive = decode_primitive(doc_, primitives.at(p), shape, skin, frame_, bones_, options_);
    const bool skinned = !primitive.bone_names.empty();
    const auto placement = frame_.conjugate(world);
    for (const auto& vertex : primitive.mesh.positions) {
      const Vec3 corner = skinned ? vertex : apply(placement, vertex);
      for (std::size_t k = 0; k < 3; ++k) {
        scene_.bounds[0][k] = std::min(scene_.bounds[0][k], corner[k]);
        scene_.bounds[1][k] = std::max(scene_.bounds[1][k], corner[k]);
      }
    }
    scene_.skinned = scene_.skinned || skinned;
    scene_.nodes[self].primitives.push_back(std::move(primitive));
  }
}

}

GltfScene read_scene(const Document& doc, const ImportOptions& options, const BoneNames* bones) {
  if (!doc.json.contains("nodes") || doc.json.at("nodes").empty()) throw std::runtime_error("glTF has no nodes");
  const auto& nodes = doc.json.at("nodes");
  const std::size_t count = nodes.size();
  std::vector<std::size_t> parent(count, k_none);
  for (std::size_t i = 0; i < count; ++i) {
    for (const auto child : children_of(nodes.at(i), count)) {
      if (parent[child] != k_none) {
        throw std::runtime_error("glTF node " + std::to_string(child) + " has more than one parent");
      }
      parent[child] = i;
    }
  }

  std::vector<std::size_t> roots;
  if (doc.json.contains("scenes") && !doc.json.at("scenes").empty()) {
    const auto scene = doc.json.value("scene", std::size_t{0});
    for (const auto& root : element(doc.json, "scenes", scene).value("nodes", nlohmann::json::array())) {
      roots.push_back(root.get<std::size_t>());
    }
  } else {
    for (std::size_t i = 0; i < count; ++i) {
      if (parent[i] == k_none) roots.push_back(i);
    }
  }
  for (const auto root : roots) {
    if (root >= count) throw std::runtime_error("glTF scene lists node " + std::to_string(root) + " that does not exist");
  }

  std::size_t frame_node = k_none;
  for (const auto root : roots) {
    if (is_nif_units_root(node_matrix(nodes.at(root)))) {
      frame_node = root;
      break;
    }
  }

  std::vector<bool> joints(count, false);
  if (doc.json.contains("skins")) {
    for (const auto& skin : doc.json.at("skins")) {
      for (const auto& joint : skin.at("joints")) {
        const auto index = joint.get<std::size_t>();
        if (index >= count) throw std::runtime_error("glTF skin lists joint " + std::to_string(index) + " that does not exist");
        joints[index] = true;
      }
    }
  }
  Walk walk(doc, options, bones, Frame{frame_node != k_none}, std::move(joints));
  return walk.run(roots, frame_node);
}

}
