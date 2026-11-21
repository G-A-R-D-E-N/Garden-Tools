#pragma once

#include "fo4/lod/generate_support.hpp"

#include <memory>

namespace fo4::lod {

// Invariant: the world's atlas is packed before any tile is written, because packing rewrites every tile's UVs.
// Constraint: the terrain stage's WorldTerrain supplies the cull's LAND, so LAND decodes once per world.
void generate_object_stage(const LodGenerateWorld& context, const std::shared_ptr<const WorldTerrain>& terrain,
                           LodLedger& merged, LodGenerateTotals& totals);

}  // namespace fo4::lod
