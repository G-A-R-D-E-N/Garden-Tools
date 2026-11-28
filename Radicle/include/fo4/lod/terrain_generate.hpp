#pragma once

#include "fo4/lod/generate_support.hpp"

namespace fo4::lod {

// Invariant: tiles are independent, so a world builds on every core, and rows and ledger entries merge in plan order.
void generate_terrain_stage(const LodGenerateWorld& context, const WorldTerrain& terrain, LodLedger& merged,
                            LodGenerateTotals& totals);

}  // namespace fo4::lod
