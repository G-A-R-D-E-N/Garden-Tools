#include "fo4/lod/object_generate.hpp"

#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/object_candidates.hpp"
#include "fo4/lod/object_tile.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::lod {

namespace {

struct ObjectLevel {
  std::int32_t level{0};
  LodCandidateCollection candidates;
  std::vector<LodTile> origins;
};

// Invariant: the write loop reads only these of a built tile, so pass one frees the geometry once the atlas plan has it.
struct ObjectTileFacts {
  bool shapeless{false};
  std::size_t missing_meshes{0};
};

// Constraint: a dry run applies the writer's own rule, so only a tile that builds no shape and lost no mesh is deleted.
void note_existing_object_tile(const LodGenerateWorld& context, const ObjectLevel& entry, const LodTile& tile,
                               LodGenerateTotals& totals) {
  bool shapeless = false;
  bool lost_meshes = false;
  try {
    const auto built = build_object_tile(context.assets, context.world.settings, tile.level, tile.x, tile.y,
                                         entry.candidates);
    shapeless = built.shapes.empty();
    lost_meshes = built.missing_meshes != 0;
  } catch (const std::bad_alloc&) {
    throw;
  } catch (const std::exception&) {
    // Constraint: a tile that cannot be built is listed as overwritten, the conservative side for the user.
  }
  if (shapeless && lost_meshes) return;
  add_dry_run_path(shapeless ? totals.would_delete : totals.would_overwrite, context.data_dir, tile);
}

}  // namespace

void generate_object_stage(const LodGenerateWorld& context, const std::shared_ptr<const WorldTerrain>& terrain,
                           LodLedger& merged, LodGenerateTotals& totals) {
  const auto& world = context.world;
  const auto& shared = context.shared;
  const bool apply = context.apply;
  const auto& data_dir = context.data_dir;
  // Constraint: a scope narrower than the world's baked range must not repack the atlas, so it bakes no object tile.
  if (lod_level_scope_is_partial(world.settings, context.level_min, context.level_max)) {
    totals.objects_skipped = "level scope";
    return;
  }
  std::vector<ObjectLevel> object_levels;
  std::vector<LodTilePlan> object_plans;
  const std::shared_ptr<const LandGrids> land(terrain, &terrain->land);
  for_each_object_level(world.settings, [&](std::int32_t level) {
    // Invariant: a scoped run collects only its own levels, so a level outside the range is never planned.
    if (!lod_level_in_scope(context, level)) return;
    ObjectLevel entry;
    entry.level = level;
    entry.candidates = collect_object_lod_candidates(context.environment, world.key, level, land);
    totals.unsupported +=
        entry.candidates.counts.skipped[static_cast<std::size_t>(LodCandidateSkip::unsupported_base)] +
        entry.candidates.counts.skipped[static_cast<std::size_t>(LodCandidateSkip::malformed)];
    for (std::size_t reason = 0; reason < totals.object_drops.size(); ++reason) {
      totals.object_drops.at(reason) += entry.candidates.counts.skipped.at(reason);
    }
    entry.origins = object_tiles(world, level, entry.candidates);
    // Invariant: a tile's inputs are the shared files plus the loose files that tile itself reads.
    std::vector<std::vector<LodInput>> object_inputs;
    object_inputs.reserve(entry.origins.size());
    for (const auto& origin : entry.origins) {
      std::vector<LodInput> list(shared.begin(), shared.end());
      if (context.extra_inputs != nullptr) {
        const auto found = context.extra_inputs->find(lod_ledger_key(origin));
        if (found != context.extra_inputs->end()) list.insert(list.end(), found->second.begin(), found->second.end());
      }
      object_inputs.push_back(std::move(list));
    }
    for (auto& plan : plan_lod_rebakes(entry.origins, object_inputs, merged)) {
      object_plans.push_back(std::move(plan));
    }
    object_levels.push_back(std::move(entry));
  });
  if (context.progress != nullptr) context.progress->total += object_plans.size();
  // Invariant: a run cancelled before its first object tile writes none, so no atlas is baked for a partial set.
  if (lod_run_cancelled(context)) {
    totals.cancelled = true;
    return;
  }
  // Invariant: a tile recorded as empty for these inputs is not rebuilt each run.
  const auto known_empty = [&](const LodTilePlan& plan) {
    const auto empty_record = merged.hashes.find(object_tile_empty_ledger_key(plan.tile));
    return empty_record != merged.hashes.end() && empty_record->second == plan.inputs;
  };
  // Invariant: a tile the ledger calls current is still rewritten when its file is gone.
  const auto wants_write = [&](const LodTilePlan& plan) {
    if (known_empty(plan)) return false;
    return plan.rebuild || (!data_dir.empty() && !tile_on_disk(data_dir, plan.tile));
  };
  const auto atlas_current = [&](ObjectAtlasMap map) {
    std::error_code status;
    const auto size = std::filesystem::file_size(object_atlas_local_path(data_dir, world.name, map), status);
    const auto recorded = merged.hashes.find(object_atlas_ledger_key(world.name, map));
    return !status && size >= k_min_texture_bytes && recorded != merged.hashes.end() &&
           recorded->second == object_atlas_inputs_hash(world.name, map, shared);
  };
  // Invariant: a world whose tiles and atlas are all current is not rebuilt.
  bool object_work = false;
  // Constraint: only a world with a tile that holds shapes has an atlas.
  bool atlas_expected = false;
  if (apply) {
    for (const auto& plan : object_plans) {
      object_work = object_work || wants_write(plan);
      atlas_expected = atlas_expected || !known_empty(plan);
    }
    for (std::size_t index = 0; atlas_expected && index < k_object_atlas_maps; ++index) {
      object_work = object_work || !atlas_current(static_cast<ObjectAtlasMap>(index));
    }
  }
  // Invariant: once anything is written every object tile is folded into the atlas plan, because it must cover the whole world.
  std::vector<ObjectTileFacts> facts;
  ObjectAtlasPlan atlas_plan;
  if (object_work) {
    for (const auto& entry : object_levels) {
      if (entry.origins.empty()) continue;
      // Invariant: one level's geometry is alive at a time; it is freed before the next level is built.
      const auto built = build_object_tiles(context.assets, world.settings, entry.level, entry.origins, entry.candidates);
      for (const auto& tile : built) {
        facts.push_back(ObjectTileFacts{tile.shapes.empty(), tile.missing_meshes});
        totals.object_missing_meshes += tile.missing_meshes;
        totals.object_dropped_triangles += tile.dropped_triangles;
        atlas_plan.add(tile);
      }
    }
    // Invariant: plans and tile facts stay index-aligned, one of each per origin.
    if (facts.size() != object_plans.size()) throw std::runtime_error("object tile plan and build disagree");
  }
  ObjectAtlas atlas;
  std::string atlas_error;
  if (object_work) {
    try {
      atlas = atlas_plan.bake(context.assets);
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception& failure) {
      atlas_error = failure.what();
    }
    totals.atlas_entries += atlas.entries;
    totals.atlas_missing_textures += atlas.missing_textures;
    totals.atlas_clamped_shapes += atlas.clamped_shapes;
  }
  // Invariant: a changed atlas digest rewrites every tile and map together, so no tile keeps UVs into an older layout.
  const auto content_key = object_atlas_content_ledger_key(world.name);
  bool atlas_changed = false;
  if (atlas.side > 0) {
    const auto recorded_content = merged.hashes.find(content_key);
    atlas_changed = recorded_content == merged.hashes.end() || recorded_content->second != atlas.content_hash;
  }
  // Invariant: a bake failure is its own failed row, so a run that only had the atlas to restore cannot read clean.
  if (!atlas_error.empty()) {
    ++totals.failed;
    if (!totals.textures.empty()) totals.textures.push_back(',');
    totals.textures += "{\"world\":" + json_quote(world.name);
    totals.textures += ",\"kind\":\"texture\",\"map\":\"atlas\",\"status\":\"failed\",\"bytes\":0";
    totals.textures += ",\"error\":" + json_quote(atlas_error) + '}';
  }
  // Invariant: a tile is rebuilt for the write only when it will be written, so a current tile costs no second build.
  std::vector<char> needs_geometry(object_plans.size(), 0);
  if (object_work && atlas_error.empty()) {
    for (std::size_t index = 0; index < object_plans.size(); ++index) {
      needs_geometry[index] = !facts.at(index).shapeless && (wants_write(object_plans[index]) || atlas_changed);
    }
  }
  std::vector<std::size_t> level_of;
  std::vector<std::size_t> level_first;
  for (std::size_t at = 0; at < object_levels.size(); ++at) {
    level_first.push_back(level_of.size());
    level_of.insert(level_of.end(), object_levels[at].origins.size(), at);
  }
  // Invariant: the resident tiles are one level's rebuilt, remapped geometry, dropped as it is written or the level ends.
  std::size_t resident_level = object_levels.size();
  std::map<std::size_t, ObjectTile> resident;
  std::map<std::size_t, std::string> refused;
  std::string resident_error;
  const auto geometry_of = [&](std::size_t index) -> const ObjectTile& {
    const auto at = level_of.at(index);
    if (at != resident_level) {
      resident.clear();
      refused.clear();
      resident_error.clear();
      resident_level = at;
      const auto& entry = object_levels.at(at);
      std::vector<LodTile> wanted;
      std::vector<std::size_t> wanted_index;
      for (std::size_t position = 0; position < entry.origins.size(); ++position) {
        if (needs_geometry.at(level_first.at(at) + position) == 0) continue;
        wanted.push_back(entry.origins[position]);
        wanted_index.push_back(level_first.at(at) + position);
      }
      try {
        auto rebuilt = build_object_tiles(context.assets, world.settings, entry.level, wanted, entry.candidates);
        for (std::size_t slot = 0; slot < rebuilt.size() && slot < wanted_index.size(); ++slot) {
          // Constraint: a rebuilt tile that is empty or holds a shape the plan never saw must not be written.
          if (rebuilt[slot].shapes.empty() || !atlas_plan.remap(rebuilt[slot])) {
            refused.emplace(wanted_index[slot], "object tile changed while the atlas was built");
          } else {
            resident.emplace(wanted_index[slot], std::move(rebuilt[slot]));
          }
        }
      } catch (const std::bad_alloc&) {
        throw;
      } catch (const std::exception& failure) {
        resident_error = failure.what();
        if (resident_error.empty()) resident_error = "object tile rebuild failed";
      }
    }
    if (!resident_error.empty()) throw std::runtime_error(resident_error);
    const auto refusal = refused.find(index);
    if (refusal != refused.end()) throw std::runtime_error(refusal->second);
    const auto found = resident.find(index);
    if (found == resident.end()) throw std::runtime_error("object tile geometry was not rebuilt");
    return found->second;
  };
  bool atlas_write_failed = false;
  for (std::size_t index = 0; index < object_plans.size(); ++index) {
    // Invariant: a cancelled run stops before the next write, so only finished tiles are recorded.
    if (lod_run_cancelled(context)) {
      totals.cancelled = true;
      break;
    }
    const auto& plan = object_plans[index];
    const bool relaid = atlas_changed && !facts.at(index).shapeless;
    if (!wants_write(plan) && !relaid) {
      lod_report_progress(context, plan.tile);
      continue;
    }
    std::string state;
    std::size_t bytes = 0;
    std::string error;
    if (!apply) {
      state = "would-write";
      ++totals.would_write;
      if (tile_file_exists(data_dir, plan.tile)) {
        const auto entry = std::find_if(object_levels.begin(), object_levels.end(),
                                        [&](const ObjectLevel& level) { return level.level == plan.tile.level; });
        if (entry != object_levels.end()) note_existing_object_tile(context, *entry, plan.tile, totals);
      }
    } else if (!atlas_error.empty()) {
      // Constraint: a tile whose UVs were never packed must not reach disk, it would sample the wrong texels.
      state = "failed";
      error = atlas_error;
      ++totals.failed;
    } else {
      try {
        const auto& fact = facts.at(index);
        // Constraint: a BTO needs at least one shape group, so an empty tile is reported, not written.
        if (fact.shapeless) {
          // Constraint: only a tile that read every mesh is recorded as empty; one that lost a mesh is retried.
          if (fact.missing_meshes == 0) {
            merged.hashes[object_tile_empty_ledger_key(plan.tile)] = plan.inputs;
            // Invariant: a tile that turned empty takes its obsolete file and ledger row with it.
            merged.hashes.erase(lod_ledger_key(plan.tile));
            std::error_code removal;
            std::filesystem::remove(lod_tile_local_path(data_dir, plan.tile), removal);
          }
          state = "empty";
          ++totals.empty;
        } else {
          const auto payload = write_object_bto(geometry_of(index), world.name);
          resident.erase(index);
          if (write_payload_at(lod_tile_local_path(data_dir, plan.tile), payload, bytes, error)) {
            merged.hashes.erase(object_tile_empty_ledger_key(plan.tile));
            commit_lod_rebake(merged, plan);
            state = "written";
            ++totals.written;
          } else {
            state = "failed";
            ++totals.failed;
            atlas_write_failed = true;
          }
        }
      } catch (const std::bad_alloc&) {
        throw;
      } catch (const std::exception& failure) {
        // Invariant: a tile that throws is one failed row, so the world's other tiles still reach the ledger.
        state = "failed";
        error = failure.what();
        ++totals.failed;
        atlas_write_failed = true;
      }
    }
    if (!totals.tiles.empty()) totals.tiles.push_back(',');
    totals.tiles += tile_row(plan.tile, state, bytes, error);
    lod_report_progress(context, plan.tile);
  }
  // Invariant: a cancelled run records no atlas, so the next run relays the tiles it skipped.
  if (lod_run_cancelled(context)) {
    totals.cancelled = true;
    return;
  }
  // Invariant: the atlas is one per world, so every level's object tiles resolve to the same three maps.
  const bool atlas_idle = atlas_expected && !object_work;
  if (!atlas_idle && atlas.side <= 0) return;
  for (std::size_t index = 0; index < k_object_atlas_maps; ++index) {
    const auto map = static_cast<ObjectAtlasMap>(index);
    const auto key = object_atlas_ledger_key(world.name, map);
    const auto hash = object_atlas_inputs_hash(world.name, map, shared);
    const auto target = object_atlas_local_path(data_dir, world.name, map);
    std::string state;
    std::size_t bytes = 0;
    std::string error;
    if (atlas_idle || (!atlas_changed && atlas_current(map))) {
      state = "skipped";
      ++totals.textures_skipped;
    } else {
      try {
        const auto payload = write_object_atlas_dds(atlas, map);
        if (write_payload_at(target, payload, bytes, error)) {
          merged.hashes[key] = hash;
          state = "written";
          ++totals.textures_written;
        } else {
          state = "failed";
          ++totals.failed;
          atlas_write_failed = true;
        }
      } catch (const std::bad_alloc&) {
        throw;
      } catch (const std::exception& failure) {
        error = failure.what();
        state = "failed";
        ++totals.failed;
        atlas_write_failed = true;
      }
    }
    if (!totals.textures.empty()) totals.textures.push_back(',');
    totals.textures += "{\"world\":" + json_quote(world.name);
    totals.textures += ",\"kind\":\"texture\",\"map\":" + json_quote(object_atlas_map_name(map));
    if (atlas.side > 0) totals.textures += ",\"side\":" + std::to_string(atlas.side);
    totals.textures += ",\"status\":" + json_quote(state);
    totals.textures += ",\"bytes\":" + std::to_string(bytes);
    if (!error.empty()) totals.textures += ",\"error\":" + json_quote(error);
    totals.textures += '}';
  }
  // Constraint: the digest is recorded only once every tile and map reached disk, so a failed run relays again.
  if (atlas.side > 0 && !atlas_write_failed) merged.hashes[content_key] = atlas.content_hash;
}

}  // namespace fo4::lod
