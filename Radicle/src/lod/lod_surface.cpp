#include "fo4/lod/lod_surface.hpp"

#include "fo4/hash.hpp"
#include "fo4/lod/generate_support.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/object_tile.hpp"
#include "fo4/lod/terrain_tile.hpp"
#include "fo4/signature.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fo4::lod {
namespace {

// Constraint: bump a tile-kind format when that kind's tile output or input set changes, so old tiles rebuild once.
constexpr std::uint64_t k_terrain_format = 5;
constexpr std::uint64_t k_object_format = 3;

void hash_bytes(std::uint64_t& hash, std::string_view text) {
  hash = fnv1a64(text, hash);
}

void hash_length(std::uint64_t& hash, std::uint64_t length) {
  hash_bytes(hash, std::to_string(length));
  hash_bytes(hash, std::string_view{"\n"});
}

std::string lower_ascii(std::string text) {
  for (auto& ch : text) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return text;
}

std::string signature_of(const LodTile& tile) { return std::string{lod_tile_kind_name(tile.kind)}; }

std::optional<std::int32_t> parse_i32(std::string_view text) {
  std::int32_t value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

}  // namespace

std::vector<LodWorld> list_lod_worlds(const env::Environment& environment,
                                      const archive::AssetResolver& assets,
                                      std::size_t* failed) {
  std::vector<LodWorld> worlds;
  for (const auto* ref : environment.winners_of(sig_wrld)) {
    const auto meta = environment.metadata(*ref);
    if (!meta.editor_id || meta.editor_id->empty()) continue;
    const std::string name{*meta.editor_id};
    try {
      const auto path = lod_settings_path(name);
      const auto location = assets.locate(path);
      if (!location.has_value()) continue;
      const auto bytes = assets.read(*location);
      LodWorld world;
      world.key = ref->record->form_key;
      world.name = name;
      world.settings = parse_lod_settings(bytes);
      worlds.push_back(std::move(world));
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception&) {
      // Constraint: a malformed lodsettings must be counted, so a world cannot vanish without a trace.
      if (failed != nullptr) ++*failed;
    }
  }
  return worlds;
}

std::vector<LodWorld> list_lod_worlds_without_settings(const env::Environment& environment,
                                                       const archive::AssetResolver& assets) {
  std::vector<LodWorld> worlds;
  for (const auto* ref : environment.winners_of(sig_wrld)) {
    const auto meta = environment.metadata(*ref);
    if (!meta.editor_id || meta.editor_id->empty()) continue;
    const std::string name{*meta.editor_id};
    if (!lod_worldspace_name_is_safe(name)) continue;
    try {
      if (assets.locate(lod_settings_path(name)).has_value()) continue;
    } catch (const std::exception&) {
      continue;
    }
    LodWorld world;
    world.key = ref->record->form_key;
    world.name = name;
    worlds.push_back(std::move(world));
  }
  return worlds;
}

std::string lod_ledger_key(const LodTile& tile) {
  return lower_ascii(tile.worldspace) + "|" + signature_of(tile) + "|" + std::to_string(tile.level) + "|" +
         std::to_string(tile.x) + "|" + std::to_string(tile.y);
}

LodLedger parse_lod_ledger(std::string_view text) {
  LodLedger ledger;
  std::size_t position = 0;
  while (position < text.size()) {
    const auto end = text.find('\n', position);
    auto line = text.substr(position, end == std::string_view::npos ? text.size() - position : end - position);
    position = end == std::string_view::npos ? text.size() : end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) continue;
    const auto separator = line.rfind(' ');
    if (separator == std::string_view::npos || separator == 0) continue;
    const auto key = line.substr(0, separator);
    const auto value_text = line.substr(separator + 1);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(value_text.data(), value_text.data() + value_text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != value_text.data() + value_text.size()) continue;
    ledger.hashes[std::string{key}] = value;
  }
  return ledger;
}

std::string format_lod_ledger(const LodLedger& ledger) {
  std::string text;
  for (const auto& [key, value] : ledger.hashes) {
    text += key;
    text += ' ';
    text += std::to_string(value);
    text += '\n';
  }
  return text;
}

std::optional<LodInput> loose_file_input(const archive::AssetResolver& assets, std::string_view asset_path) {
  std::optional<std::filesystem::path> path;
  try {
    path = assets.locate_loose(asset_path);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  if (!path.has_value()) return std::nullopt;
  std::error_code status;
  const auto size = std::filesystem::file_size(*path, status);
  if (status) return std::nullopt;
  const auto stamp = std::filesystem::last_write_time(*path, status);
  if (status) return std::nullopt;
  return LodInput{std::string{asset_path}, static_cast<std::uint64_t>(size), 0,
                  static_cast<std::uint64_t>(stamp.time_since_epoch().count())};
}

std::uint64_t lod_texture_inputs_hash(const LodTile& tile, std::span<const LodInput> inputs) {
  std::uint64_t hash = k_fnv64_offset;
  hash_bytes(hash, lod_ledger_key(tile));
  for (const auto& input : inputs) {
    hash_length(hash, input.path.size());
    hash_bytes(hash, input.path);
    hash_length(hash, input.size);
    hash_length(hash, input.digest);
    hash_length(hash, input.write_time);
  }
  return hash;
}

std::uint64_t lod_tile_inputs_hash(const LodTile& tile, std::span<const LodInput> inputs) {
  auto hash = lod_texture_inputs_hash(tile, inputs);
  hash_length(hash, tile.kind == LodTileKind::terrain ? k_terrain_format : k_object_format);
  return hash;
}

std::vector<LodTilePlan> plan_lod_rebakes(const std::vector<LodTile>& tiles,
                                          std::span<const std::vector<LodInput>> inputs, const LodLedger& ledger) {
  if (inputs.size() != tiles.size()) throw LodError("every tile needs exactly one input list");
  std::vector<LodTilePlan> plans;
  plans.reserve(tiles.size());
  for (std::size_t index = 0; index < tiles.size(); ++index) {
    LodTilePlan plan;
    plan.tile = tiles[index];
    plan.inputs = lod_tile_inputs_hash(plan.tile, inputs[index]);
    const auto found = ledger.hashes.find(lod_ledger_key(plan.tile));
    plan.rebuild = found == ledger.hashes.end() || found->second != plan.inputs;
    plans.push_back(std::move(plan));
  }
  return plans;
}

void commit_lod_rebake(LodLedger& ledger, const LodTilePlan& plan) {
  ledger.hashes[lod_ledger_key(plan.tile)] = plan.inputs;
}

bool lod_progress_complete(const LodProgress& progress) {
  return progress.tiles_done == progress.tiles_total &&
         progress.tiles_done == progress.tiles_rebuilt + progress.tiles_skipped;
}

std::vector<std::byte> preview_terrain_tile(const env::Environment& environment, const LodWorld& world,
                                            std::int32_t level, std::int32_t tile_x, std::int32_t tile_y) {
  const auto tile = build_terrain_tile(environment, world.key, world.settings, level, tile_x, tile_y);
  return write_terrain_btr(tile, world.name);
}

std::optional<std::vector<std::byte>> preview_object_tile(const env::Environment& environment,
                                                          const archive::AssetResolver& assets,
                                                          const LodWorld& world, std::int32_t level,
                                                          std::int32_t tile_x, std::int32_t tile_y) {
  const auto is_addressed = [&](const LodTile& origin) { return origin.x == tile_x && origin.y == tile_y; };
  bool level_baked = false;
  for_each_object_level(world.settings, [&](std::int32_t current) { level_baked = level_baked || current == level; });
  if (!level_baked) return std::nullopt;
  // Invariant: origins do not depend on LAND, so a cell with no object tile misses before any LAND decode or build.
  auto addressed_level =
      collect_object_lod_candidates(environment, world.key, level, std::make_shared<const LandGrids>());
  const auto addressed_origins = object_tiles(world, level, addressed_level);
  if (std::none_of(addressed_origins.begin(), addressed_origins.end(), is_addressed)) return std::nullopt;
  const auto land = std::make_shared<const LandGrids>(collect_land_grids(environment, world.key));
  // Invariant: the cull reads candidates.terrain, so the early collection must be given the real LAND before it builds.
  addressed_level.terrain = land;
  // Invariant: the layout needs every level's shape keys, but a level's geometry is freed once its keys are added.
  ObjectAtlasPlan plan;
  std::optional<ObjectTile> addressed;
  for_each_object_level(world.settings, [&](std::int32_t current) {
    auto collected = current == level ? std::move(addressed_level)
                                      : collect_object_lod_candidates(environment, world.key, current, land);
    const auto origins = object_tiles(world, current, collected);
    if (origins.empty()) return;
    auto built = build_object_tiles(assets, world.settings, current, origins, collected);
    for (std::size_t slot = 0; slot < built.size(); ++slot) {
      plan.add(built[slot]);
      // Invariant: the tile is addressed by its own level and origin, never by its list position alone.
      if (current == level && slot < origins.size() && is_addressed(origins[slot])) {
        addressed = std::move(built[slot]);
      }
    }
  });
  if (!addressed.has_value()) return std::nullopt;
  // Constraint: packing rewrites a tile's UVs, so the addressed tile is remapped only after the layout is set.
  plan.bake(assets);
  if (addressed->shapes.empty()) return std::nullopt;
  if (!plan.remap(*addressed)) throw LodError("object tile changed while the atlas was built");
  return write_object_bto(*addressed, world.name);
}

std::filesystem::path lod_tile_local_path(const std::filesystem::path& root, const LodTile& tile) {
  // Constraint: a ledger key is untrusted input, so the world must stay one plain path component.
  if (!lod_worldspace_name_is_safe(tile.worldspace)) {
    throw LodError("a LOD tile path needs a plain worldspace name");
  }
  if (tile.level <= 0) throw LodError("a LOD tile path needs a positive level");
  std::filesystem::path path = root;
  path /= "Meshes";
  path /= "Terrain";
  path /= tile.worldspace;
  if (tile.kind == LodTileKind::object) path /= "Objects";
  std::string name{tile.worldspace};
  name += '.';
  name += std::to_string(tile.level);
  name += '.';
  name += std::to_string(tile.x);
  name += '.';
  name += std::to_string(tile.y);
  name += tile.kind == LodTileKind::object ? ".BTO" : ".BTR";
  path /= name;
  return path;
}

std::optional<LodTile> parse_lod_ledger_key(std::string_view key) {
  std::array<std::string_view, 5> parts{};
  std::size_t count = 0;
  std::size_t start = 0;
  while (count < parts.size()) {
    const auto bar = key.find('|', start);
    parts[count++] = bar == std::string_view::npos ? key.substr(start) : key.substr(start, bar - start);
    if (bar == std::string_view::npos) break;
    start = bar + 1;
  }
  if (count != parts.size() || parts[0].empty()) return std::nullopt;
  if (!lod_worldspace_name_is_safe(parts[0])) return std::nullopt;
  LodTile tile;
  if (parts[1] == "terrain") {
    tile.kind = LodTileKind::terrain;
  } else if (parts[1] == "object") {
    tile.kind = LodTileKind::object;
  } else {
    return std::nullopt;
  }
  const auto level = parse_i32(parts[2]);
  const auto x = parse_i32(parts[3]);
  const auto y = parse_i32(parts[4]);
  if (!level || !x || !y || *level <= 0) return std::nullopt;
  tile.worldspace = std::string{parts[0]};
  tile.level = *level;
  tile.x = *x;
  tile.y = *y;
  return tile;
}

}  // namespace fo4::lod
