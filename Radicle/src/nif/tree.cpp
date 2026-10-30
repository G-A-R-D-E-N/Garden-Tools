#include "fo4/nif/describe.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "nif_internal.hpp"
#include "tree_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using detail::block_ref_text;
using detail::bool_field;
using detail::number_field;
using detail::numbers_field;
using detail::readonly_field;
using detail::text_field;
using detail::tex_field;

constexpr std::uint32_t k_fo4_version = 130;
constexpr std::size_t k_shape_texture_slots = 8;

std::string field_text(const std::vector<TreeField>& fields, std::string_view key) {
  for (const auto& field : fields) {
    if (field.key != key) continue;
    if (const auto* text = std::get_if<std::string>(&field.value)) return *text;
  }
  return {};
}

// Constraint: every block reaches this function, so an unreadable one still shows its type and its fault.
template <typename Build>
TreeBlock make_block(const NifFile& nif, std::uint32_t block, std::string group, std::string cat, Build build) {
  const auto type = nif.blocks().at(block).type;
  std::vector<TreeField> fields;
  try {
    fields = build();
  } catch (const parse_error& error) {
    fields.push_back(readonly_field("readError", "Read error", error.parse_message()));
  }
  auto name = field_text(fields, "name");
  return TreeBlock{block, type, std::move(name), std::move(group), std::move(cat), std::move(fields)};
}

std::vector<TreeField> node_fields(const Node& node) {
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", node.name));
  fields.push_back(numbers_field("translation", "Translation", {node.local.t[0], node.local.t[1], node.local.t[2]},
                                 "vec3"));
  fields.push_back(number_field("scale", "Scale", node.local.s));
  return fields;
}

// Invariant: positions head the list because the descriptor's sizes start with them, then the VertexDesc flag order.
void append_vertex_desc_fields(std::vector<TreeField>& fields, const VertexDesc& desc) {
  std::string attributes = vertex_attribute_name(VertexAttribute::Position);
  for (std::size_t index = 1; index < k_vertex_attribute_count; ++index) {
    const auto attribute = static_cast<VertexAttribute>(index);
    if (!desc.has(attribute)) continue;
    attributes += ", ";
    attributes += vertex_attribute_name(attribute);
  }
  fields.push_back(readonly_field("vertexSize", "Vertex size", std::to_string(desc.size_bytes()) + " bytes"));
  fields.push_back(readonly_field("vertexAttributes", "Vertex attributes", attributes));
  fields.push_back(readonly_field("fullPrecision", "Full precision positions", desc.full_precision() ? "yes" : "no"));
}

std::vector<TreeField> shape_fields(const NifFile& nif, std::uint32_t block) {
  const auto shape = read_shape(nif, block);
  const auto transform = block_transform(nif, block);
  const auto shader_type = shape.shader_ref >= 0
      ? nif.blocks().at(static_cast<std::size_t>(shape.shader_ref)).type
      : std::string{};
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", shape.name));
  fields.push_back(numbers_field("translation", "Translation", {transform.t[0], transform.t[1], transform.t[2]},
                                 "vec3"));
  fields.push_back(readonly_field("verts", "Vertices", std::to_string(shape.verts.size() / 3)));
  fields.push_back(readonly_field("tris", "Triangles", std::to_string(shape.tris.size() / 3)));
  fields.push_back(readonly_field("shaderType", "Shader", shader_type));
  fields.push_back(text_field("material", "Material (.bgsm)", shape.material));
  for (std::size_t slot = 0; slot < k_shape_texture_slots; ++slot) {
    fields.push_back(tex_field("tex" + std::to_string(slot), "Texture " + std::to_string(slot),
                               slot < shape.textures.size() ? shape.textures[slot] : std::string{}));
  }
  fields.push_back(readonly_field("skin", "Skin", block_ref_text(shape.skin_ref)));
  append_vertex_desc_fields(fields, read_vertex_desc(nif, block));
  if (shape.alpha_ref >= 0) {
    const auto alpha = read_alpha(nif, static_cast<std::uint32_t>(shape.alpha_ref));
    fields.push_back(bool_field("alphaBlend", "Alpha blend", alpha.blend_enabled));
    fields.push_back(bool_field("alphaTest", "Alpha test", alpha.test_enabled));
    fields.push_back(number_field("alphaThreshold", "Alpha threshold", static_cast<float>(alpha.threshold), "int"));
  }
  return fields;
}

std::vector<TreeField> shape_fallback_fields(const NifFile& nif, std::uint32_t block, const parse_error& error) {
  std::vector<TreeField> fields;
  // Invariant: the name sits at the start of the NiAVObject prefix, so a shape that fails further in still reports it.
  try {
    auto cursor = detail::block_cursor(nif, block);
    fields.push_back(text_field("name", "Name", detail::read_string_ref(cursor, nif)));
  } catch (const parse_error&) {
    fields.push_back(text_field("name", "Name", {}));
  }
  fields.push_back(readonly_field("readError", "Read error", error.parse_message()));
  return fields;
}

std::vector<TreeField> collision_object_fields(const NifFile& nif, std::uint32_t block) {
  const auto object = read_collision_object(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(readonly_field("target", "Target", block_ref_text(object.target)));
  fields.push_back(number_field("flags", "Flags", static_cast<float>(object.flags), "int"));
  fields.push_back(readonly_field("system", "Data", block_ref_text(object.system)));
  fields.push_back(number_field("bodyId", "Body ID", static_cast<float>(object.body_id), "int"));
  return fields;
}

std::vector<TreeField> collision_system_fields(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  const auto size = cursor.read_u32();
  std::vector<TreeField> fields;
  fields.push_back(readonly_field("bytes", "Packfile bytes", std::to_string(size)));
  return fields;
}

std::vector<TreeField> skin_instance_fields(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  const auto name = detail::read_string_ref(cursor, nif);
  const auto data = cursor.read_i32();
  const auto bone_count = cursor.read_u32();
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", name));
  fields.push_back(readonly_field("skinData", "Bone data", block_ref_text(data)));
  fields.push_back(readonly_field("bones", "Bones", std::to_string(bone_count)));
  return fields;
}

std::vector<TreeField> bone_data_fields(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(readonly_field("transforms", "Transforms", std::to_string(cursor.read_u32())));
  return fields;
}

std::vector<TreeField> bsx_fields(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  const auto name = detail::read_string_ref(cursor, nif);
  const auto flags = read_bsx_flags(nif, block);
  std::vector<TreeField> fields;
  fields.push_back(text_field("name", "Name", name));
  fields.push_back(number_field("flags", "Flags", static_cast<float>(flags), "int"));
  fields.push_back(bool_field("editorMarker", "Editor marker", is_editor_marker(flags)));
  return fields;
}

std::vector<TreeField> unknown_fields(const NifFile& nif, std::uint32_t block) {
  std::vector<TreeField> fields;
  fields.push_back(readonly_field("size", "Size", std::to_string(nif.blocks().at(block).size)));
  return fields;
}

TreeBlock describe_block(const NifFile& nif, std::uint32_t block) {
  const auto& type = nif.blocks().at(block).type;
  const auto node = nif.node_for_block(block);
  if (node >= 0) {
    const auto& entry = nif.nodes().at(static_cast<std::size_t>(node));
    return make_block(nif, block, "Nodes", "node", [&entry] { return node_fields(entry); });
  }
  if (is_shape_block_type(type)) {
    std::vector<TreeField> fields;
    try {
      fields = shape_fields(nif, block);
    } catch (const parse_error& error) {
      fields = shape_fallback_fields(nif, block, error);
    }
    auto name = field_text(fields, "name");
    return TreeBlock{block, type, std::move(name), "Shapes", "shape", std::move(fields)};
  }
  if (type.starts_with("bhk")) {
    if (type == "bhkNPCollisionObject") {
      return make_block(nif, block, "Collision", "collision",
                        [&nif, block] { return collision_object_fields(nif, block); });
    }
    if (type == "bhkPhysicsSystem" || type == "bhkRagdollSystem") {
      return make_block(nif, block, "Collision", "collision",
                        [&nif, block] { return collision_system_fields(nif, block); });
    }
    return make_block(nif, block, "Collision", "collision", [&nif, block] { return unknown_fields(nif, block); });
  }
  if (type == "BSLightingShaderProperty") {
    return make_block(nif, block, "Extra", "shader", [&nif, block] { return detail::lighting_shader_fields(nif, block); });
  }
  if (type == "BSEffectShaderProperty") {
    return make_block(nif, block, "Extra", "shader", [&nif, block] { return detail::effect_shader_fields(nif, block); });
  }
  if (type == "NiAlphaProperty") {
    return make_block(nif, block, "Extra", "alpha", [&nif, block] { return detail::alpha_property_fields(nif, block); });
  }
  if (type == "BSShaderTextureSet") {
    return make_block(nif, block, "Extra", "texture", [&nif, block] { return detail::texture_set_fields(nif, block); });
  }
  if (type == "BSSkin::Instance") {
    return make_block(nif, block, "Extra", "skin", [&nif, block] { return skin_instance_fields(nif, block); });
  }
  if (type == "BSSkin::BoneData") {
    return make_block(nif, block, "Extra", "skin", [&nif, block] { return bone_data_fields(nif, block); });
  }
  if (type == "BSXFlags") {
    return make_block(nif, block, "Extra", "property", [&nif, block] { return bsx_fields(nif, block); });
  }
  return make_block(nif, block, "Extra", "block", [&nif, block] { return unknown_fields(nif, block); });
}

}  // namespace

NifTree describe_nif(const NifFile& nif) {
  NifTree tree;
  tree.fo4 = nif.bs_version() == k_fo4_version;
  tree.blocks.reserve(nif.blocks().size());
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    tree.blocks.push_back(describe_block(nif, block));
  }
  return tree;
}

}  // namespace fo4::nif
