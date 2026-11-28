#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/environment/material_swap.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::material {

enum class AlphaMode : std::uint8_t { opaque, test, blend };

// Invariant: the slot indexes are the lighting texture-set layout for every shader kind, so a BGEM base texture lands in slot 0.
inline constexpr std::size_t k_resolved_texture_slots = 10;
inline constexpr std::size_t k_slot_diffuse = 0;
inline constexpr std::size_t k_slot_normal = 1;
inline constexpr std::size_t k_slot_glow = 2;
inline constexpr std::size_t k_slot_greyscale = 3;
inline constexpr std::size_t k_slot_envmap = 4;
inline constexpr std::size_t k_slot_envmask = 5;
inline constexpr std::size_t k_slot_smooth_spec = 7;

// Constraint: UNVERIFIED default threshold when nothing names one.
inline constexpr std::uint8_t k_default_alpha_threshold = 128;

struct ResolvedMaterial {
  enum class Source : std::uint8_t { nif_only, bgsm, bgem };

  Source source{Source::nif_only};
  std::string nif_material_path;
  std::string material_path;
  std::array<std::string, k_resolved_texture_slots> textures;
  AlphaMode alpha_mode{AlphaMode::opaque};
  std::uint8_t alpha_threshold{k_default_alpha_threshold};
  nif::AlphaBlendMode blend_src{nif::AlphaBlendMode::SourceAlpha};
  nif::AlphaBlendMode blend_dst{nif::AlphaBlendMode::InverseSourceAlpha};
  float alpha{1.0F};
  bool two_sided{false};
  bool has_glow{false};
  bool has_wet{false};
  std::array<float, 2> uv_offset{0.0F, 0.0F};
  std::array<float, 2> uv_scale{1.0F, 1.0F};
  Color3 emissive;
  float emissive_mult{0.0F};
  bool swapped{false};
  std::optional<float> color_remapping_index;
  std::vector<std::string> warnings;
};

// Requires shape from read_shapes or read_shape on the same nif; unreadable shaders and materials end up in warnings, not exceptions.
ResolvedMaterial resolve_material(const nif::NifFile& file, const nif::Shape& shape, const archive::AssetResolver& assets);

// Invariant: re-resolves from the NIF with the replacement, returns false when the original does not match rm.nif_material_path.
bool apply_material_swap(ResolvedMaterial& rm, const nif::NifFile& file, const nif::Shape& shape,
                         const archive::AssetResolver& assets, const env::MaterialSubstitution& substitution);
bool apply_material_swap(ResolvedMaterial& rm, const nif::NifFile& file, const nif::Shape& shape,
                         const archive::AssetResolver& assets, std::span<const env::MaterialSubstitution> substitutions);

}  // namespace fo4::material
