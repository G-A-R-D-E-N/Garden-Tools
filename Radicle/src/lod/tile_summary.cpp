#include "fo4/lod/tile_summary.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace fo4::lod {
namespace {

constexpr std::array<std::string_view, 4> k_shape_types{
    "BSTriShape", "BSSubIndexTriShape", "BSMeshLODTriShape", "BSDynamicTriShape"};

bool listed(std::span<const std::string_view> names, std::string_view value) {
  for (const auto name : names) {
    if (name == value) return true;
  }
  return false;
}

}  // namespace

LodTileSummary summarize_lod_tile(const nif::NifFile& nif) {
  LodTileSummary summary;
  for (const auto& block : nif.blocks()) {
    if (listed(std::span<const std::string_view>{k_shape_types}, block.type)) {
      ++summary.shapes;
      if (block.type == "BSSubIndexTriShape") ++summary.subindex_shapes;
      continue;
    }
    if (block.type == "BSShaderTextureSet") ++summary.texture_sets;
  }
  return summary;
}

}  // namespace fo4::lod
