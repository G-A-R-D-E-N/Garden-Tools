#include "fo4/c_api_nif.h"

#include "c_api_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/shader.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>

namespace {

using namespace radicle_c_detail;

constexpr size_t k_lighting_wetness_values = 6;
constexpr const char* k_lighting_type = "BSLightingShaderProperty";
constexpr const char* k_effect_type = "BSEffectShaderProperty";
constexpr const char* k_alpha_type = "NiAlphaProperty";

template <typename Body>
radicle_status guarded(const char* what, Body body) {
  try {
    return body();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::parse_error& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

radicle_status copy_text(const std::string& value, uint8_t** out_bytes, size_t* out_size) {
  if (!out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (value.empty()) return RADICLE_STATUS_OK;
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate a NIF string");
  std::memcpy(buffer, value.data(), value.size());
  *out_bytes = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

// Invariant: a shader accessor answers only for a block of its own type, so a caller cannot read defaults instead.
radicle_status require_shader_block(const radicle_nif* nif, uint32_t block, const char* type, radicle_status& status) {
  if (!nif) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif is required");
    return status;
  }
  if (block >= nif->file.blocks().size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "block index is out of range");
    return status;
  }
  if (nif->file.blocks()[block].type != type) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "block is not a shader property of the requested type");
    return status;
  }
  status = RADICLE_STATUS_OK;
  return status;
}

}  // namespace

extern "C" {

radicle_status radicle_nif_read_lighting_shader(const radicle_nif* nif,
                                           uint32_t block,
                                           radicle_nif_lighting_shader* out_shader) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_lighting_type, status) != RADICLE_STATUS_OK) return status;
  if (!out_shader) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_shader is required");
  return guarded("unknown exception while reading a lighting shader", [&] {
    const auto shader = fo4::nif::read_lighting_shader(nif->file, block);
    radicle_nif_lighting_shader out{};
    out.block = shader.block;
    out.shader_type = shader.shader_type;
    out.flags1 = shader.flags.word1;
    out.flags2 = shader.flags.word2;
    std::memcpy(out.uv_offset, shader.uv_offset, sizeof out.uv_offset);
    std::memcpy(out.uv_scale, shader.uv_scale, sizeof out.uv_scale);
    out.texture_set_ref = shader.texture_set_ref;
    out.texture_count = shader.textures.size();
    std::memcpy(out.emissive_color, shader.emissive_color, sizeof out.emissive_color);
    out.emissive_multiple = shader.emissive_multiple;
    out.has_root_material = shader.has_root_material ? 1 : 0;
    out.texture_clamp = shader.texture_clamp;
    out.alpha = shader.alpha;
    out.refraction_strength = shader.refraction_strength;
    out.smoothness = shader.smoothness;
    std::memcpy(out.specular_color, shader.specular_color, sizeof out.specular_color);
    out.specular_strength = shader.specular_strength;
    out.subsurface_rolloff = shader.subsurface_rolloff;
    out.rimlight_power = shader.rimlight_power;
    out.has_backlight = shader.has_backlight ? 1 : 0;
    out.backlight_power = shader.backlight_power;
    out.has_grayscale_to_palette_scale = shader.has_grayscale_to_palette_scale ? 1 : 0;
    out.grayscale_to_palette_scale = shader.grayscale_to_palette_scale;
    out.fresnel_power = shader.fresnel_power;
    out.has_wetness = shader.has_wetness ? 1 : 0;
    const float wetness[k_lighting_wetness_values] = {
        shader.wetness.spec_scale, shader.wetness.spec_power, shader.wetness.min_var,
        shader.wetness.env_map_scale, shader.wetness.fresnel_power, shader.wetness.metalness};
    std::memcpy(out.wetness, wetness, sizeof wetness);
    out.has_env_map = shader.has_env_map ? 1 : 0;
    out.env_map_scale = shader.env_map_scale;
    out.env_map_ssr = shader.env_map_ssr;
    out.env_map_wetness_ssr = shader.env_map_wetness_ssr;
    out.has_skin_tint = shader.has_skin_tint ? 1 : 0;
    std::memcpy(out.skin_tint, shader.skin_tint, sizeof out.skin_tint);
    out.has_hair_tint = shader.has_hair_tint ? 1 : 0;
    std::memcpy(out.hair_tint, shader.hair_tint, sizeof out.hair_tint);
    out.has_snow = shader.has_snow ? 1 : 0;
    std::memcpy(out.snow, shader.snow, sizeof out.snow);
    out.has_parallax_occ = shader.has_parallax_occ ? 1 : 0;
    std::memcpy(out.parallax_occ, shader.parallax_occ, sizeof out.parallax_occ);
    out.has_multi_layer_parallax = shader.has_multi_layer_parallax ? 1 : 0;
    std::memcpy(out.multi_layer_parallax, shader.multi_layer_parallax, sizeof out.multi_layer_parallax);
    out.has_eye = shader.has_eye ? 1 : 0;
    out.eye_cubemap_scale = shader.eye_cubemap_scale;
    std::memcpy(out.eye_left, shader.eye_left, sizeof out.eye_left);
    std::memcpy(out.eye_right, shader.eye_right, sizeof out.eye_right);
    out.tail_bytes = shader.tail_bytes;
    *out_shader = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_lighting_shader_name(const radicle_nif* nif,
                                                uint32_t block,
                                                uint8_t** out_bytes,
                                                size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_lighting_type, status) != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading a shader name", [&] {
    return copy_text(fo4::nif::read_lighting_shader(nif->file, block).name, out_bytes, out_size);
  });
}

radicle_status radicle_nif_lighting_shader_root_material(const radicle_nif* nif,
                                                         uint32_t block,
                                                         uint8_t** out_bytes,
                                                         size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_lighting_type, status) != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading a root material", [&] {
    return copy_text(fo4::nif::read_lighting_shader(nif->file, block).root_material, out_bytes, out_size);
  });
}

radicle_status radicle_nif_read_effect_shader(const radicle_nif* nif,
                                         uint32_t block,
                                         radicle_nif_effect_shader* out_shader) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_effect_type, status) != RADICLE_STATUS_OK) return status;
  if (!out_shader) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_shader is required");
  return guarded("unknown exception while reading an effect shader", [&] {
    const auto shader = fo4::nif::read_effect_shader(nif->file, block);
    radicle_nif_effect_shader out{};
    out.block = shader.block;
    out.flags1 = shader.flags.word1;
    out.flags2 = shader.flags.word2;
    std::memcpy(out.uv_offset, shader.uv_offset, sizeof out.uv_offset);
    std::memcpy(out.uv_scale, shader.uv_scale, sizeof out.uv_scale);
    out.texture_clamp = shader.texture_clamp;
    out.lighting_influence = shader.lighting_influence;
    out.env_map_min_lod = shader.env_map_min_lod;
    out.unused = shader.unused;
    out.falloff_start_angle = shader.falloff_start_angle;
    out.falloff_stop_angle = shader.falloff_stop_angle;
    out.falloff_start_opacity = shader.falloff_start_opacity;
    out.falloff_stop_opacity = shader.falloff_stop_opacity;
    std::memcpy(out.base_color, shader.base_color, sizeof out.base_color);
    out.base_color_scale = shader.base_color_scale;
    out.soft_falloff_depth = shader.soft_falloff_depth;
    out.texture_count = shader.textures.size();
    out.env_map_scale = shader.env_map_scale;
    *out_shader = out;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_nif_effect_shader_name(const radicle_nif* nif,
                                              uint32_t block,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_lighting_type, status) != RADICLE_STATUS_OK) return status;
  if (require_shader_block(nif, block, k_effect_type, status) != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading an effect shader name", [&] {
    return copy_text(fo4::nif::read_effect_shader(nif->file, block).name, out_bytes, out_size);
  });
}

radicle_status radicle_nif_effect_shader_texture(const radicle_nif* nif,
                                                 uint32_t block,
                                                 size_t slot,
                                                 uint8_t** out_bytes,
                                                 size_t* out_size) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_lighting_type, status) != RADICLE_STATUS_OK) return status;
  if (require_shader_block(nif, block, k_effect_type, status) != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading an effect shader texture", [&] {
    const auto shader = fo4::nif::read_effect_shader(nif->file, block);
    if (slot >= shader.textures.size()) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "texture slot is out of range");
    }
    return copy_text(shader.textures[slot], out_bytes, out_size);
  });
}

radicle_status radicle_nif_read_alpha_property(const radicle_nif* nif,
                                         uint32_t block,
                                         radicle_nif_alpha_property* out_alpha) {
  clear_error();
  radicle_status status = RADICLE_STATUS_OK;
  if (require_shader_block(nif, block, k_alpha_type, status) != RADICLE_STATUS_OK) return status;
  if (!out_alpha) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_alpha is required");
  return guarded("unknown exception while reading an alpha property", [&] {
    const auto alpha = fo4::nif::read_alpha(nif->file, block);
    out_alpha->block = alpha.block;
    out_alpha->flags = alpha.flags;
    out_alpha->threshold = alpha.threshold;
    out_alpha->blend_enabled = alpha.blend_enabled ? 1 : 0;
    out_alpha->source_blend = static_cast<uint8_t>(alpha.source_blend);
    out_alpha->destination_blend = static_cast<uint8_t>(alpha.destination_blend);
    out_alpha->test_enabled = alpha.test_enabled ? 1 : 0;
    out_alpha->test_function = static_cast<uint8_t>(alpha.test_function);
    return RADICLE_STATUS_OK;
  });
}

size_t radicle_nif_shader_flag_count(void) {
  clear_error();
  return fo4::nif::shader_flag_count();
}

radicle_status radicle_nif_shader_flag_name(uint32_t bit, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  const auto* name = fo4::nif::shader_flag_name(bit);
  if (name == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "shader flag bit is past the 64-bit flag word");
  return copy_text(name, out_bytes, out_size);
}

}  // namespace
