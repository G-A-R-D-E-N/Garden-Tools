#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_tile.hpp"
#include "fo4/lod/terrain_texture.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::lod {

enum class ObjectAtlasMap : std::uint8_t { diffuse, normal, specular };

inline constexpr std::size_t k_object_atlas_maps = 3;

std::string_view object_atlas_map_name(ObjectAtlasMap map);

// Invariant: an asset path is cut to start at its Data folder (Textures, Materials), which is added when missing.
std::string lod_data_folder_path(std::string_view name, std::string_view folder);

// Invariant: the BTO texture set and the atlas writer both call this, so the names can never drift apart.
std::string object_atlas_texture_path(std::string_view worldspace, ObjectAtlasMap map);

// Invariant: an atlas ledger key has three parts, so it can never parse as a five-part tile key.
std::string object_atlas_ledger_key(std::string_view worldspace, ObjectAtlasMap map);

// Invariant: the local file is the tile's Data-less path, so both platforms agree on one layout.
std::filesystem::path object_atlas_local_path(const std::filesystem::path& data_dir, std::string_view worldspace,
                                              ObjectAtlasMap map);

// Invariant: the written atlas is a power-of-two square between these sides, and verify accepts exactly that range.
inline constexpr int k_object_atlas_min_side = 64;
inline constexpr int k_object_atlas_max_side = 4096;

struct ObjectAtlas {
  int side{0};
  std::size_t shapes{0};
  // Invariant: one entry is one distinct source texture set at one UV repeat window.
  std::size_t entries{0};
  // Invariant: counts each named source texture that could not be read or decoded, once per path.
  std::size_t missing_textures{0};
  // Invariant: counts shapes whose UVs tile further than an entry can repeat, so their UVs were clamped.
  std::size_t clamped_shapes{0};
  // Invariant: digests the layout and every texel, so a changed source mesh, material or texture changes it.
  std::uint64_t content_hash{0};
  std::array<TerrainImage, k_object_atlas_maps> maps;
  struct AtlasCell {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
  };
  std::vector<AtlasCell> cells;
};

// Invariant: a baked digest that differs from the one under this three-part key rewrites every tile and map together.
std::string object_atlas_content_ledger_key(std::string_view worldspace);

// Invariant: only a shape's texture paths, material, UV repeat and clamp flag set the layout, so a plan keeps no geometry.
class ObjectAtlasPlan {
 public:
  ObjectAtlasPlan();
  ~ObjectAtlasPlan();
  ObjectAtlasPlan(const ObjectAtlasPlan&) = delete;
  ObjectAtlasPlan& operator=(const ObjectAtlasPlan&) = delete;

  // Invariant: every tile of the world is added before bake, because the layout covers all of them.
  void add(const ObjectTile& tile);

  // Invariant: bake packs exactly the keys added, so the result equals bake_object_atlas over the same tiles.
  ObjectAtlas bake(const archive::AssetResolver& assets);

  // Invariant: false means a shape's key was never added, and then no UV of the tile has been touched.
  bool remap(ObjectTile& tile) const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

// Invariant: each shape's UVs are rewritten into its packed rectangle, in a layout set only by the sorted tiles.
ObjectAtlas bake_object_atlas(const archive::AssetResolver& assets, std::vector<ObjectTile>& tiles);

// Invariant: the atlas reuses the terrain DDS writer, so both lanes share one header and mip chain.
std::vector<std::byte> write_object_atlas_dds(const ObjectAtlas& atlas, ObjectAtlasMap map);

// Invariant: generate and verify both call this, so a texture's ledger value cannot differ between them.
std::uint64_t object_atlas_inputs_hash(std::string_view worldspace, ObjectAtlasMap map,
                                       std::span<const LodInput> inputs);

// Invariant: a written atlas is a square BC3 DDS whose mips fill the file exactly, so verify can judge it.
bool object_atlas_dds_well_formed(std::span<const std::byte> bytes);

}
