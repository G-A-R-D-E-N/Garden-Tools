#pragma once

#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/lod.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::lod {

struct LodWorld {
  FormKey key;
  std::string name;
  LodSettings settings;
};

// Invariant: worlds whose settings file is missing or unparseable are skipped; failed receives their count.
std::vector<LodWorld> list_lod_worlds(const env::Environment& environment,
                                      const archive::AssetResolver& assets,
                                      std::size_t* failed = nullptr);

// Invariant: a landed mod world with no lodsettings is listed so a run can derive and write one; settings stay default.
std::vector<LodWorld> list_lod_worlds_without_settings(const env::Environment& environment,
                                                       const archive::AssetResolver& assets);

struct LodInput {
  // Invariant: the name is a Data-relative name, never an absolute path, so moving a data folder keeps tiles current.
  std::string path;
  std::uint64_t size{0};
  // Invariant: the digest covers the file content, so a same-size plugin edit still reopens a tile; 0 when unhashed.
  std::uint64_t digest{0};
  // Invariant: a loose or archive file is judged by write time, because hashing every mod file per tile is too slow.
  std::uint64_t write_time{0};
};

// Constraint: only an existing loose file is an input, so a file appearing or vanishing reopens the tiles that read it.
std::optional<LodInput> loose_file_input(const archive::AssetResolver& assets, std::string_view asset_path);

struct LodTilePlan {
  LodTile tile;
  std::uint64_t inputs{0};
  bool rebuild{false};
};

// Invariant: the ledger key is "<world>|<kind>|<level>|<x>|<y>" and its value a 64-bit input hash.
struct LodLedger {
  std::map<std::string, std::uint64_t> hashes;
};

// Invariant: worldspace is lower-cased in the key so two casings cannot create separate entries.
std::string lod_ledger_key(const LodTile& tile);
LodLedger parse_lod_ledger(std::string_view text);
std::string format_lod_ledger(const LodLedger& ledger);

// Invariant: the hash covers the tile identity plus every input name, size, digest and write time, length-prefixed.
std::uint64_t lod_texture_inputs_hash(const LodTile& tile, std::span<const LodInput> inputs);

// Invariant: the same hash, with the terrain format version mixed in for terrain tiles only.
std::uint64_t lod_tile_inputs_hash(const LodTile& tile, std::span<const LodInput> inputs);

// Invariant: planning must not mutate the ledger; a tile only becomes current through commit_lod_rebake.
std::vector<LodTilePlan> plan_lod_rebakes(const std::vector<LodTile>& tiles,
                                          std::span<const std::vector<LodInput>> inputs, const LodLedger& ledger);

// Invariant: call only after the tile was written successfully; records its input hash as current.
void commit_lod_rebake(LodLedger& ledger, const LodTilePlan& plan);

struct LodProgress {
  std::size_t tiles_total{0};
  std::size_t tiles_done{0};
  std::size_t tiles_rebuilt{0};
  std::size_t tiles_skipped{0};
};

// Invariant: progress is complete only when every tile was either rebuilt or skipped.
bool lod_progress_complete(const LodProgress& progress);

// Invariant: preview bytes are exactly the NIF the terrain writer produces for that tile.
std::vector<std::byte> preview_terrain_tile(const env::Environment& environment, const LodWorld& world,
                                            std::int32_t level, std::int32_t tile_x, std::int32_t tile_y);

// Invariant: the bytes are exactly the BTO lod_generate writes for that tile; none when the cell has no object tile.
std::optional<std::vector<std::byte>> preview_object_tile(const env::Environment& environment,
                                                          const archive::AssetResolver& assets,
                                                          const LodWorld& world, std::int32_t level,
                                                          std::int32_t tile_x, std::int32_t tile_y);

// Invariant: the local path mirrors format_lod_tile_path component by component, so both platforms agree.
std::filesystem::path lod_tile_local_path(const std::filesystem::path& root, const LodTile& tile);

// Invariant: a ledger key parses back to the tile it was built from, or nothing when it is malformed.
std::optional<LodTile> parse_lod_ledger_key(std::string_view key);

}  // namespace fo4::lod
