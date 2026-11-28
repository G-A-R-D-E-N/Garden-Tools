#include "fo4/nif/resolved.hpp"

#include "fo4/nif/material_link.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <utility>

namespace fo4::material {
namespace {

constexpr std::string_view k_lighting_type = "BSLightingShaderProperty";
constexpr std::string_view k_effect_type = "BSEffectShaderProperty";
constexpr std::string_view k_alpha_type = "NiAlphaProperty";
constexpr std::string_view k_wet_suffix = "_wet.bgsm";
constexpr std::string_view k_materials_prefix = "materials/";
constexpr std::string_view k_textures_prefix = "textures/";
constexpr std::uint32_t k_last_blend_mode = 10;

struct SlotField {
  std::string_view field;
  std::size_t slot;
};

constexpr SlotField k_bgsm_slots[] = {
    {"DiffuseTexture", k_slot_diffuse}, {"NormalTexture", k_slot_normal},   {"GlowTexture", k_slot_glow},
    {"GreyscaleTexture", k_slot_greyscale}, {"EnvmapTexture", k_slot_envmap}, {"SmoothSpecTexture", k_slot_smooth_spec},
};
constexpr SlotField k_bgem_slots[] = {
    {"BaseTexture", k_slot_diffuse}, {"NormalTexture", k_slot_normal}, {"GrayscaleTexture", k_slot_greyscale},
    {"EnvmapTexture", k_slot_envmap}, {"EnvmapMaskTexture", k_slot_envmask},
};
// Invariant: the order of an effect shader's textures in nif::EffectShader, mapped to the lighting layout.
constexpr std::size_t k_effect_nif_slots[] = {k_slot_diffuse, k_slot_greyscale, k_slot_envmap, k_slot_normal, k_slot_envmask};

struct Merge {
  ResolvedMaterial& out;
  const archive::AssetResolver& assets;
  bool effect{false};
  bool emit{false};
  bool glowmap{false};
  bool hair{false};
};

float unit_alpha(float alpha) { return std::isnan(alpha) ? 1.0F : std::clamp(alpha, 0.0F, 1.0F); }

std::string_view trimmed(std::string_view raw) {
  raw = text(raw);
  while (!raw.empty() && (raw.back() == '\0' || raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
  while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
  return raw;
}

bool has_name(const std::vector<std::string>& names, std::string_view name) {
  return std::ranges::find(names, name) != names.end();
}

std::string texture_path(std::string_view raw, std::vector<std::string>& warnings) {
  const auto cleaned = trimmed(raw);
  if (cleaned.empty()) return {};
  try {
    auto path = archive::normalize_asset_path(cleaned);
    if (!path.starts_with(k_textures_prefix)) path.insert(0, k_textures_prefix);
    return path;
  } catch (const archive::AssetPathError&) {
    warnings.push_back("texture path refused: " + std::string(cleaned));
    return {};
  }
}

std::string material_key(std::string_view raw) {
  const auto cleaned = trimmed(raw);
  if (cleaned.empty()) return {};
  try {
    return archive::normalize_asset_path(nif::data_relative_material_path(cleaned));
  } catch (const archive::AssetPathError&) {
    return {};
  }
}

std::string swap_key(std::string_view raw) {
  auto key = material_key(raw);
  if (key.starts_with(k_materials_prefix)) key.erase(0, k_materials_prefix.size());
  return key;
}

bool is_material_key(const std::string& key) { return key.ends_with(".bgsm") || key.ends_with(".bgem"); }

bool texture_exists(const Merge& m, const std::string& path) {
  try {
    return m.assets.locate(path).has_value();
  } catch (const std::exception&) {
    return false;
  }
}

void place(Merge& m, std::size_t slot, const std::string& path, bool authoritative) {
  if (path.empty()) return;
  auto& current = m.out.textures[slot];
  if (current.empty() || current == path) {
    current = path;
    return;
  }
  if (authoritative && (texture_exists(m, path) || !texture_exists(m, current))) current = path;
}

void read_lighting(const nif::NifFile& file, std::uint32_t block, Merge& m, std::string& root_material) {
  const auto shader = nif::read_lighting_shader(file, block);
  auto& out = m.out;
  for (std::size_t i = 0; i < std::min(shader.textures.size(), out.textures.size()); ++i) {
    out.textures[i] = texture_path(shader.textures[i], out.warnings);
  }
  out.uv_offset = {shader.uv_offset[0], shader.uv_offset[1]};
  out.uv_scale = {shader.uv_scale[0], shader.uv_scale[1]};
  out.emissive = {shader.emissive_color[0], shader.emissive_color[1], shader.emissive_color[2]};
  out.emissive_mult = shader.emissive_multiple;
  out.alpha = unit_alpha(shader.alpha);
  const auto flags = nif::shader_flag_names(shader.flags);
  out.two_sided = has_name(flags, "Double_Sided");
  m.emit = has_name(flags, "Own_Emit");
  m.glowmap = has_name(flags, "Glow_Map");
  m.hair = has_name(flags, "Hair");
  if (shader.has_root_material) root_material = shader.root_material;
}

void read_effect(const nif::NifFile& file, std::uint32_t block, Merge& m) {
  const auto shader = nif::read_effect_shader(file, block);
  auto& out = m.out;
  m.effect = true;
  for (std::size_t i = 0; i < std::min(shader.textures.size(), std::size(k_effect_nif_slots)); ++i) {
    out.textures[k_effect_nif_slots[i]] = texture_path(shader.textures[i], out.warnings);
  }
  out.uv_offset = {shader.uv_offset[0], shader.uv_offset[1]};
  out.uv_scale = {shader.uv_scale[0], shader.uv_scale[1]};
  out.emissive = {shader.base_color[0], shader.base_color[1], shader.base_color[2]};
  out.emissive_mult = shader.base_color_scale;
  out.alpha = unit_alpha(shader.base_color[3]);
  out.two_sided = has_name(nif::shader_flag_names(shader.flags), "Double_Sided");
}

void read_nif_side(const nif::NifFile& file, const nif::Shape& shape, Merge& m, std::string& root_material) {
  auto& out = m.out;
  const auto& blocks = file.blocks();
  try {
    if (shape.shader_ref >= 0 && static_cast<std::size_t>(shape.shader_ref) < blocks.size()) {
      const auto block = static_cast<std::uint32_t>(shape.shader_ref);
      if (blocks[block].type == k_lighting_type) {
        read_lighting(file, block, m, root_material);
      } else if (blocks[block].type == k_effect_type) {
        read_effect(file, block, m);
      } else {
        out.warnings.push_back("shader " + blocks[block].type + " is not a lighting or effect shader");
      }
    }
    if (shape.alpha_ref >= 0 && static_cast<std::size_t>(shape.alpha_ref) < blocks.size() &&
        blocks[static_cast<std::size_t>(shape.alpha_ref)].type == k_alpha_type) {
      const auto property = nif::read_alpha(file, static_cast<std::uint32_t>(shape.alpha_ref));
      out.alpha_mode = property.blend_enabled  ? AlphaMode::blend
                       : property.test_enabled ? AlphaMode::test
                                               : AlphaMode::opaque;
      out.alpha_threshold = property.threshold;
      out.blend_src = property.source_blend;
      out.blend_dst = property.destination_blend;
    }
  } catch (const std::exception& error) {
    out.warnings.push_back(std::string("shader not read: ") + error.what());
  }
}

std::optional<MaterialFile> load_material(const archive::AssetResolver& assets, std::string& key,
                                          std::vector<std::string>& warnings) {
  try {
    auto location = assets.locate(key);
    if (!location && !key.starts_with(k_materials_prefix)) {
      const auto rooted = std::string(k_materials_prefix) + key;
      if ((location = assets.locate(rooted))) key = rooted;
    }
    if (!location) {
      warnings.push_back("material not found: " + key);
      return std::nullopt;
    }
    return read_material_file(assets.read(*location));
  } catch (const std::exception& error) {
    warnings.push_back("material not read: " + key + " (" + error.what() + ")");
    return std::nullopt;
  }
}

nif::AlphaBlendMode blend_mode(const std::optional<std::uint32_t>& value, nif::AlphaBlendMode fallback,
                               std::vector<std::string>& warnings) {
  if (!value) return fallback;
  if (*value > k_last_blend_mode) {
    warnings.push_back("material blend factor " + std::to_string(*value) + " is not a NiAlphaProperty blend mode");
    return fallback;
  }
  return static_cast<nif::AlphaBlendMode>(*value);
}

bool header_blends(const MaterialFile& file) {
  return get_field<std::uint8_t>(file, "AlphaBlendMode0").value_or(0) != 0;
}

bool header_tests(const MaterialFile& file) { return get_field<bool>(file, "AlphaTest").value_or(false); }

void use_header_alpha(ResolvedMaterial& out, const MaterialFile& file) {
  const bool blend = header_blends(file);
  const bool test = header_tests(file);
  out.alpha_mode = blend ? AlphaMode::blend : test ? AlphaMode::test : AlphaMode::opaque;
  if (blend) {
    out.blend_src = blend_mode(get_field<std::uint32_t>(file, "AlphaBlendMode1"), out.blend_src, out.warnings);
    out.blend_dst = blend_mode(get_field<std::uint32_t>(file, "AlphaBlendMode2"), out.blend_dst, out.warnings);
  }
  if (test) out.alpha_threshold = get_field<std::uint8_t>(file, "AlphaTestRef").value_or(out.alpha_threshold);
}

void use_header_uv(ResolvedMaterial& out, const MaterialFile& file) {
  out.uv_offset = {get_field<float>(file, "UOffset").value_or(out.uv_offset[0]),
                   get_field<float>(file, "VOffset").value_or(out.uv_offset[1])};
  out.uv_scale = {get_field<float>(file, "UScale").value_or(out.uv_scale[0]),
                  get_field<float>(file, "VScale").value_or(out.uv_scale[1])};
}

void merge_textures(Merge& m, const MaterialFile& file, std::span<const SlotField> table, bool authoritative) {
  for (const auto& entry : table) {
    if (m.effect && (entry.slot == k_slot_glow || entry.slot == k_slot_smooth_spec)) continue;
    if (const auto value = get_field<std::string>(file, entry.field)) {
      place(m, entry.slot, texture_path(*value, m.out.warnings), authoritative);
    }
  }
}

void merge_bgsm(Merge& m, const MaterialFile& file, bool own) {
  auto& out = m.out;
  merge_textures(m, file, k_bgsm_slots, own && !m.effect);
  out.two_sided = out.two_sided || get_field<bool>(file, "TwoSided").value_or(false);
  if (m.effect) {
    use_header_alpha(out, file);
    return;
  }
  const bool emit = get_field<bool>(file, "EmitEnabled").value_or(false);
  if (own) {
    m.emit = emit;
    m.glowmap = get_field<bool>(file, "Glowmap").value_or(false);
  }
  if (emit) {
    m.emit = true;
    if (const auto color = get_field<Color3>(file, "EmittanceColor")) out.emissive = *color;
    out.emissive_mult = get_field<float>(file, "EmittanceMult").value_or(out.emissive_mult);
  }
  const auto alpha = unit_alpha(get_field<float>(file, "Alpha").value_or(1.0F));
  out.alpha = own ? alpha : unit_alpha(out.alpha * alpha);
  use_header_uv(out, file);
  if (own || (out.alpha_mode == AlphaMode::opaque && (header_blends(file) || header_tests(file)))) {
    use_header_alpha(out, file);
  }
}

void merge_bgem(Merge& m, const MaterialFile& file) {
  auto& out = m.out;
  merge_textures(m, file, k_bgem_slots, false);
  out.two_sided = out.two_sided || get_field<bool>(file, "TwoSided").value_or(false);
  use_header_uv(out, file);
  const auto alpha = unit_alpha(get_field<float>(file, "Alpha").value_or(1.0F));
  if (m.effect) {
    if (const auto color = get_field<Color3>(file, "BaseColor")) out.emissive = *color;
    out.emissive_mult = get_field<float>(file, "BaseColorScale").value_or(out.emissive_mult);
    out.alpha = alpha;
    use_header_alpha(out, file);
    return;
  }
  out.alpha = unit_alpha(out.alpha * alpha);
  if (out.alpha_mode == AlphaMode::opaque && (header_blends(file) || header_tests(file))) use_header_alpha(out, file);
}

void merge_material(Merge& m, const MaterialFile& file, bool own) {
  m.out.source = file.format == MaterialFormat::bgsm ? ResolvedMaterial::Source::bgsm : ResolvedMaterial::Source::bgem;
  if (file.format == MaterialFormat::bgsm) {
    merge_bgsm(m, file, own);
  } else {
    merge_bgem(m, file);
  }
}

ResolvedMaterial resolve_named(const nif::NifFile& file, const nif::Shape& shape, const archive::AssetResolver& assets,
                               std::string_view material_name) {
  ResolvedMaterial out;
  out.nif_material_path = material_key(shape.material);
  Merge m{out, assets};
  std::string root_material;
  read_nif_side(file, shape, m, root_material);

  auto key = material_key(material_name);
  bool own = false;
  if (is_material_key(key)) {
    out.has_wet = key.ends_with(k_wet_suffix);
    if (const auto material = load_material(assets, key, out.warnings)) {
      out.material_path = key;
      merge_material(m, *material, true);
      own = true;
    }
  }
  auto root_key = material_key(root_material);
  if (!own && !m.effect && !root_key.empty()) {
    if (root_key.ends_with(k_wet_suffix)) {
      out.has_wet = true;
    } else if (const auto material = load_material(assets, root_key, out.warnings)) {
      out.material_path = root_key;
      merge_material(m, *material, false);
    }
  }
  out.has_glow = !m.hair && m.emit && m.glowmap && !out.textures[k_slot_glow].empty();
  out.alpha = unit_alpha(out.alpha);
  return out;
}

}  // namespace

ResolvedMaterial resolve_material(const nif::NifFile& file, const nif::Shape& shape, const archive::AssetResolver& assets) {
  return resolve_named(file, shape, assets, shape.material);
}

bool apply_material_swap(ResolvedMaterial& rm, const nif::NifFile& file, const nif::Shape& shape,
                         const archive::AssetResolver& assets, const env::MaterialSubstitution& substitution) {
  auto nif_key = rm.nif_material_path;
  if (nif_key.starts_with(k_materials_prefix)) nif_key.erase(0, k_materials_prefix.size());
  const auto original = swap_key(substitution.original_material);
  if (original.empty() || original != nif_key) return false;
  const auto index = substitution.color_remapping_index ? substitution.color_remapping_index : rm.color_remapping_index;
  if (substitution.replacement_material && !material_key(*substitution.replacement_material).empty()) {
    rm = resolve_named(file, shape, assets, *substitution.replacement_material);
  }
  rm.swapped = true;
  rm.color_remapping_index = index;
  return true;
}

bool apply_material_swap(ResolvedMaterial& rm, const nif::NifFile& file, const nif::Shape& shape,
                         const archive::AssetResolver& assets, std::span<const env::MaterialSubstitution> substitutions) {
  return std::ranges::any_of(substitutions, [&](const env::MaterialSubstitution& substitution) {
    return apply_material_swap(rm, file, shape, assets, substitution);
  });
}

}  // namespace fo4::material
