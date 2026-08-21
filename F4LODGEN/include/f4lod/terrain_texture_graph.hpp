#pragma once

#include "f4lod/record_graph.hpp"
#include "f4lod/terrain.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace f4lod {

struct TerrainAlphaSample {
    std::uint16_t position{};
    float opacity{};
};

struct TerrainLandTextureLayer {
    bool alpha{};
    std::uint8_t quadrant{};
    std::int16_t layer{};
    bool has_landscape_texture{};
    FormKey landscape_texture;
    std::vector<TerrainAlphaSample> alpha_samples;
};

struct TerrainTextureCell {
    FormKey cell;
    FormKey land;
    std::string land_source_plugin;
    std::vector<TerrainLandTextureLayer> layers;
};

struct LandscapeTextureDependency {
    FormKey key;
    std::string source_plugin;
    std::string editor_id;
    bool has_texture_set{};
    FormKey texture_set;
};

struct TextureSetDependency {
    FormKey key;
    std::string source_plugin;
    std::string editor_id;
    // FO4 TXST logical order TX00..TX07. Empty entries are preserved.
    std::array<std::string, 8> texture_paths;
};

struct TerrainTextureDiagnostic {
    std::string code;
    std::string message;
    FormKey record;
};

struct TerrainTextureGraph {
    bool valid{true};
    std::vector<TerrainTextureCell> cells;
    std::vector<LandscapeTextureDependency> landscape_textures;
    std::vector<TextureSetDependency> texture_sets;
    std::vector<TerrainTextureDiagnostic> diagnostics;
};

// Builds the record-level terrain texture graph for the LAND records selected
// by TerrainWorld. No DDS decoding happens here:
//
//   LAND BTXT/ATXT(+VTXT) -> winning LTEX -> winning TXST -> TX00..TX07 paths
//
// NULL FormIDs remain explicit null dependencies. Non-null payload FormIDs
// resolve through RecordGraph's source-plugin master tables.
[[nodiscard]] TerrainTextureGraph build_terrain_texture_graph(
    const RecordGraph& graph,
    const TerrainWorld& terrain);

} // namespace f4lod
