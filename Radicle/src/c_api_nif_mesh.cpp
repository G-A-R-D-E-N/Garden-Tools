#include "fo4/c_api_nif_mesh.h"

#include "c_api_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/nif/mesh_plain.hpp"
#include "fo4/nif/tri_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;

struct MeshHolder {
  radicle_mesh view{};
  fo4::nif::PlainMesh data;
  std::vector<radicle_mesh_part> parts;
  std::vector<radicle_mesh_material> materials;
  std::vector<radicle_mesh_skin> skins;
  std::vector<std::vector<const char*>> bone_names;
  std::vector<std::vector<radicle_mesh_transform>> binds;
};

template <typename Values>
const typename Values::value_type* data_or_null(const Values& values) {
  return values.empty() ? nullptr : values.data();
}

radicle_mesh_transform to_c(const fo4::nif::PlainTransform& value) {
  radicle_mesh_transform out{};
  std::copy_n(value.basis, 9, out.basis);
  std::copy_n(value.origin, 3, out.origin);
  return out;
}

void fill_skin(MeshHolder& holder, std::size_t index, const fo4::nif::PlainSkin& skin) {
  auto& names = holder.bone_names[index];
  for (const auto& name : skin.bone_names) names.push_back(name.c_str());
  auto& binds = holder.binds[index];
  for (const auto& bind : skin.inverse_binds) binds.push_back(to_c(bind));
  auto& out = holder.skins[index];
  out.bone_count = skin.bone_names.size();
  out.bone_names = data_or_null(names);
  out.inverse_binds = data_or_null(binds);
  out.bone_indices = data_or_null(skin.bone_indices);
  out.bone_weights = data_or_null(skin.bone_weights);
}

radicle_mesh_part to_c(const fo4::nif::PlainPart& part) {
  radicle_mesh_part out{};
  out.name = part.name.c_str();
  out.block = part.block;
  out.vertex_count = part.positions.size() / 3;
  out.index_count = part.indices.size();
  out.positions = data_or_null(part.positions);
  out.normals = data_or_null(part.normals);
  out.tangents = data_or_null(part.tangents);
  out.uv0 = data_or_null(part.uv0);
  out.uv1 = data_or_null(part.uv1);
  out.colors = data_or_null(part.colors);
  out.indices = data_or_null(part.indices);
  std::copy_n(part.bounds_min, 3, out.bounds_min);
  std::copy_n(part.bounds_max, 3, out.bounds_max);
  std::copy_n(part.bounds_center, 3, out.bounds_center);
  out.bounds_radius = part.bounds_radius;
  out.material = part.material;
  out.warning = part.warning.c_str();
  return out;
}

radicle_mesh_material to_c(const fo4::nif::PlainMaterial& material) {
  radicle_mesh_material out{};
  out.key = material.key.c_str();
  out.shader_kind = material.shader_kind.c_str();
  out.kind = static_cast<std::uint8_t>(material.kind);
  out.alpha_mode = static_cast<std::uint8_t>(material.alpha_mode);
  out.alpha_cutoff = material.alpha_cutoff;
  out.diffuse = material.diffuse.c_str();
  out.normal = material.normal.c_str();
  out.glow = material.glow.c_str();
  out.greyscale = material.greyscale.c_str();
  out.cubemap = material.cubemap.c_str();
  out.env_mask = material.env_mask.c_str();
  out.smooth_spec = material.smooth_spec.c_str();
  std::copy_n(material.uv_offset, 2, out.uv_offset);
  std::copy_n(material.uv_scale, 2, out.uv_scale);
  out.has_skin_tint = material.has_skin_tint ? 1 : 0;
  std::copy_n(material.skin_tint, 3, out.skin_tint);
  const auto& flags = material.flags;
  out.two_sided = flags.two_sided ? 1 : 0;
  out.vertex_colors = flags.vertex_colors ? 1 : 0;
  out.vertex_alpha = flags.vertex_alpha ? 1 : 0;
  out.z_write = flags.z_write ? 1 : 0;
  out.cast_shadows = flags.cast_shadows ? 1 : 0;
  out.model_space_normals = flags.model_space_normals ? 1 : 0;
  out.decal = flags.decal ? 1 : 0;
  out.specular = flags.specular ? 1 : 0;
  out.environment_mapping = flags.environment_mapping ? 1 : 0;
  out.own_emit = flags.own_emit ? 1 : 0;
  out.glow_map = flags.glow_map ? 1 : 0;
  out.palette_color = flags.palette_color ? 1 : 0;
  out.hair = flags.hair ? 1 : 0;
  out.root_material = material.root_material.c_str();
  out.shader_type = material.shader_type;
  out.shader_flags1 = material.shader_flags1;
  out.shader_flags2 = material.shader_flags2;
  out.texture_clamp = material.texture_clamp;
  std::copy_n(material.emissive_color, 3, out.emissive_color);
  out.emissive_multiple = material.emissive_multiple;
  out.env_map_scale = material.env_map_scale;
  out.has_env_map = material.has_env_map ? 1 : 0;
  const auto& lighting = material.lighting;
  out.glossiness = lighting.glossiness;
  std::copy_n(lighting.specular_color, 3, out.specular_color);
  out.specular_strength = lighting.specular_strength;
  out.alpha = lighting.alpha;
  out.fresnel_power = lighting.fresnel_power;
  out.subsurface_rolloff = lighting.subsurface_rolloff;
  out.rimlight_power = lighting.rimlight_power;
  out.backlight_power = lighting.backlight_power;
  out.grayscale_to_palette_scale = lighting.grayscale_to_palette_scale;
  out.has_backlight = lighting.has_backlight ? 1 : 0;
  out.has_grayscale_to_palette_scale = lighting.has_grayscale_to_palette_scale ? 1 : 0;
  const auto& effect = material.effect;
  std::copy_n(effect.base_color, 4, out.effect_base_color);
  out.effect_base_color_scale = effect.base_color_scale;
  out.effect_falloff_start_angle = effect.falloff_start_angle;
  out.effect_falloff_stop_angle = effect.falloff_stop_angle;
  out.effect_falloff_start_opacity = effect.falloff_start_opacity;
  out.effect_falloff_stop_opacity = effect.falloff_stop_opacity;
  out.effect_soft_falloff_depth = effect.soft_falloff_depth;
  const auto& blend = material.alpha_property;
  out.has_alpha_property = blend.present ? 1 : 0;
  out.alpha_source_blend = blend.source_blend;
  out.alpha_destination_blend = blend.destination_blend;
  out.alpha_threshold = blend.threshold;
  return out;
}

std::unique_ptr<MeshHolder> build_holder(fo4::nif::PlainMesh&& mesh) {
  auto holder = std::make_unique<MeshHolder>();
  holder->data = std::move(mesh);
  const auto count = holder->data.parts.size();
  holder->skins.resize(count);
  holder->bone_names.resize(count);
  holder->binds.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& part = holder->data.parts[i];
    auto item = to_c(part);
    if (part.skin) {
      fill_skin(*holder, i, *part.skin);
      item.skin = &holder->skins[i];
    }
    holder->parts.push_back(item);
  }
  for (const auto& material : holder->data.materials) holder->materials.push_back(to_c(material));
  holder->view.parts = data_or_null(holder->parts);
  holder->view.part_count = holder->parts.size();
  holder->view.materials = data_or_null(holder->materials);
  holder->view.material_count = holder->materials.size();
  holder->view.internal = holder.get();
  return holder;
}

}  // namespace

struct radicle_tri {
  fo4::nif::TriFile file;
};

extern "C" {

radicle_status radicle_nif_mesh_read(const radicle_nif* nif,
                                     const radicle_mesh_options* options,
                                     radicle_mesh** out_mesh) {
  clear_error();
  if (out_mesh) *out_mesh = nullptr;
  if (!nif || !out_mesh) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and out_mesh are required");
  fo4::nif::PlainMeshOptions settings;
  if (options) {
    if ((options->flags & ~static_cast<std::uint32_t>(RADICLE_MESH_FLIP_WINDING)) != 0) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown radicle_mesh_options flag bits");
    }
    settings.units_per_metre = options->units_per_metre;
    settings.flip_winding = (options->flags & RADICLE_MESH_FLIP_WINDING) != 0;
  }
  return guard_exceptions("unknown exception while reading a plain NIF mesh", [&] {
    auto holder = build_holder(fo4::nif::read_plain_mesh(nif->file, nif->shapes, settings));
    *out_mesh = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_nif_mesh_free(radicle_mesh* mesh) {
  clear_error();
  if (!mesh) return;
  delete static_cast<MeshHolder*>(mesh->internal);
}

radicle_status radicle_tri_read(const uint8_t* bytes, size_t size, radicle_tri** out_tri) {
  clear_error();
  if (out_tri) *out_tri = nullptr;
  if (!bytes || size == 0 || !out_tri) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes, size and out_tri are required");
  return guard_exceptions("unknown exception while reading a tri file", [&] {
    auto tri = std::make_unique<radicle_tri>();
    tri->file = fo4::nif::parse_tri(std::as_bytes(std::span<const uint8_t>(bytes, size)));
    *out_tri = tri.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_tri_free(radicle_tri* tri) {
  clear_error();
  delete tri;
}

radicle_status radicle_tri_counts(const radicle_tri* tri,
                                  uint32_t* out_vertex_count,
                                  uint32_t* out_triangle_count,
                                  uint32_t* out_morph_count) {
  clear_error();
  if (out_vertex_count) *out_vertex_count = 0;
  if (out_triangle_count) *out_triangle_count = 0;
  if (out_morph_count) *out_morph_count = 0;
  if (!tri || !out_vertex_count || !out_triangle_count || !out_morph_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tri and every output are required");
  }
  *out_vertex_count = tri->file.vertex_count;
  *out_triangle_count = tri->file.triangle_count;
  *out_morph_count = static_cast<uint32_t>(tri->file.morphs.size());
  return RADICLE_STATUS_OK;
}

radicle_status radicle_tri_morph_name(const radicle_tri* tri, uint32_t index, const char** out_name) {
  clear_error();
  if (out_name) *out_name = nullptr;
  if (!tri || !out_name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tri and out_name are required");
  if (index >= tri->file.morphs.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tri morph index is out of range");
  *out_name = tri->file.morphs[index].name.c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_tri_positions(const radicle_tri* tri, float units_per_metre, float** out_values, size_t* out_count) {
  clear_error();
  if (out_values) *out_values = nullptr;
  if (out_count) *out_count = 0;
  if (!tri || !out_values || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tri and every output are required");
  return guard_exceptions("unknown exception while reading tri positions", [&] {
    const auto values = fo4::nif::tri_to_engine_frame(tri->file.positions, units_per_metre);
    return copy_out(values, out_values, out_count, {"tri and every output are required", "failed to allocate the tri positions"});
  });
}

radicle_status radicle_tri_morph_deltas(const radicle_tri* tri,
                                        uint32_t index,
                                        float units_per_metre,
                                        float** out_values,
                                        size_t* out_count) {
  clear_error();
  if (out_values) *out_values = nullptr;
  if (out_count) *out_count = 0;
  if (!tri || !out_values || !out_count) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tri and every output are required");
  if (index >= tri->file.morphs.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "tri morph index is out of range");
  return guard_exceptions("unknown exception while reading tri morph deltas", [&] {
    const auto values = fo4::nif::tri_to_engine_frame(fo4::nif::tri_morph_deltas(tri->file.morphs[index]), units_per_metre);
    return copy_out(values, out_values, out_count, {"tri and every output are required", "failed to allocate the tri morph deltas"});
  });
}

}  // extern "C"
