#include "import_gltf_build.hpp"

#include "fo4/nif/nif.hpp"
#include "import_blocks.hpp"
#include "write_model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fo4::nif::import_detail {
namespace {

using write_detail::Bytes;
using write_detail::Model;

struct PrimitiveBlocks {
  std::size_t shape{0};
  std::size_t shader{0};
  std::size_t texture_set{0};
  std::size_t instance{0};
  std::size_t data{0};
};

std::int32_t as_block(std::size_t block) { return static_cast<std::int32_t>(block); }

}

std::vector<std::byte> build_gltf_nif(const GltfScene& scene, const NifFile* skeleton, bool box_collision) {
  const std::size_t node_count = scene.nodes.size();
  const bool copy_skeleton = skeleton != nullptr && scene.skinned;
  const std::size_t copy_base = 1 + node_count;
  const std::size_t copy_count = copy_skeleton ? skeleton->nodes().size() : 0;
  std::size_t next = copy_base + copy_count;

  std::vector<std::vector<PrimitiveBlocks>> plans(node_count);
  for (std::size_t n = 0; n < node_count; ++n) {
    for (const auto& primitive : scene.nodes[n].primitives) {
      PrimitiveBlocks planned;
      planned.shape = next++;
      planned.shader = next++;
      planned.texture_set = next++;
      if (!primitive.bone_names.empty()) {
        planned.instance = next++;
        planned.data = next++;
      }
      plans[n].push_back(planned);
    }
  }
  const std::size_t bsx = next++;
  std::size_t collision = 0;
  std::size_t physics = 0;
  if (box_collision) {
    collision = next++;
    physics = next++;
  }
  const std::size_t total = next;

  Model model;
  model.banner = "Gamebryo File Format, Version 20.2.0.7";
  model.version = k_nif_version;
  model.user_version = k_user_version;
  model.bs_version = k_bs_version;
  model.export_strings.assign(4, std::string{});
  model.roots = {0};

  std::vector<Bytes> blocks(total);
  std::vector<std::string_view> types(total);
  std::vector<std::int32_t> root_children;
  std::vector<std::vector<std::int32_t>> child_nodes(node_count);
  for (std::size_t t = 0; t < node_count; ++t) {
    const auto parent = scene.nodes[t].parent;
    if (parent < 0) {
      root_children.push_back(as_block(1 + t));
    } else {
      child_nodes[static_cast<std::size_t>(parent)].push_back(as_block(1 + t));
    }
  }

  std::unordered_map<std::string, std::size_t> bone_block;
  std::int32_t skeleton_root = 0;
  if (copy_skeleton) {
    for (std::size_t k = 0; k < copy_count; ++k) {
      const auto& node = skeleton->nodes()[k];
      bone_block.emplace(node.name, copy_base + k);
      std::vector<std::int32_t> children;
      for (const auto child : node.children) {
        const auto index = skeleton->node_for_block(child);
        if (index >= 0) children.push_back(as_block(copy_base + static_cast<std::size_t>(index)));
      }
      blocks[copy_base + k] = ni_node(node.local, write_detail::intern_string(model, node.name), {}, children);
      types[copy_base + k] = "NiNode";
      if (node.parent < 0) root_children.push_back(as_block(copy_base + k));
    }
    if (!skeleton->roots().empty()) {
      const auto root = skeleton->node_for_block(skeleton->roots().front());
      if (root >= 0) skeleton_root = as_block(copy_base + static_cast<std::size_t>(root));
    }
  }

  for (std::size_t n = 0; n < node_count; ++n) {
    const auto& node = scene.nodes[n];
    std::vector<std::int32_t> children = child_nodes[n];
    for (const auto& planned : plans[n]) children.push_back(as_block(planned.shape));
    blocks[1 + n] = ni_node(node.local, write_detail::intern_string(model, node.name), {}, children);
    types[1 + n] = "NiNode";
  }

  for (std::size_t n = 0; n < node_count; ++n) {
    for (std::size_t i = 0; i < scene.nodes[n].primitives.size(); ++i) {
      const auto& primitive = scene.nodes[n].primitives[i];
      const auto& planned = plans[n][i];
      const bool skinned = !primitive.bone_names.empty();
      blocks[planned.shape] =
          shape_block(primitive.mesh, write_detail::intern_string(model, primitive.mesh.name),
                      skinned ? as_block(planned.instance) : -1, as_block(planned.shader));
      types[planned.shape] = "BSTriShape";
      blocks[planned.shader] = shader_block(write_detail::intern_string(model, primitive.material.bgsm),
                                            as_block(planned.texture_set),
                                            k_shader_flags1 | (skinned ? k_skinned_shader_flag : 0U));
      types[planned.shader] = "BSLightingShaderProperty";
      blocks[planned.texture_set] = texture_set_block(primitive.material.diffuse, primitive.material.normal);
      types[planned.texture_set] = "BSShaderTextureSet";
      if (!skinned) continue;
      std::vector<std::int32_t> bones;
      for (const auto& name : primitive.bone_names) {
        const auto found = bone_block.find(name);
        if (found == bone_block.end()) {
          throw std::runtime_error("internal error: bone '" + name + "' is not in the skeleton copy");
        }
        bones.push_back(as_block(found->second));
      }
      blocks[planned.instance] = skin_instance_block(skeleton_root, as_block(planned.data), bones);
      types[planned.instance] = "BSSkin::Instance";
      blocks[planned.data] = bone_data_block(primitive.mesh, primitive.bone_binds);
      types[planned.data] = "BSSkin::BoneData";
    }
  }

  blocks[0] = ni_node(Transform{}, write_detail::intern_string(model, "Scene Root"), {as_block(bsx)}, root_children,
                      box_collision ? as_block(collision) : -1);
  types[0] = "NiNode";
  blocks[bsx] = bsx_block(write_detail::intern_string(model, "BSX"), box_collision ? k_box_bsx_flags : 0U);
  types[bsx] = "BSXFlags";
  if (box_collision) {
    blocks[collision] = collision_object_block(as_block(physics));
    types[collision] = "bhkNPCollisionObject";
    blocks[physics] = physics_system_block(box_packfile(scene.bounds));
    types[physics] = "bhkPhysicsSystem";
  }

  for (std::size_t i = 0; i < total; ++i) {
    const std::string type(types[i]);
    auto found = std::find(model.block_types.begin(), model.block_types.end(), type);
    if (found == model.block_types.end()) {
      model.block_types.push_back(type);
      found = model.block_types.end() - 1;
    }
    model.block_type_index.push_back(static_cast<std::uint16_t>(found - model.block_types.begin()));
  }
  model.blocks = std::move(blocks);
  for (const auto& text : model.strings) {
    model.max_string_length = std::max(model.max_string_length, static_cast<std::uint32_t>(text.size()));
  }

  auto bytes = write_detail::serialize(model);
  const auto nif = read_nif(bytes);
  const auto shapes = read_shapes(nif);
  std::vector<const GltfPrimitive*> primitives;
  for (const auto& node : scene.nodes) {
    for (const auto& primitive : node.primitives) primitives.push_back(&primitive);
  }
  bool clean = shapes.size() == primitives.size();
  for (std::size_t k = 0; clean && k < shapes.size(); ++k) {
    const auto& shape = shapes[k];
    const auto& primitive = *primitives[k];
    clean = shape.warning.empty() && shape.verts.size() == 3 * primitive.mesh.positions.size() &&
            shape.tris.size() == primitive.mesh.tris.size() && shape.bones.size() == primitive.bone_names.size();
  }
  if (!clean || (box_collision && !reads_one_box_body(nif))) {
    throw std::runtime_error("internal error: the written NIF does not read back as the glTF scene");
  }
  return bytes;
}

}
