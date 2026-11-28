#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::nif {

// Invariant: the output frame is x right, y up, z toward the viewer (NIF x, z, -y), in metres.
struct PlainMeshOptions {
  // Constraint: 0 selects k_hknp_game_units_per_metre; otherwise it must be finite and positive.
  float units_per_metre{0.0f};
  // Invariant: a NIF winding is counter-clockwise, so a clockwise-front consumer sets this.
  bool flip_winding{false};
};

// Invariant: basis is column-major with the uniform scale folded in, so basis[0..2] is the image of the x axis.
struct PlainTransform {
  float basis[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float origin[3]{};
};

// Invariant: bone_indices and bone_weights hold four entries per vertex, and the weights of a vertex sum to 1 unless all are 0.
struct PlainSkin {
  std::vector<std::string> bone_names;
  std::vector<PlainTransform> inverse_binds;
  std::vector<std::uint16_t> bone_indices;
  std::vector<float> bone_weights;
};

// Invariant: an optional attribute is empty when the NIF lacks it; tangents hold four floats per vertex, xyz then the glTF handedness sign.
struct PlainPart {
  std::uint32_t block{0};
  std::string name;
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> tangents;
  std::vector<float> uv0;
  std::vector<float> uv1;
  std::vector<float> colors;
  std::vector<std::uint32_t> indices;
  float bounds_min[3]{};
  float bounds_max[3]{};
  float bounds_center[3]{};
  float bounds_radius{0.0f};
  std::int32_t material{-1};
  std::optional<PlainSkin> skin;
  std::string warning;
};

// Invariant: kind none never appears in PlainMesh::materials.
enum class PlainMaterialKind : std::uint8_t { none = 0, lighting = 1, effect = 2, other = 3 };
enum class PlainAlphaMode : std::uint8_t { opaque = 0, mask = 1, blend = 2 };

struct PlainMaterialFlags {
  bool two_sided{false};
  bool vertex_colors{false};
  bool vertex_alpha{false};
  bool z_write{false};
  bool cast_shadows{false};
  bool model_space_normals{false};
  bool decal{false};
  bool specular{false};
  bool environment_mapping{false};
  bool own_emit{false};
  bool glow_map{false};
  bool palette_color{false};
  bool hair{false};
};

// Invariant: zero for any other kind; has_backlight and has_grayscale_to_palette_scale say whether the block stores that field.
struct PlainLightingValues {
  float glossiness{0.0f};
  float specular_color[3]{};
  float specular_strength{0.0f};
  float alpha{0.0f};
  float fresnel_power{0.0f};
  float subsurface_rolloff{0.0f};
  float rimlight_power{0.0f};
  float backlight_power{0.0f};
  bool has_backlight{false};
  float grayscale_to_palette_scale{0.0f};
  bool has_grayscale_to_palette_scale{false};
};

// Invariant: zero unless the material is an effect shader.
struct PlainEffectValues {
  float base_color[4]{};
  float base_color_scale{0.0f};
  float falloff_start_angle{0.0f};
  float falloff_stop_angle{0.0f};
  float falloff_start_opacity{0.0f};
  float falloff_stop_opacity{0.0f};
  float soft_falloff_depth{0.0f};
};

// Constraint: the blend factors are NiAlphaProperty enum values and mean something only when present is true.
struct PlainAlphaProperty {
  bool present{false};
  std::uint8_t source_blend{0};
  std::uint8_t destination_blend{0};
  std::uint8_t threshold{0};
};

// Invariant: key is the shader's name, which is the .bgsm or .bgem path when the shape names one; texture paths are stored text, unresolved.
struct PlainMaterial {
  std::string key;
  PlainMaterialKind kind{PlainMaterialKind::none};
  std::string shader_kind;
  std::string diffuse;
  std::string normal;
  std::string glow;
  std::string greyscale;
  std::string cubemap;
  std::string env_mask;
  std::string smooth_spec;
  PlainAlphaMode alpha_mode{PlainAlphaMode::opaque};
  float alpha_cutoff{0.0f};
  float uv_offset[2]{};
  float uv_scale[2]{1.0f, 1.0f};
  bool has_skin_tint{false};
  float skin_tint[3]{};
  PlainMaterialFlags flags;
  // Invariant: raw block values, zero for a shader the kind does not read; an effect shader reports its base colour and scale as emissive.
  std::uint32_t shader_type{0};
  std::uint32_t shader_flags1{0};
  std::uint32_t shader_flags2{0};
  std::uint32_t texture_clamp{0};
  float emissive_color[3]{};
  float emissive_multiple{0.0f};
  float env_map_scale{0.0f};
  bool has_env_map{false};
  std::string root_material;
  PlainLightingValues lighting;
  PlainEffectValues effect;
  PlainAlphaProperty alpha_property;
};

struct PlainMesh {
  std::vector<PlainPart> parts;
  std::vector<PlainMaterial> materials;
};

// Requires shapes to be read_shapes(nif); throws std::invalid_argument for bad options and parse_error for a non-finite or singular transform.
PlainMesh read_plain_mesh(const NifFile& nif, const std::vector<Shape>& shapes, const PlainMeshOptions& options = {});
PlainMesh read_plain_mesh(const NifFile& nif, const PlainMeshOptions& options = {});

}  // namespace fo4::nif
