#include "c_api_env_internal.hpp"
#include "c_api_nif_internal.hpp"
#include "fo4/c_api_materials.h"
#include "fo4/environment/material_swap.hpp"
#include "fo4/material/resolved.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct SwapHolder {
  radicle_material_swap view{};
  std::vector<fo4::env::MaterialSubstitution> data;
  std::vector<radicle_material_substitution> substitutions;
};

radicle_status require(const radicle_env* env, const radicle_record* record, const void* out) {
  if (env == nullptr || record == nullptr || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and an output are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return RADICLE_STATUS_OK;
}

struct ResolvedHolder {
  radicle_resolved_material view{};
  fo4::material::ResolvedMaterial value;
  std::vector<const char*> textures;
  std::vector<const char*> warnings;
};

radicle_resolved_material_source c_source(fo4::material::ResolvedMaterial::Source source) {
  using Source = fo4::material::ResolvedMaterial::Source;
  switch (source) {
    case Source::bgsm:
      return RADICLE_RESOLVED_MATERIAL_SOURCE_BGSM;
    case Source::bgem:
      return RADICLE_RESOLVED_MATERIAL_SOURCE_BGEM;
    case Source::nif_only:
      break;
  }
  return RADICLE_RESOLVED_MATERIAL_SOURCE_NIF_ONLY;
}

radicle_resolved_alpha_mode c_alpha_mode(fo4::material::AlphaMode mode) {
  switch (mode) {
    case fo4::material::AlphaMode::test:
      return RADICLE_RESOLVED_ALPHA_MODE_TEST;
    case fo4::material::AlphaMode::blend:
      return RADICLE_RESOLVED_ALPHA_MODE_BLEND;
    case fo4::material::AlphaMode::opaque:
      break;
  }
  return RADICLE_RESOLVED_ALPHA_MODE_OPAQUE;
}

void fill_view(ResolvedHolder& holder) {
  const auto& v = holder.value;
  auto& c = holder.view;
  holder.textures.clear();
  for (const auto& texture : v.textures) holder.textures.push_back(texture.c_str());
  holder.warnings.clear();
  for (const auto& warning : v.warnings) holder.warnings.push_back(warning.c_str());
  c.source = c_source(v.source);
  c.nif_material_path = v.nif_material_path.c_str();
  c.material_path = v.material_path.c_str();
  c.textures = holder.textures.data();
  c.texture_count = holder.textures.size();
  c.alpha_mode = c_alpha_mode(v.alpha_mode);
  c.alpha_threshold = v.alpha_threshold;
  c.blend_src = static_cast<uint8_t>(v.blend_src);
  c.blend_dst = static_cast<uint8_t>(v.blend_dst);
  c.alpha = v.alpha;
  c.two_sided = v.two_sided ? 1 : 0;
  c.has_glow = v.has_glow ? 1 : 0;
  c.has_wet = v.has_wet ? 1 : 0;
  c.uv_offset[0] = v.uv_offset[0];
  c.uv_offset[1] = v.uv_offset[1];
  c.uv_scale[0] = v.uv_scale[0];
  c.uv_scale[1] = v.uv_scale[1];
  c.emissive = {v.emissive.r, v.emissive.g, v.emissive.b};
  c.emissive_mult = v.emissive_mult;
  c.swapped = v.swapped ? 1 : 0;
  set_optional(v.color_remapping_index, c.has_color_remapping_index, c.color_remapping_index);
  c.warnings = holder.warnings.empty() ? nullptr : holder.warnings.data();
  c.warning_count = holder.warnings.size();
  c.internal = &holder;
}

const fo4::nif::Shape* resolve_target(const radicle_nif* nif, size_t shape_index, const radicle_assets* assets,
                                      radicle_status& status) {
  if (nif == nullptr || assets == nullptr) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "nif and assets are required");
    return nullptr;
  }
  if (shape_index >= nif->shapes.size()) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "shape index is out of range");
    return nullptr;
  }
  status = RADICLE_STATUS_OK;
  return &nif->shapes[shape_index];
}

}  // namespace

extern "C" {

radicle_status radicle_material_swap_decode(const radicle_env* env,
                                            const radicle_record* record,
                                            radicle_material_swap** out_swap) {
  clear_error();
  if (const auto status = require(env, record, out_swap); status != RADICLE_STATUS_OK) return status;
  *out_swap = nullptr;
  return guarded("unknown exception while decoding MSWP", [&] {
    auto holder = std::make_unique<SwapHolder>();
    holder->data = fo4::env::decode_material_swap(env->value, *to_ref(record));
    for (const auto& s : holder->data) {
      radicle_material_substitution c{};
      c.original_material = stored_text(*env, s.original_material);
      c.replacement_material = stored_text(*env, s.replacement_material);
      set_optional(s.color_remapping_index, c.has_color_remapping_index, c.color_remapping_index);
      holder->substitutions.push_back(c);
    }
    auto& v = holder->view;
    v.substitutions = holder->substitutions.empty() ? nullptr : holder->substitutions.data();
    v.substitution_count = holder->substitutions.size();
    v.internal = holder.get();
    *out_swap = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_material_swap_free(radicle_material_swap* swap) {
  clear_error();
  if (swap != nullptr) delete static_cast<SwapHolder*>(swap->internal);
}

radicle_status radicle_placed_reference_material_swap(const radicle_env* env,
                                                      const radicle_record* record,
                                                      radicle_form_key* out_swap) {
  clear_error();
  if (const auto status = require(env, record, out_swap); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading XMSP", [&] {
    *out_swap = to_c_key(*env, fo4::env::placed_material_swap(env->value, *to_ref(record)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_primary_model_material_swap(const radicle_env* env,
                                                          const radicle_record* record,
                                                          radicle_form_key* out_swap) {
  clear_error();
  if (const auto status = require(env, record, out_swap); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading the model material swap", [&] {
    *out_swap = to_c_key(*env, fo4::env::primary_model_material_swap(env->value, *to_ref(record)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_primary_model_color_remapping_index(const radicle_env* env,
                                                                 const radicle_record* record,
                                                                 float* out_index) {
  clear_error();
  if (const auto status = require(env, record, out_index); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading the model color remapping index", [&] {
    const auto index = fo4::env::primary_model_color_remapping_index(env->value, *to_ref(record));
    if (!index) return fail(RADICLE_STATUS_NOT_FOUND, "the primary model has no color remapping index");
    *out_index = *index;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_material_resolve(const radicle_nif* nif,
                                        size_t shape_index,
                                        const radicle_assets* assets,
                                        radicle_resolved_material** out_material) {
  clear_error();
  if (out_material == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_material is required");
  *out_material = nullptr;
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = resolve_target(nif, shape_index, assets, status);
  if (shape == nullptr) return status;
  return guarded("unknown exception while resolving a material", [&] {
    auto holder = std::make_unique<ResolvedHolder>();
    holder->value = fo4::material::resolve_material(nif->file, *shape, assets->value);
    fill_view(*holder);
    *out_material = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_resolved_material_apply_swap(radicle_resolved_material* material,
                                                    const radicle_nif* nif,
                                                    size_t shape_index,
                                                    const radicle_assets* assets,
                                                    const radicle_material_swap* swap,
                                                    int32_t* out_applied) {
  clear_error();
  if (material == nullptr || swap == nullptr || out_applied == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "material, swap and out_applied are required");
  }
  if (swap->substitution_count != 0 && swap->substitutions == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swap has a count but no substitutions");
  }
  radicle_status status = RADICLE_STATUS_OK;
  const auto* shape = resolve_target(nif, shape_index, assets, status);
  if (shape == nullptr) return status;
  *out_applied = 0;
  return guarded("unknown exception while applying a material swap", [&] {
    std::vector<fo4::env::MaterialSubstitution> substitutions;
    for (size_t i = 0; i < swap->substitution_count; ++i) {
      const auto& s = swap->substitutions[i];
      if (s.original_material == nullptr) continue;
      fo4::env::MaterialSubstitution c{s.original_material, std::nullopt, std::nullopt};
      if (s.replacement_material != nullptr) c.replacement_material = std::string_view(s.replacement_material);
      if (s.has_color_remapping_index != 0) c.color_remapping_index = s.color_remapping_index;
      substitutions.push_back(c);
    }
    auto& holder = *static_cast<ResolvedHolder*>(material->internal);
    *out_applied = fo4::material::apply_material_swap(holder.value, nif->file, *shape, assets->value, substitutions) ? 1 : 0;
    fill_view(holder);
    return RADICLE_STATUS_OK;
  });
}

void radicle_resolved_material_free(radicle_resolved_material* material) {
  clear_error();
  if (material != nullptr) delete static_cast<ResolvedHolder*>(material->internal);
}

}  // extern "C"
