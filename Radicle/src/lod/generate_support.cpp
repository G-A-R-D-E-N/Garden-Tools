#include "fo4/lod/generate_support.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fo4::lod {

void append_count(std::string& json, std::string_view name, std::size_t value) {
  if (value == 0) return;
  json += ",\"";
  json.append(name);
  json += "\":" + std::to_string(value);
}

std::string json_quote(std::string_view text) {
  std::string out{"\""};
  for (const char raw : text) {
    const auto ch = static_cast<unsigned char>(raw);
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(ch));
    } else if (ch < 0x20) {
      const char* const hex = "0123456789abcdef";
      out += "\\u00";
      out.push_back(hex[(ch >> 4) & 0xF]);
      out.push_back(hex[ch & 0xF]);
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  out.push_back('"');
  return out;
}

bool lod_level_in_scope(const LodGenerateWorld& context, std::int32_t level) noexcept {
  return level >= context.level_min && level <= context.level_max;
}

bool lod_level_scope_is_partial(const LodSettings& settings, std::int32_t level_min, std::int32_t level_max) noexcept {
  // Constraint: a world grid may start below k_lod_level_min, so its own range must be clamped before comparing.
  const auto world_min = std::max(settings.level_min, k_lod_level_min);
  const auto world_max = std::min(settings.level_max, k_lod_level_max);
  return level_min > world_min || level_max < world_max;
}

bool lod_run_cancelled(const LodGenerateWorld& context) noexcept {
  if (context.progress == nullptr || context.progress->cancel == nullptr) return false;
  // Constraint: the flag is written by the caller's thread, so it must be read atomically, never through a plain load.
  return std::atomic_ref<std::int32_t>(*context.progress->cancel).load(std::memory_order_relaxed) != 0;
}

void lod_report_progress(const LodGenerateWorld& context, const LodTile& tile) {
  if (context.progress == nullptr) return;
  auto* sink = context.progress;
  ++sink->done;
  if (sink->report == nullptr) return;
  // Constraint: the kind must outlive the call, so it is copied into a local the callee may read.
  const std::string kind{lod_tile_kind_name(tile.kind)};
  sink->report(sink->user, context.world.name.c_str(), kind.c_str(), tile.level, tile.x, tile.y, sink->done,
               sink->total);
}

bool write_payload_at(const std::filesystem::path& target, std::span<const std::byte> payload, std::size_t& bytes,
                      std::string& error) {
  try {
    std::error_code status;
    std::filesystem::create_directories(target.parent_path(), status);
    // Invariant: tiles of one world share a folder, so a folder another worker just made is success, not failure.
    if (status && !std::filesystem::is_directory(target.parent_path())) {
      error = "creating the tile folder failed: " + status.message();
      return false;
    }
    auto temporary = target;
    temporary += ".tmp";
    {
      std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
      out.flush();
      if (!out) {
        out.close();
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        error = "writing the temporary tile failed";
        return false;
      }
    }
    std::filesystem::rename(temporary, target, status);
    if (status) {
      std::error_code cleanup;
      std::filesystem::remove(temporary, cleanup);
      error = "replacing the tile failed: " + status.message();
      return false;
    }
    const auto written = std::filesystem::file_size(target, status);
    if (status || written != payload.size()) {
      error = "the written tile does not match the payload size";
      return false;
    }
    bytes = payload.size();
    return true;
  } catch (const std::exception& failure) {
    error = failure.what();
    return false;
  }
}

bool tile_on_disk(const std::filesystem::path& data_dir, const LodTile& tile) {
  std::error_code status;
  const auto size = std::filesystem::file_size(lod_tile_local_path(data_dir, tile), status);
  return !status && size >= k_min_tile_bytes;
}

bool tile_file_exists(const std::filesystem::path& data_dir, const LodTile& tile) {
  if (data_dir.empty()) return false;
  std::error_code status;
  return std::filesystem::is_regular_file(lod_tile_local_path(data_dir, tile), status);
}

void add_dry_run_file(LodPathList& list, const std::filesystem::path& data_dir, const std::filesystem::path& file) {
  if (list.paths.size() >= k_lod_dry_run_path_cap) {
    list.truncated = true;
    return;
  }
  list.paths.push_back(file.lexically_relative(data_dir).generic_string());
}

void add_dry_run_path(LodPathList& list, const std::filesystem::path& data_dir, const LodTile& tile) {
  add_dry_run_file(list, data_dir, lod_tile_local_path(data_dir, tile));
}

void append_path_list(std::string& json, std::string_view name, const LodPathList& list) {
  if (list.paths.empty()) return;
  json += ",\"";
  json.append(name);
  json += "\":[";
  for (std::size_t index = 0; index < list.paths.size(); ++index) {
    if (index != 0) json.push_back(',');
    json += json_quote(list.paths.at(index));
  }
  json.push_back(']');
  if (list.truncated) {
    json += ",\"";
    json.append(name);
    json += "Truncated\":true";
  }
}

std::string tile_row(const LodTile& tile, std::string_view state, std::size_t bytes, const std::string& error) {
  std::string row{"{\"world\":" + json_quote(tile.worldspace)};
  row += ",\"kind\":" + json_quote(lod_tile_kind_name(tile.kind));
  row += ",\"level\":" + std::to_string(tile.level);
  row += ",\"x\":" + std::to_string(tile.x);
  row += ",\"y\":" + std::to_string(tile.y);
  row += ",\"status\":" + json_quote(state);
  row += ",\"bytes\":" + std::to_string(bytes);
  if (!error.empty()) row += ",\"error\":" + json_quote(error);
  row += '}';
  return row;
}

bool object_level_supported(std::int32_t level) {
  return level == 4 || level == 8 || level == 16 || level == 32;
}

std::vector<LodTile> object_tiles(const LodWorld& world, std::int32_t level,
                                  const LodCandidateCollection& candidates) {
  auto tiles = object_tile_origins(world.settings, level, candidates);
  for (auto& tile : tiles) tile.worldspace = world.name;
  return tiles;
}

namespace {

bool terrain_tile_has_content(const WorldTerrain& terrain, std::int32_t level, std::int32_t x, std::int32_t y) {
  for (std::int32_t cy = 0; cy < level; ++cy) {
    for (std::int32_t cx = 0; cx < level; ++cx) {
      const std::pair<std::int32_t, std::int32_t> key{x + cx, y + cy};
      if (terrain.land.has_land(key)) return true;
      if (terrain.water_heights.count(key) != 0) return true;
    }
  }
  return false;
}

}  // namespace

std::vector<LodTile> terrain_tiles(const LodWorld& world, const WorldTerrain& terrain) {
  std::vector<LodTile> tiles;
  const auto& settings = world.settings;
  for (std::int32_t level = settings.level_min; level <= settings.level_max;) {
    if (level <= 0) break;
    const auto steps = settings.stride / level;
    for (std::int32_t grid_y = 0; grid_y < steps; ++grid_y) {
      for (std::int32_t grid_x = 0; grid_x < steps; ++grid_x) {
        const auto x = static_cast<std::int32_t>(settings.southwest_x) + grid_x * level;
        const auto y = static_cast<std::int32_t>(settings.southwest_y) + grid_y * level;
        if (!terrain_tile_has_content(terrain, level, x, y)) continue;
        LodTile tile{world.name, level, x, y, LodTileKind::terrain};
        if (check_lod_tile_placement(settings, tile) != LodTilePlacement::ok) continue;
        tiles.push_back(std::move(tile));
      }
    }
    if (level > settings.level_max / 2) break;
    level *= 2;
  }
  return tiles;
}

}  // namespace fo4::lod
