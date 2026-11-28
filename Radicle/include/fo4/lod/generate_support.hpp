#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_candidates.hpp"
#include "fo4/lod/object_tile.hpp"
#include "fo4/lod/terrain_heightfield.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::lod {

// Invariant: a written tile is at least its fixed NIF header, so anything smaller is a partial write.
inline constexpr std::uintmax_t k_min_tile_bytes = 40;

// Invariant: a written DDS is at least its 128-byte header, so anything smaller is a partial write.
inline constexpr std::uintmax_t k_min_texture_bytes = 148;

std::string json_quote(std::string_view text);

// Invariant: a summary lists only the counts that happened, so a zero is omitted rather than serialised.
void append_count(std::string& json, std::string_view name, std::size_t value);

// Constraint: the target is replaced by a rename, so a crash can never leave a truncated file behind.
bool write_payload_at(const std::filesystem::path& target, std::span<const std::byte> payload, std::size_t& bytes,
                      std::string& error);

bool tile_on_disk(const std::filesystem::path& data_dir, const LodTile& tile);

// Constraint: a dry-run path list is capped, so a world-sized report stays small and truncated says it was cut.
inline constexpr std::size_t k_lod_dry_run_path_cap = 200;

struct LodPathList {
  std::vector<std::string> paths;
  bool truncated{false};
};

// Invariant: this is any regular file at the tile's path, so a truncated tile still counts as one a run would replace.
bool tile_file_exists(const std::filesystem::path& data_dir, const LodTile& tile);

// Invariant: a listed path is relative to the data folder with forward slashes, whatever the host separator.
void add_dry_run_path(LodPathList& list, const std::filesystem::path& data_dir, const LodTile& tile);

// Invariant: an empty list is omitted, so an apply run and a dry run that touches nothing carry no key.
void append_path_list(std::string& json, std::string_view name, const LodPathList& list);

// Invariant: terrain and object tiles report through one row writer, so every row has the same fields.
std::string tile_row(const LodTile& tile, std::string_view state, std::size_t bytes, const std::string& error);

bool object_level_supported(std::int32_t level);

// Invariant: object tiles come from the candidate origins, and every row must carry the world name.
std::vector<LodTile> object_tiles(const LodWorld& world, std::int32_t level,
                                  const LodCandidateCollection& candidates);

// Invariant: object LOD exists only at the shipped levels, so iteration must skip the rest.
template <typename Fn>
void for_each_object_level(const LodSettings& settings, const Fn& body) {
  for (std::int32_t level = settings.level_min; level <= settings.level_max;) {
    if (level <= 0) break;
    if (object_level_supported(level)) body(level);
    if (level > settings.level_max / 2) break;
    level *= 2;
  }
}

// Invariant: planning enumerates the world's block grid, so a missing tile is generated, not skipped.
std::vector<LodTile> terrain_tiles(const LodWorld& world, const WorldTerrain& terrain);

struct LodGenerateProgress {
  // Invariant: progress is called from the merging thread only, so a host callback never runs concurrently.
  void (*report)(void* user, const char* world, const char* kind, std::int32_t level, std::int32_t x, std::int32_t y,
                 std::size_t done, std::size_t total){nullptr};
  void* user{nullptr};
  // Constraint: the cancel flag is written by another thread, so it is read atomically, never through a plain load.
  std::int32_t* cancel{nullptr};
  std::size_t total{0};
  std::size_t done{0};
};

struct LodGenerateWorld {
  const env::Environment& environment;
  const archive::AssetResolver& assets;
  const std::filesystem::path& data_dir;
  const LodWorld& world;
  std::span<const LodInput> shared;
  bool apply{false};
  // Invariant: a tile adds the loose files it reads to the shared list, keyed by the ledger key it commits under.
  const std::map<std::string, std::vector<LodInput>>* extra_inputs{nullptr};
  // Invariant: a scoped run bakes only the levels it was asked for, so the defaults keep the shipped range.
  std::int32_t level_min{k_lod_level_min};
  std::int32_t level_max{k_lod_level_max};
  // Invariant: progress and cancellation are optional, so an unscoped run carries neither.
  LodGenerateProgress* progress{nullptr};
};

// Invariant: a tile outside the scoped level range is never planned, so a scoped run costs only its own levels.
bool lod_level_in_scope(const LodGenerateWorld& context, std::int32_t level) noexcept;

// Invariant: partial means a level the object stage would bake is outside the scope, so a wider grid alone is not.
bool lod_level_scope_is_partial(const LodSettings& settings, std::int32_t level_min, std::int32_t level_max) noexcept;

// Invariant: a cancelled run writes no further tile, so the ledger only gains tiles that finished.
bool lod_run_cancelled(const LodGenerateWorld& context) noexcept;

// Invariant: one progress report per finished tile, so a host counts tiles rather than plans.
void lod_report_progress(const LodGenerateWorld& context, const LodTile& tile);

struct LodGenerateTotals {
  std::size_t written{0};
  std::size_t would_write{0};
  LodPathList would_overwrite;
  LodPathList would_delete;
  std::size_t unsupported{0};
  std::array<std::size_t, k_lod_candidate_skip_count> object_drops{};
  std::size_t empty{0};
  std::size_t failed{0};
  std::size_t textures_written{0};
  std::size_t textures_skipped{0};
  std::size_t terrain_textures_written{0};
  std::size_t terrain_textures_skipped{0};
  std::size_t terrain_textures_empty{0};
  std::size_t object_missing_meshes{0};
  std::size_t object_dropped_triangles{0};
  std::size_t atlas_entries{0};
  std::size_t atlas_missing_textures{0};
  std::size_t atlas_clamped_shapes{0};
  std::string tiles;
  std::string textures;
  // Invariant: one "world":{counts} row per world that lost terrain, so a hole carries the world it sits in.
  std::string terrain_loss;
  // Invariant: cancelled means a stage saw the flag, so a flag set after the work finished reads as a clean run.
  bool cancelled{false};
  // Invariant: a partial level scope bakes no object tile, so the report names the reason rather than a zero count.
  std::string objects_skipped;
};

}  // namespace fo4::lod
