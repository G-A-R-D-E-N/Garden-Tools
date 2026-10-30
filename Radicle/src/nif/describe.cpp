#include "fo4/nif/describe.hpp"

#include "fo4/nif/nif.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::uint32_t k_fo4_version = 130;

bool has_block_prefix(const NifFile& nif, std::string_view prefix) {
  return std::any_of(nif.blocks().begin(), nif.blocks().end(),
                     [prefix](const BlockInfo& block) { return block.type.starts_with(prefix); });
}

std::vector<TextureSlot> texture_slots(const std::vector<std::string>& textures) {
  std::vector<TextureSlot> slots;
  for (std::size_t slot = 0; slot < textures.size(); ++slot) {
    if (!textures[slot].empty()) slots.push_back({slot, textures[slot]});
  }
  return slots;
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
  report.fo4 = nif.bs_version() == k_fo4_version;
  report.has_bsx = has_block_prefix(nif, "BSXFlags");
  report.has_collision = has_block_prefix(nif, "bhk");
  for (const auto& shape : read_shapes(nif)) {
    report.shapes.push_back({shape.name, shape.verts.size() / 3, shape.tris.size() / 3,
                             shape.skin_ref >= 0, shape.material, texture_slots(shape.textures),
                             !shape.tangents.empty()});
  }
  return report;
}

GeometryReport geometry_nif(const NifFile& nif) {
  GeometryReport report;
  report.fo4 = nif.bs_version() == k_fo4_version;
  for (const auto& shape : read_shapes(nif)) {
    GeometryShape out;
    out.name = shape.name;
    out.verts = shape.verts;
    out.tris = shape.tris;
    out.normals = shape.normals;
    out.uvs = shape.uvs;
    out.textures = texture_slots(shape.textures);
    out.skinned = shape.skin_ref >= 0;
    out.material = shape.material;
    const auto mode = billboard_mode(nif, shape.block);
    out.billboard = mode >= 0;
    out.billboard_mode = mode;
    report.shapes.push_back(std::move(out));
  }
  return report;
}

}  // namespace fo4::nif
