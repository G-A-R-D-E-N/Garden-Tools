#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_candidates.hpp"
#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::lod {

// Invariant: object vertices carry position, UV, normal and tangent, stride 20 bytes.
struct ObjectTileVertex {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
  float u{0.0f};
  float v{0.0f};
  float nx{0.0f};
  float ny{0.0f};
  float nz{0.0f};
  float tx{0.0f};
  float ty{0.0f};
  float tz{0.0f};
  float bx{0.0f};
  float by{0.0f};
  float bz{0.0f};
  bool frame{false};
};

// Invariant: start_index is an index-buffer offset and primitives counts triangles, matching vanilla segments.
struct ObjectTileSegment {
  std::int32_t cell_x{0};
  std::int32_t cell_y{0};
  std::uint32_t start_index{0};
  std::uint32_t primitives{0};
};

struct ObjectTileShape {
  std::string name;
  // Invariant: the material is the source BSLightingShaderProperty name, never a texture path.
  std::string material;
  // Invariant: the source diffuse, normal and specular textures the atlas packs for this group; empty when unnamed.
  std::array<std::string, 3> textures;
  // Invariant: member UVs are shifted to start at zero and span this many whole repeats, so tilings never mix.
  std::array<std::int32_t, 2> uv_repeat{1, 1};
  // Invariant: set when a member mesh tiled further than an atlas entry may repeat, so its UVs were clamped.
  bool uv_clamped{false};
  // Invariant: the enable parent this shape toggles with, 0 for an always-shown shape.
  std::uint32_t toggle_ref{0};
  std::vector<ObjectTileVertex> vertices;
  std::vector<std::uint16_t> triangles;
  // Invariant: only render_shapes fills this (level 4: one per cell through the highest occupied one); a built tile has none.
  std::vector<ObjectTileSegment> segments;
  // Invariant: level-4 only, one relative cell slot per triangle (column * level + row, -1 outside the tile).
  std::vector<std::int32_t> triangle_cells;
  // Invariant: where each appended object starts in the vertex and index lists, so a capped shape splits between objects.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> block_starts;
  // Invariant: one serial per block_starts entry, counted over the whole tile, so render_shapes can interleave groups by object.
  std::vector<std::uint32_t> block_serials;
  // Invariant: centre xyz and radius in stored units, extents are half sizes in stored units.
  std::array<float, 4> bound{};
  std::array<float, 3> extents{};
};

struct ObjectTile {
  std::int32_t level{0};
  std::int32_t origin_cell_x{0};
  std::int32_t origin_cell_y{0};
  std::vector<ObjectTileShape> shapes;
  std::size_t objects{0};
  std::size_t meshes{0};
  std::size_t missing_meshes{0};
  std::size_t dropped_vertices{0};
  std::size_t dropped_triangles{0};
  // Invariant: triangles wholly below the terrain are culled, not lost, so they never count as dropped.
  std::size_t culled_triangles{0};
  std::uint32_t block_serial{0};
};

ObjectTile build_object_tile(const env::Environment& environment, const archive::AssetResolver& assets,
                             const FormKey& worldspace, const LodSettings& settings, std::int32_t level,
                             std::int32_t tile_x, std::int32_t tile_y);

// Invariant: the caller collects the candidates once and reuses them for every tile of a level.
ObjectTile build_object_tile(const archive::AssetResolver& assets, const LodSettings& settings,
                             std::int32_t level, std::int32_t tile_x, std::int32_t tile_y,
                             const LodCandidateCollection& candidates);

// Invariant: the object tiles a candidate set occupies at one level, sorted by y then x.
std::vector<LodTile> object_tile_origins(const LodSettings& settings, std::int32_t level,
                                         const LodCandidateCollection& candidates);

// Invariant: one origin rule buckets candidates, so an input is never filed under a tile the builder does not write.
std::pair<std::int32_t, std::int32_t> object_tile_origin_of(const LodSettings& settings, std::int32_t level,
                                                            const LodObjectCandidate& candidate);

// Invariant: a mesh's loose inputs are the same at every level, so one cache parses each mesh once per call.
using ObjectLooseMeshCache = std::map<std::string, std::vector<LodInput>>;

// Invariant: an object tile reads its candidates' loose meshes, materials and textures, so a mod reopens those tiles.
void collect_object_loose_inputs(const archive::AssetResolver& assets, const LodSettings& settings,
                                 std::string_view world, std::int32_t level,
                                 const LodCandidateCollection& candidates,
                                 std::map<std::string, std::vector<LodInput>>& out, ObjectLooseMeshCache& meshes);

// Invariant: one pass buckets every candidate by tile and parses each mesh once, not once per instance.
std::vector<ObjectTile> build_object_tiles(const archive::AssetResolver& assets, const LodSettings& settings,
                                           std::int32_t level, const std::vector<LodTile>& tiles,
                                           const LodCandidateCollection& candidates);

// Invariant: this per-mesh seam is what the tile builder calls and what tests drive directly.
void append_lod_object(ObjectTile& tile, const fo4::nif::NifFile& nif, const fo4::nif::Shape& shape,
                       const LodObjectCandidate& candidate, std::int32_t level, std::int32_t tile_x,
                       std::int32_t tile_y, const LandGrids* terrain = nullptr);

// Invariant: a triangle is culled when all three vertices sit more than this far below the LAND under them.
inline constexpr double k_object_cull_depth = 64.0;

// Invariant: an atlas entry repeats its source at most this often per axis; a mesh that tiles further is clamped.
inline constexpr std::int32_t k_object_uv_max_repeat = 4;

// Invariant: a tile that built no shape is recorded under this three-part key, which never parses as a tile key.
std::string object_tile_empty_ledger_key(const LodTile& tile);

std::vector<std::byte> write_object_bto(const ObjectTile& tile, std::string_view worldspace);

}  // namespace fo4::lod
