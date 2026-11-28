#include "fo4/nif/mesh_plain.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/shader.hpp"
#include "gltf_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

using Vec = std::array<double, 3>;
using Basis = std::array<double, 9>;

struct Affine {
  Basis a{1, 0, 0, 0, 1, 0, 0, 0, 1};
  Vec t{};
};

// Invariant: engine x is NIF x, engine y is NIF z, engine z is NIF -y.
constexpr Basis k_engine_axes{1, 0, 0, 0, 0, 1, 0, -1, 0};
constexpr Basis k_engine_axes_inverse{1, 0, 0, 0, 0, -1, 0, 1, 0};
constexpr std::size_t k_influences = 4;

Affine from_transform(const Transform& xf) {
  Affine out;
  for (std::size_t i = 0; i < 9; ++i) out.a[i] = static_cast<double>(xf.s) * static_cast<double>(xf.r[i]);
  for (std::size_t i = 0; i < 3; ++i) out.t[i] = xf.t[i];
  return out;
}

Vec rotate(const Basis& m, const Vec& v) {
  Vec out{};
  for (std::size_t row = 0; row < 3; ++row) out[row] = m[row * 3] * v[0] + m[row * 3 + 1] * v[1] + m[row * 3 + 2] * v[2];
  return out;
}

Vec transform_point(const Affine& m, const Vec& p) {
  auto out = rotate(m.a, p);
  for (std::size_t i = 0; i < 3; ++i) out[i] += m.t[i];
  return out;
}

Basis multiply(const Basis& x, const Basis& y) {
  Basis out{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      for (std::size_t k = 0; k < 3; ++k) out[row * 3 + col] += x[row * 3 + k] * y[k * 3 + col];
    }
  }
  return out;
}

Affine compose(const Affine& outer, const Affine& inner) {
  Affine out;
  out.a = multiply(outer.a, inner.a);
  out.t = transform_point(outer, inner.t);
  return out;
}

double determinant(const Basis& m) {
  return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

bool invert(const Affine& m, Affine& out) {
  const double det = determinant(m.a);
  if (!std::isfinite(det) || std::fabs(det) < 1e-30) return false;
  const auto& a = m.a;
  out.a = {(a[4] * a[8] - a[5] * a[7]) / det, (a[2] * a[7] - a[1] * a[8]) / det, (a[1] * a[5] - a[2] * a[4]) / det,
           (a[5] * a[6] - a[3] * a[8]) / det, (a[0] * a[8] - a[2] * a[6]) / det, (a[2] * a[3] - a[0] * a[5]) / det,
           (a[3] * a[7] - a[4] * a[6]) / det, (a[1] * a[6] - a[0] * a[7]) / det, (a[0] * a[4] - a[1] * a[3]) / det};
  const auto moved = rotate(out.a, m.t);
  for (std::size_t i = 0; i < 3; ++i) out.t[i] = -moved[i];
  return true;
}

float finite(double value, std::size_t offset) {
  if (!std::isfinite(value)) throw parse_error(offset, "mesh value is not finite");
  return static_cast<float>(value);
}

// Invariant: the map takes NIF points to engine metres, so it carries the unit scale on both parts.
Affine point_map(const Affine& model, double metres_per_unit) {
  Affine out;
  out.a = multiply(k_engine_axes, model.a);
  out.t = rotate(k_engine_axes, model.t);
  for (std::size_t i = 0; i < 9; ++i) out.a[i] *= metres_per_unit;
  for (std::size_t i = 0; i < 3; ++i) out.t[i] *= metres_per_unit;
  return out;
}

// Invariant: a transform between two NIF spaces keeps its basis in the engine frame and only its origin takes the unit scale.
PlainTransform engine_transform(const Affine& m, double metres_per_unit, std::size_t offset) {
  const auto basis = multiply(multiply(k_engine_axes, m.a), k_engine_axes_inverse);
  const auto origin = rotate(k_engine_axes, m.t);
  PlainTransform out;
  for (std::size_t col = 0; col < 3; ++col) {
    for (std::size_t row = 0; row < 3; ++row) out.basis[col * 3 + row] = finite(basis[row * 3 + col], offset);
    out.origin[col] = finite(origin[col] * metres_per_unit, offset);
  }
  return out;
}

std::vector<float> points(const Affine& map, const std::vector<float>& verts, std::size_t offset) {
  std::vector<float> out(verts.size());
  for (std::size_t at = 0; at + 2 < verts.size(); at += 3) {
    const auto p = transform_point(map, Vec{verts[at], verts[at + 1], verts[at + 2]});
    for (std::size_t i = 0; i < 3; ++i) out[at + i] = finite(p[i], offset);
  }
  return out;
}

std::vector<float> directions(const Basis& map, const std::vector<float>& values, std::size_t width, bool mirrored) {
  std::vector<float> out(values.size());
  for (std::size_t at = 0; at + width <= values.size(); at += width) {
    auto v = rotate(map, Vec{values[at], values[at + 1], values[at + 2]});
    const double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0.0 && std::isfinite(length)) {
      for (auto& component : v) component /= length;
    }
    for (std::size_t i = 0; i < 3; ++i) out[at + i] = static_cast<float>(v[i]);
    if (width == 4) out[at + 3] = mirrored ? -values[at + 3] : values[at + 3];
  }
  return out;
}

std::vector<std::uint32_t> indices(const std::vector<std::uint16_t>& tris, bool flip) {
  std::vector<std::uint32_t> out(tris.begin(), tris.end());
  if (!flip) return out;
  for (std::size_t at = 0; at + 2 < out.size(); at += 3) std::swap(out[at + 1], out[at + 2]);
  return out;
}

void bounds(PlainPart& part) {
  constexpr float inf = std::numeric_limits<float>::infinity();
  float low[3]{inf, inf, inf};
  float high[3]{-inf, -inf, -inf};
  for (std::size_t at = 0; at + 2 < part.positions.size(); at += 3) {
    for (std::size_t i = 0; i < 3; ++i) {
      low[i] = std::min(low[i], part.positions[at + i]);
      high[i] = std::max(high[i], part.positions[at + i]);
    }
  }
  for (std::size_t i = 0; i < 3; ++i) {
    part.bounds_min[i] = low[i];
    part.bounds_max[i] = high[i];
    part.bounds_center[i] = (low[i] + high[i]) * 0.5f;
  }
  float farthest = 0.0f;
  for (std::size_t at = 0; at + 2 < part.positions.size(); at += 3) {
    float squared = 0.0f;
    for (std::size_t i = 0; i < 3; ++i) {
      const float d = part.positions[at + i] - part.bounds_center[i];
      squared += d * d;
    }
    farthest = std::max(farthest, squared);
  }
  part.bounds_radius = std::sqrt(farthest);
}

std::vector<float> unit_weights(const std::vector<float>& weights) {
  std::vector<float> out(weights.size());
  for (std::size_t at = 0; at + k_influences <= weights.size(); at += k_influences) {
    float sum = 0.0f;
    for (std::size_t k = 0; k < k_influences; ++k) {
      const float w = weights[at + k];
      out[at + k] = std::isfinite(w) && w > 0.0f ? w : 0.0f;
      sum += out[at + k];
    }
    if (sum <= 0.0f) continue;
    for (std::size_t k = 0; k < k_influences; ++k) out[at + k] /= sum;
  }
  return out;
}

Affine skin_to_bone(const Bone& bone) {
  Transform xf;
  for (std::size_t i = 0; i < 3; ++i) xf.t[i] = bone.translation[i];
  for (std::size_t i = 0; i < 9; ++i) xf.r[i] = bone.rotation[i];
  xf.s = bone.scale;
  return from_transform(xf);
}

struct SkinSpace {
  Affine skin_to_model;
  Affine model_to_skin;
};

// Constraint: the first bone whose node exists fixes the skin's model space, as niftool's globalToSkin did; with none, skin space is model space.
SkinSpace skin_space(const NifFile& nif, const Shape& shape, std::size_t offset) {
  SkinSpace out;
  for (const auto& bone : shape.bones) {
    if (bone.block < 0 || nif.node_for_block(static_cast<std::uint32_t>(bone.block)) < 0) continue;
    const auto to_global = from_transform(shape_to_world(nif, static_cast<std::uint32_t>(bone.block)));
    out.skin_to_model = compose(to_global, skin_to_bone(bone));
    if (!invert(out.skin_to_model, out.model_to_skin)) throw parse_error(offset, "skin transform has no inverse");
    break;
  }
  return out;
}

PlainSkin build_skin(const Shape& shape, const SkinSpace& space, double metres_per_unit, std::size_t offset) {
  PlainSkin skin;
  for (const auto& bone : shape.bones) {
    skin.bone_names.push_back(bone.name);
    skin.inverse_binds.push_back(engine_transform(compose(skin_to_bone(bone), space.model_to_skin), metres_per_unit, offset));
  }
  skin.bone_indices.assign(shape.bone_indices.begin(), shape.bone_indices.end());
  skin.bone_weights = unit_weights(shape.weights);
  for (std::size_t i = 0; i < skin.bone_indices.size(); ++i) {
    if (skin.bone_weights[i] == 0.0f) skin.bone_indices[i] = 0;
  }
  return skin;
}

bool block_is(const NifFile& nif, std::int32_t block, std::string_view type) {
  return block >= 0 && static_cast<std::size_t>(block) < nif.blocks().size() &&
         nif.blocks()[static_cast<std::size_t>(block)].type == type;
}

std::string slot(const std::vector<std::string>& textures, std::size_t index) {
  return index < textures.size() ? textures[index] : std::string{};
}

void apply_flags(PlainMaterial& material, const ShaderFlags& flags) {
  const auto names = shader_flag_names(flags);
  const auto on = [&names](std::string_view name) { return std::find(names.begin(), names.end(), name) != names.end(); };
  auto& out = material.flags;
  out.two_sided = on("Double_Sided");
  out.vertex_colors = on("Vertex_Colors");
  out.vertex_alpha = on("Vertex_Alpha");
  out.z_write = on("ZBuffer_Write");
  out.cast_shadows = on("Cast_Shadows");
  out.model_space_normals = on("Model_Space_Normals");
  out.decal = on("Decal");
  out.specular = on("Specular");
  out.environment_mapping = on("Environment_Mapping");
  out.own_emit = on("Own_Emit");
  out.glow_map = on("Glow_Map");
  out.palette_color = on("GreyscaleToPalette_Color");
  out.hair = on("Hair");
}

// Invariant: lighting texture slots are 0 diffuse, 1 normal, 2 glow, 3 greyscale, 4 cube, 5 environment mask, 7 smoothness and specular.
void read_lighting(const NifFile& nif, std::uint32_t block, PlainMaterial& material) {
  const auto shader = read_lighting_shader(nif, block);
  material.kind = PlainMaterialKind::lighting;
  material.key = shader.name;
  material.shader_kind = std::string(lighting_type_name(shader.shader_type));
  material.diffuse = slot(shader.textures, 0);
  material.normal = slot(shader.textures, 1);
  material.glow = slot(shader.textures, 2);
  material.greyscale = slot(shader.textures, 3);
  material.cubemap = slot(shader.textures, 4);
  material.env_mask = slot(shader.textures, 5);
  material.smooth_spec = slot(shader.textures, 7);
  std::copy_n(shader.uv_offset, 2, material.uv_offset);
  std::copy_n(shader.uv_scale, 2, material.uv_scale);
  material.has_skin_tint = shader.has_skin_tint;
  std::copy_n(shader.skin_tint, 3, material.skin_tint);
  apply_flags(material, shader.flags);
  material.shader_type = shader.shader_type;
  material.shader_flags1 = shader.flags.word1;
  material.shader_flags2 = shader.flags.word2;
  material.texture_clamp = shader.texture_clamp;
  std::copy_n(shader.emissive_color, 3, material.emissive_color);
  material.emissive_multiple = shader.emissive_multiple;
  material.env_map_scale = shader.env_map_scale;
  material.has_env_map = shader.has_env_map;
  material.root_material = shader.root_material;
  auto& values = material.lighting;
  values.glossiness = shader.smoothness;
  std::copy_n(shader.specular_color, 3, values.specular_color);
  values.specular_strength = shader.specular_strength;
  values.alpha = shader.alpha;
  values.fresnel_power = shader.fresnel_power;
  values.subsurface_rolloff = shader.subsurface_rolloff;
  values.rimlight_power = shader.rimlight_power;
  values.backlight_power = shader.backlight_power;
  values.has_backlight = shader.has_backlight;
  values.grayscale_to_palette_scale = shader.grayscale_to_palette_scale;
  values.has_grayscale_to_palette_scale = shader.has_grayscale_to_palette_scale;
}

// Invariant: effect texture slots are 0 source, 1 greyscale, 2 cube, 3 normal, 4 environment mask.
void read_effect(const NifFile& nif, std::uint32_t block, PlainMaterial& material) {
  const auto effect = read_effect_shader(nif, block);
  material.kind = PlainMaterialKind::effect;
  material.key = effect.name;
  material.shader_kind = "Effect";
  material.diffuse = effect.source_texture;
  material.greyscale = slot(effect.textures, 1);
  material.cubemap = slot(effect.textures, 2);
  material.normal = slot(effect.textures, 3);
  material.env_mask = slot(effect.textures, 4);
  std::copy_n(effect.uv_offset, 2, material.uv_offset);
  std::copy_n(effect.uv_scale, 2, material.uv_scale);
  apply_flags(material, effect.flags);
  material.shader_flags1 = effect.flags.word1;
  material.shader_flags2 = effect.flags.word2;
  material.texture_clamp = effect.texture_clamp;
  std::copy_n(effect.base_color, 3, material.emissive_color);
  material.emissive_multiple = effect.base_color_scale;
  material.env_map_scale = effect.env_map_scale;
  material.has_env_map = effect.offsets.env_map_scale != 0;
  auto& values = material.effect;
  std::copy_n(effect.base_color, 4, values.base_color);
  values.base_color_scale = effect.base_color_scale;
  values.falloff_start_angle = effect.falloff_start_angle;
  values.falloff_stop_angle = effect.falloff_stop_angle;
  values.falloff_start_opacity = effect.falloff_start_opacity;
  values.falloff_stop_opacity = effect.falloff_stop_opacity;
  values.soft_falloff_depth = effect.soft_falloff_depth;
}

PlainMaterial read_material(const NifFile& nif, const Shape& shape) {
  PlainMaterial material;
  const auto block = static_cast<std::uint32_t>(shape.shader_ref);
  const auto& type = nif.blocks().at(block).type;
  if (type == "BSLightingShaderProperty") {
    read_lighting(nif, block, material);
  } else if (type == "BSEffectShaderProperty") {
    read_effect(nif, block, material);
  } else {
    material.kind = PlainMaterialKind::other;
    material.shader_kind = type;
  }
  if (block_is(nif, shape.alpha_ref, "NiAlphaProperty")) {
    const auto alpha = read_alpha(nif, static_cast<std::uint32_t>(shape.alpha_ref));
    material.alpha_property = {true, static_cast<std::uint8_t>(alpha.source_blend),
                               static_cast<std::uint8_t>(alpha.destination_blend), alpha.threshold};
    if (alpha.blend_enabled) {
      material.alpha_mode = PlainAlphaMode::blend;
    } else if (alpha.test_enabled) {
      material.alpha_mode = PlainAlphaMode::mask;
      material.alpha_cutoff = static_cast<float>(alpha.threshold) / 255.0f;
    }
  }
  return material;
}

class MaterialTable {
public:
  std::int32_t index_of(const NifFile& nif, const Shape& shape) {
    const auto key = std::make_pair(shape.shader_ref, shape.alpha_ref);
    if (const auto found = index_.find(key); found != index_.end()) return found->second;
    materials_.push_back(read_material(nif, shape));
    const auto at = static_cast<std::int32_t>(materials_.size() - 1);
    index_.emplace(key, at);
    return at;
  }

  std::vector<PlainMaterial> take() { return std::move(materials_); }

private:
  std::map<std::pair<std::int32_t, std::int32_t>, std::int32_t> index_;
  std::vector<PlainMaterial> materials_;
};

double metres_per_unit(const PlainMeshOptions& options) {
  const float units = options.units_per_metre == 0.0f ? k_hknp_game_units_per_metre : options.units_per_metre;
  if (!std::isfinite(units) || units <= 0.0f) throw std::invalid_argument("units_per_metre must be finite and positive");
  return 1.0 / static_cast<double>(units);
}

bool is_skinned(const Shape& shape, std::size_t vertices) {
  return !shape.bones.empty() && shape.bone_indices.size() == vertices * k_influences &&
         shape.weights.size() == vertices * k_influences;
}

PlainPart build_part(const NifFile& nif, const Shape& shape, double scale, bool flip, MaterialTable& materials) {
  const std::size_t offset = nif.blocks().at(shape.block).offset;
  const std::size_t count = shape.verts.size() / 3;
  const bool skinned = is_skinned(shape, count);

  SkinSpace space;
  Affine model;
  if (skinned) {
    space = skin_space(nif, shape, offset);
    model = space.skin_to_model;
  } else {
    model = from_transform(shape_to_global(nif, shape.block));
  }
  const Basis direction_map = multiply(k_engine_axes, model.a);
  const bool mirrored = determinant(model.a) < 0.0;

  PlainPart part;
  part.block = shape.block;
  part.name = shape.name;
  part.warning = shape.warning;
  part.positions = points(point_map(model, scale), shape.verts, offset);
  const bool has_normals = shape.normals.size() == shape.verts.size();
  if (has_normals) part.normals = directions(direction_map, shape.normals, 3, false);
  if (has_normals && shape.tangents.size() == shape.verts.size() && shape.bitangents.size() == shape.verts.size()) {
    part.tangents = directions(direction_map, gltf::tangent_frames(shape), 4, mirrored);
  }
  if (shape.uvs.size() == count * 2) part.uv0 = shape.uvs;
  if (shape.uvs2.size() == count * 2) part.uv1 = shape.uvs2;
  if (shape.colors.size() == count * 4) part.colors = shape.colors;
  part.indices = indices(shape.tris, flip != mirrored);
  bounds(part);
  if (skinned) part.skin = build_skin(shape, space, scale, offset);

  if (shape.shader_ref >= 0 && static_cast<std::size_t>(shape.shader_ref) < nif.blocks().size()) {
    try {
      part.material = materials.index_of(nif, shape);
    } catch (const parse_error& error) {
      const std::string note = std::string{"material: "} + error.parse_message();
      part.warning = part.warning.empty() ? note : part.warning + "; " + note;
    }
  }
  return part;
}

}  // namespace

PlainMesh read_plain_mesh(const NifFile& nif, const std::vector<Shape>& shapes, const PlainMeshOptions& options) {
  const double scale = metres_per_unit(options);
  PlainMesh mesh;
  MaterialTable materials;
  for (const auto& shape : shapes) {
    if (shape.verts.empty() || shape.tris.empty()) continue;
    mesh.parts.push_back(build_part(nif, shape, scale, options.flip_winding, materials));
  }
  mesh.materials = materials.take();
  return mesh;
}

PlainMesh read_plain_mesh(const NifFile& nif, const PlainMeshOptions& options) {
  return read_plain_mesh(nif, read_shapes(nif), options);
}

}  // namespace fo4::nif
