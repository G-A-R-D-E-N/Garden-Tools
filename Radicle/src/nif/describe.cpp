#include "fo4/nif/describe.hpp"

#include "fo4/constants.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shader.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::nif {
namespace {

bool has_block_prefix(const NifFile& nif, std::string_view prefix) {
  return std::any_of(nif.blocks().begin(), nif.blocks().end(),
                     [prefix](const BlockInfo& block) { return block.type.starts_with(prefix); });
}

bool block_is(const NifFile& nif, std::int64_t block, std::string_view type) {
  return block >= 0 && static_cast<std::uint64_t>(block) < nif.blocks().size() &&
         nif.blocks()[static_cast<std::size_t>(block)].type == type;
}

std::optional<std::uint32_t> first_bsx_flags(const NifFile& nif) {
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    if (nif.blocks()[block].type == "BSXFlags") return read_bsx_flags(nif, block);
  }
  return std::nullopt;
}

// Constraint: a malformed effect shader leaves its shape on the basic material, so the rest of the geometry still loads.
void read_effect_for_geometry(const NifFile& nif, const Shape& shape, GeometryShape& out) {
  if (!block_is(nif, shape.shader_ref, "BSEffectShaderProperty")) return;
  try {
    auto effect = read_effect_shader(nif, static_cast<std::uint32_t>(shape.shader_ref));
    std::optional<AlphaProperty> alpha;
    if (block_is(nif, shape.alpha_ref, "NiAlphaProperty")) {
      alpha = read_alpha(nif, static_cast<std::uint32_t>(shape.alpha_ref));
    }
    out.effect = std::move(effect);
    out.alpha = alpha;
  } catch (const parse_error&) {
  }
}

std::vector<TextureSlot> texture_slots(const std::vector<std::string>& textures) {
  std::vector<TextureSlot> slots;
  for (std::size_t slot = 0; slot < textures.size(); ++slot) {
    if (!textures[slot].empty()) slots.push_back({slot, textures[slot]});
  }
  return slots;
}

void transform_points(const Transform& xf, std::vector<float>& values, bool positions) {
  for (std::size_t base = 0; base + 2 < values.size(); base += 3) {
    const float in[3] = {values[base], values[base + 1], values[base + 2]};
    for (std::size_t i = 0; i < 3; ++i) {
      float value = 0.0f;
      for (std::size_t k = 0; k < 3; ++k) value += xf.r[i * 3 + k] * in[k];
      values[base + i] = positions ? xf.s * value + xf.t[i] : (xf.s < 0.0f ? -value : value);
    }
  }
}

// Invariant: a transform mirrors the shape when s^3 * det(r) is negative, and mirrored triangles must swap indices 1 and 2.
bool mirrors_winding(const Transform& xf) {
  const float* r = xf.r;
  const float det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                    r[2] * (r[3] * r[7] - r[4] * r[6]);
  return xf.s * xf.s * xf.s * det < 0.0f;
}

void flip_winding(std::vector<std::uint16_t>& tris) {
  for (std::size_t base = 0; base + 2 < tris.size(); base += 3) std::swap(tris[base + 1], tris[base + 2]);
}

}  // namespace

std::size_t VerifyReport::failed() const noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(checks, [](const VerifyCheck& check) {
    return !check.failure.empty();
  }));
}

bool is_bgsm_path(std::string_view path) {
  if (path.size() < 5) return false;
  std::string tail(path.substr(path.size() - 5));
  std::ranges::transform(tail, tail.begin(),
                         [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return tail == ".bgsm";
}

// Invariant: the names are BSShaderMaterial::Feature (G-A-R-D-E-N/commonlibf4@22b0dbd:include/RE/B/BSShaderMaterial.h), the engine's own ids.
std::string_view lighting_type_name(std::uint32_t shader_type) noexcept {
  switch (shader_type) {
    case 0: return "Default";
    case 1: return "Envmap";
    case 2: return "Glowmap";
    case 3: return "Parallax";
    case 4: return "Face";
    case 5: return "Skin tint";
    case 6: return "Hair tint";
    case 7: return "Parallax occ";
    case 8: return "Landscape";
    case 9: return "LOD landscape";
    case 10: return "Snow";
    case 11: return "Multi-layer parallax";
    case 12: return "Tree anim";
    case 13: return "LOD objects";
    case 14: return "Multi-index snow";
    case 15: return "LOD objects HD";
    case 16: return "Eye";
    case 17: return "Cloud";
    case 18: return "LOD landscape noise";
    case 19: return "LOD landscape blend";
    case 20: return "Dismemberment";
    default: return "Unknown";
  }
}

InspectReport inspect_nif(const NifFile& nif) {
  InspectReport report;
  report.fo4 = nif.bs_version() == k_nif_bs_version;
  report.has_bsx = has_block_prefix(nif, "BSXFlags");
  report.has_collision = has_block_prefix(nif, "bhk");
  report.bsx_flags = first_bsx_flags(nif);
  report.connect_points = read_connect_parents(nif);
  report.connect_children = read_connect_children(nif);
  for (const auto& shape : read_shapes(nif)) {
    InspectShape out{shape.name, shape.verts.size() / 3, shape.tris.size() / 3, shape.skin_ref >= 0,
                     shape.material, texture_slots(shape.textures), !shape.tangents.empty(), {}, {}, {}};
    if (block_is(nif, shape.alpha_ref, "NiAlphaProperty")) {
      out.alpha = read_alpha(nif, static_cast<std::uint32_t>(shape.alpha_ref));
    }
    if (block_is(nif, shape.shader_ref, "BSEffectShaderProperty")) {
      out.effect = read_effect_shader(nif, static_cast<std::uint32_t>(shape.shader_ref));
    }
    if (block_is(nif, shape.block, "BSSubIndexTriShape")) out.segments = read_segments(nif, shape.block);
    report.shapes.push_back(std::move(out));
  }
  return report;
}

GeometryReport geometry_nif(const NifFile& nif) {
  GeometryReport report;
  report.fo4 = nif.bs_version() == k_nif_bs_version;
  for (const auto& shape : read_shapes(nif)) {
    GeometryShape out;
    out.block = shape.block;
    out.name = shape.name;
    out.verts = shape.verts;
    out.tris = shape.tris;
    out.normals = shape.normals;
    out.uvs = shape.uvs;
    out.uvs2 = shape.uvs2;
    out.textures = texture_slots(shape.textures);
    out.skinned = shape.skin_ref >= 0;
    out.material = shape.material;
    out.hidden = shape.hidden;
    out.shader_kind = block_is(nif, shape.shader_ref, "BSEffectShaderProperty")     ? "effect"
                      : block_is(nif, shape.shader_ref, "BSLightingShaderProperty") ? "lighting"
                                                                                    : "none";
    out.shader_ref = shape.shader_ref;
    out.colors = shape.colors;
    const auto mode = billboard_mode(nif, shape.block);
    out.billboard = mode >= 0;
    out.billboard_mode = mode;
    read_effect_for_geometry(nif, shape, out);
    const std::size_t slots = shape.verts.size() / 3 * 4;
    if (!shape.bones.empty() && slots != 0 && shape.bone_indices.size() == slots && shape.weights.size() == slots) {
      out.weights = shape.weights;
      out.bone_indices = shape.bone_indices;
      out.bones = shape.bones;
    }
    if (!out.skinned) {
      const auto world = shape_to_global(nif, shape.block);
      if (out.billboard) {
        out.world = world;
      } else {
        transform_points(world, out.verts, true);
        transform_points(world, out.normals, false);
        if (mirrors_winding(world)) flip_winding(out.tris);
      }
    }
    report.shapes.push_back(std::move(out));
  }
  return report;
}

}  // namespace fo4::nif
