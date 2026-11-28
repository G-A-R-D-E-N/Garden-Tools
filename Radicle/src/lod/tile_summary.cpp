#include "fo4/lod/tile_summary.hpp"

namespace fo4::lod {

LodTileSummary summarize_lod_tile(const nif::NifFile& nif) {
  LodTileSummary summary;
  for (const auto& block : nif.blocks()) {
    if (nif::is_shape_block_type(block.type)) {
      ++summary.shapes;
      if (block.type == "BSSubIndexTriShape") ++summary.subindex_shapes;
      continue;
    }
    if (block.type == "BSShaderTextureSet") ++summary.texture_sets;
  }
  return summary;
}

}  // namespace fo4::lod
