#include "fo4/lod/terrain_generate.hpp"

#include "fo4/lod/parallel.hpp"
#include "fo4/lod/terrain_texture.hpp"
#include "fo4/lod/terrain_tile.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace fo4::lod {

namespace {

struct TerrainTileOutcome {
  std::string tile_row;
  std::string texture_rows;
  std::size_t bytes{0};
  std::size_t failed{0};
  std::size_t textures_written{0};
  std::size_t textures_skipped{0};
  std::size_t textures_empty{0};
  bool would_write{false};
  bool wrote_tile{false};
  bool ran{false};
  std::array<bool, k_terrain_texture_maps> texture_written{};
  std::vector<std::filesystem::path> atlas_overwrites;
};

constexpr std::array<TerrainTextureMap, 2> k_maps{TerrainTextureMap::diffuse, TerrainTextureMap::normal};

// Invariant: every terrain atlas row names its tile, so a stale DDS is attributable to one tile.
std::string terrain_texture_row(const std::string& world, const LodTile& tile, TerrainTextureMap map,
                                std::string_view state, std::size_t bytes, const std::string& error) {
  std::string row{"{\"world\":" + json_quote(world)};
  row += ",\"kind\":\"texture\",\"map\":" + json_quote(terrain_texture_map_name(map));
  row += ",\"level\":" + std::to_string(tile.level);
  row += ",\"x\":" + std::to_string(tile.x);
  row += ",\"y\":" + std::to_string(tile.y);
  row += ",\"status\":" + json_quote(state);
  row += ",\"bytes\":" + std::to_string(bytes);
  if (!error.empty()) row += ",\"error\":" + json_quote(error);
  row += '}';
  return row;
}

void note_terrain_atlases(const LodGenerateWorld& context, const LodLedger& merged, const LodTile& tile,
                          TerrainTileOutcome& outcome) {
  if (context.data_dir.empty()) return;
  const auto& world = context.world;
  std::optional<std::uint64_t> hash;
  for (const auto map : k_maps) {
    const auto target = terrain_texture_local_path(context.data_dir, world.name, tile.level, tile.x, tile.y, map);
    std::error_code status;
    if (!std::filesystem::is_regular_file(target, status)) continue;
    if (!hash) hash = lod_texture_inputs_hash(tile, context.shared);
    const auto size = std::filesystem::file_size(target, status);
    const auto recorded =
        merged.hashes.find(terrain_texture_ledger_key(world.name, tile.level, tile.x, tile.y, map));
    const bool current = !status && size >= k_min_texture_bytes && recorded != merged.hashes.end() &&
                         recorded->second == *hash;
    if (!current) outcome.atlas_overwrites.push_back(target);
  }
}

// Invariant: a .btr names two atlases, so both must exist and be current before the tile counts as done.
void bake_one_tile_textures(const LodGenerateWorld& context, const TerrainCellIndex& terrain_index,
                            const LodLedger& merged, const LodTile& tile, TerrainTileOutcome& outcome) {
  const auto& world = context.world;
  const auto hash = lod_texture_inputs_hash(tile, context.shared);
  const auto target_of = [&](TerrainTextureMap map) {
    return terrain_texture_local_path(context.data_dir, world.name, tile.level, tile.x, tile.y, map);
  };
  const auto record_texture_row = [&](TerrainTextureMap map, std::string_view state, std::size_t bytes,
                                      const std::string& error) {
    if (!outcome.texture_rows.empty()) outcome.texture_rows.push_back(',');
    outcome.texture_rows += terrain_texture_row(world.name, tile, map, state, bytes, error);
  };
  bool needed = false;
  for (const auto map : k_maps) {
    std::error_code status;
    const auto size = std::filesystem::file_size(target_of(map), status);
    const auto key = terrain_texture_ledger_key(world.name, tile.level, tile.x, tile.y, map);
    const auto recorded = merged.hashes.find(key);
    if (status || size < k_min_texture_bytes || recorded == merged.hashes.end() || recorded->second != hash) {
      needed = true;
    }
  }
  if (!needed) {
    for (const auto map : k_maps) {
      ++outcome.textures_skipped;
      record_texture_row(map, "skipped", 0, std::string{});
    }
    return;
  }
  TerrainTextures baked;
  try {
    baked = bake_terrain_textures(terrain_index, context.environment, world.settings, tile.level, tile.x, tile.y);
  } catch (const std::exception& failure) {
    ++outcome.failed;
    for (const auto map : k_maps) record_texture_row(map, "failed", 0, failure.what());
    return;
  }
  for (const auto map : k_maps) {
    const auto& image = map == TerrainTextureMap::diffuse ? baked.diffuse : baked.normal;
    const auto slot = map == TerrainTextureMap::diffuse ? std::size_t{0} : std::size_t{1};
    std::string state;
    std::size_t bytes = 0;
    std::string error;
    if (!terrain_tile_needs_texture(image)) {
      state = "empty";
      ++outcome.textures_empty;
    } else {
      const auto key = terrain_texture_ledger_key(world.name, tile.level, tile.x, tile.y, map);
      const auto target = target_of(map);
      std::error_code status;
      const auto size = std::filesystem::file_size(target, status);
      const auto recorded = merged.hashes.find(key);
      if (!status && size >= k_min_texture_bytes && recorded != merged.hashes.end() && recorded->second == hash) {
        state = "skipped";
        ++outcome.textures_skipped;
      } else {
        try {
          const auto payload = write_terrain_dds(image);
          if (write_payload_at(target, payload, bytes, error)) {
            outcome.texture_written[slot] = true;
            state = "written";
            ++outcome.textures_written;
          } else {
            state = "failed";
            ++outcome.failed;
          }
        } catch (const std::exception& failure) {
          error = failure.what();
          state = "failed";
          ++outcome.failed;
        }
      }
    }
    record_texture_row(map, state, bytes, error);
  }
}

}  // namespace

void generate_terrain_stage(const LodGenerateWorld& context, const WorldTerrain& terrain, LodLedger& merged,
                            LodGenerateTotals& totals) {
  const auto& world = context.world;
  const bool apply = context.apply;
  // Invariant: a world's cell grid is decoded once for the whole world, never once per tile.
  const auto terrain_index = build_terrain_cell_index(context.environment, world.key);
  // Invariant: a scoped run plans only its own levels, so a tile outside the range is never built or recorded.
  std::vector<LodTile> planned;
  for (auto tile : terrain_tiles(world, terrain)) {
    if (lod_level_in_scope(context, tile.level)) planned.emplace_back(std::move(tile));
  }
  if (context.progress != nullptr) context.progress->total += planned.size();
  const std::vector<std::vector<LodInput>> inputs(
      planned.size(), std::vector<LodInput>(context.shared.begin(), context.shared.end()));
  const auto terrain_plans = plan_lod_rebakes(planned, inputs, merged);
  std::vector<TerrainTileOutcome> terrain_outcomes(terrain_plans.size());
  // Invariant: a tile the ledger calls current is still rebuilt when its file is gone or truncated.
  const auto wants_write = [&](const LodTilePlan& plan) {
    return plan.rebuild || (!context.data_dir.empty() && !tile_on_disk(context.data_dir, plan.tile));
  };
  const auto run_tile = [&](std::size_t index) {
    const auto& plan = terrain_plans[index];
    auto& outcome = terrain_outcomes[index];
    // Invariant: a cancelled run writes no further tile, so only the tiles already finished reach the ledger.
    if (lod_run_cancelled(context)) return;
    outcome.ran = true;
    try {
      if (wants_write(plan)) {
        std::string state;
        std::string error;
        if (!apply) {
          state = "would-write";
          outcome.would_write = true;
        } else {
          // Invariant: the tile comes from the world's cached terrain, never a fresh decode per tile.
          const auto built = build_terrain_tile(terrain, world.settings, plan.tile.level, plan.tile.x, plan.tile.y);
          const auto payload = write_terrain_btr(built, world.name);
          if (write_payload_at(lod_tile_local_path(context.data_dir, plan.tile), payload, outcome.bytes, error)) {
            outcome.wrote_tile = true;
            state = "written";
          } else {
            state = "failed";
            ++outcome.failed;
          }
        }
        outcome.tile_row = tile_row(plan.tile, state, outcome.bytes, error);
      }
      // Invariant: a current tile still bakes its atlases, because a deleted DDS must be rebuilt.
      if (apply) {
        bake_one_tile_textures(context, terrain_index, merged, plan.tile, outcome);
      } else {
        note_terrain_atlases(context, merged, plan.tile, outcome);
      }
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception& failure) {
      // Invariant: a tile that throws reports exactly one failed row, so a half-done tile never leaks its counters.
      outcome = TerrainTileOutcome{};
      outcome.ran = true;
      outcome.failed = 1;
      outcome.tile_row = tile_row(plan.tile, "failed", 0, failure.what());
    }
  };
  // Invariant: a batch bounds how long one report waits, so progress and cancel answer during a long bake.
  const auto workers = std::max<std::size_t>(1, static_cast<std::size_t>(std::thread::hardware_concurrency()));
  const auto batch = workers * 4;
  for (std::size_t start = 0; start < terrain_plans.size(); start += batch) {
    if (lod_run_cancelled(context)) {
      totals.cancelled = true;
      break;
    }
    const auto count = std::min(batch, terrain_plans.size() - start);
    parallel_for(count, [&](std::size_t offset) { run_tile(start + offset); });
    // Invariant: rows and ledger entries merge in plan order, so a parallel bake reports exactly like a serial one.
    for (std::size_t index = start; index < start + count; ++index) {
      const auto& plan = terrain_plans[index];
      const auto& outcome = terrain_outcomes[index];
      // Invariant: a tile that did not run can only be a cancelled one, so the run must record that cancel.
      if (!outcome.ran) {
        totals.cancelled = true;
        break;
      }
      // Invariant: a cancel seen during the merge stops the rest, so no work started after it is counted.
      if (lod_run_cancelled(context)) {
        totals.cancelled = true;
        break;
      }
      if (outcome.would_write) {
        ++totals.would_write;
        if (tile_file_exists(context.data_dir, plan.tile)) {
          add_dry_run_path(totals.would_overwrite, context.data_dir, plan.tile);
        }
      }
      for (const auto& atlas : outcome.atlas_overwrites) {
        add_dry_run_file(totals.would_overwrite_atlases, context.data_dir, atlas);
      }
      if (outcome.wrote_tile) {
        commit_lod_rebake(merged, plan);
        ++totals.written;
      }
      totals.failed += outcome.failed;
      totals.terrain_textures_written += outcome.textures_written;
      totals.terrain_textures_skipped += outcome.textures_skipped;
      totals.terrain_textures_empty += outcome.textures_empty;
      if (!outcome.tile_row.empty()) {
        if (!totals.tiles.empty()) totals.tiles.push_back(',');
        totals.tiles += outcome.tile_row;
      }
      if (!outcome.texture_rows.empty()) {
        if (!totals.textures.empty()) totals.textures.push_back(',');
        totals.textures += outcome.texture_rows;
      }
      if (outcome.texture_written[0] || outcome.texture_written[1]) {
        const auto hash = lod_texture_inputs_hash(plan.tile, context.shared);
        for (const auto map : k_maps) {
          const auto slot = map == TerrainTextureMap::diffuse ? std::size_t{0} : std::size_t{1};
          if (!outcome.texture_written[slot]) continue;
          merged.hashes[terrain_texture_ledger_key(world.name, plan.tile.level, plan.tile.x, plan.tile.y, map)] = hash;
        }
      }
      // Invariant: a reported tile must be a fully merged tile, so a cancel the callback sets never splits one.
      lod_report_progress(context, plan.tile);
    }
    // Invariant: a cancel seen between batches stops the next batch, so it only counts while a batch remains.
    if (start + count < terrain_plans.size() && lod_run_cancelled(context)) {
      totals.cancelled = true;
      break;
    }
  }
  // Invariant: a world's dropped terrain is reported even when it produced no tile, so a hole never reads as clean.
  std::string loss;
  const auto count_loss = [&loss](std::string_view name, std::size_t value) {
    if (value == 0) return;
    if (!loss.empty()) loss.push_back(',');
    loss += json_quote(name) + ':' + std::to_string(value);
  };
  count_loss("badCells", terrain.land.bad_cells);
  count_loss("badLand", terrain.land.bad_land);
  count_loss("missingVhgt", terrain.land.missing_vhgt);
  count_loss("duplicateCells", terrain.land.duplicate_cells);
  count_loss("waterErrors", terrain.water_errors);
  if (!loss.empty()) {
    if (!totals.terrain_loss.empty()) totals.terrain_loss.push_back(',');
    totals.terrain_loss += json_quote(world.name) + ":{" + loss + '}';
  }
}

}  // namespace fo4::lod
