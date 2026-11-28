#pragma once

#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/terrain_texture.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::lod {

enum class ObjectAtlasTextureState { ok, missing, stale, unverified };

struct ObjectAtlasTextureRow {
  std::string world;
  ObjectAtlasMap map{};
  ObjectAtlasTextureState state{ObjectAtlasTextureState::unverified};
  bool current{false};
  std::uintmax_t bytes{0};
};

struct ObjectAtlasTextureReport {
  std::size_t ok{0};
  std::size_t missing{0};
  std::size_t stale{0};
  std::size_t unverified{0};
  std::vector<ObjectAtlasTextureRow> rows;
  std::string failure;
  bool failure_missing{false};

  // Invariant: merging keeps the first failure, so the reported world and map is the first one found on disk.
  void merge(const ObjectAtlasTextureReport &other);
};

std::string_view object_atlas_texture_state_name(ObjectAtlasTextureState state);

// Invariant: verify judges each atlas through this one function, so its path, predicate and hash match the writer.
ObjectAtlasTextureReport verify_object_atlas_textures(
    std::string_view worldspace, std::span<const LodInput> inputs,
    const std::map<std::string, std::uint64_t> &ledger,
    const std::filesystem::path &data_dir, bool check_disk);

struct TerrainAtlasTextureRow {
  std::string world;
  std::int32_t level{0};
  std::int32_t x{0};
  std::int32_t y{0};
  TerrainTextureMap map{TerrainTextureMap::diffuse};
  ObjectAtlasTextureState state{ObjectAtlasTextureState::unverified};
  bool current{false};
  std::uintmax_t bytes{0};
};

struct TerrainAtlasTextureReport {
  std::size_t ok{0};
  std::size_t missing{0};
  std::size_t stale{0};
  std::size_t unverified{0};
  std::size_t missing_recorded{0};
  std::vector<TerrainAtlasTextureRow> rows;
};

// Invariant: a planned terrain tile names two atlases, so verify judges both through the terrain writer's own path.
TerrainAtlasTextureReport verify_terrain_atlas_textures(std::string_view worldspace,
                                                        std::span<const LodInput> inputs,
                                                        std::span<const LodTile> tiles,
                                                        const std::map<std::string, std::uint64_t> &ledger,
                                                        const std::filesystem::path &data_dir, bool check_disk);

} // namespace fo4::lod
