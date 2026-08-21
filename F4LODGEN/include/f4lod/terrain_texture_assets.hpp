#pragma once

#include "f4lod/assets.hpp"
#include "f4lod/dds.hpp"
#include "f4lod/terrain_texture_graph.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainResolvedTexture {
    std::string game_path;
    AssetSource source;
    DdsInspection dds;
};

struct TerrainTextureAssetDiagnostic {
    std::string code;
    std::string message;
    std::string game_path;
};

struct TerrainTextureAssetReport {
    bool valid{true};
    std::size_t referenced_slots{};
    std::size_t unique_paths{};
    std::vector<TerrainResolvedTexture> textures;
    std::vector<TerrainTextureAssetDiagnostic> diagnostics;
};

// Resolves every non-empty TXST path through the same loose/BA2 precedence used
// by the rest of the compiler and structurally validates the resulting DDS.
// Paths are deduplicated case-insensitively after slash normalization.
[[nodiscard]] TerrainTextureAssetReport resolve_terrain_texture_assets(
    const TerrainTextureGraph& graph,
    const AssetResolver& assets);

} // namespace f4lod
