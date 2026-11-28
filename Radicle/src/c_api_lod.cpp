#include "fo4/c_api_lod.h"

#include "c_api_internal.hpp"
#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/error.hpp"
#include "fo4/hash.hpp"
#include "fo4/lod/generate_support.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/object_atlas_verify.hpp"
#include "fo4/lod/object_candidates.hpp"
#include "fo4/lod/object_generate.hpp"
#include "fo4/lod/object_tile.hpp"
#include "fo4/lod/terrain_generate.hpp"
#include "fo4/lod/terrain_heightfield.hpp"
#include "fo4/lod/terrain_texture.hpp"
#include "fo4/lod/terrain_tile.hpp"
#include "fo4/text/encoding.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

struct radicle_lod {
  std::unique_ptr<fo4::env::Environment> environment;
  std::unique_ptr<fo4::archive::AssetResolver> assets;
  std::filesystem::path data_dir;
  // Invariant: the plugin files are digested once per session, so every tile shares one input list.
  std::vector<fo4::lod::LodInput> plugin_inputs;
  // Invariant: a replaced archive must reopen the tiles its assets fed, so each one is an input by name and stamp.
  std::vector<fo4::lod::LodInput> archive_inputs;
};

namespace {

using namespace radicle_c_detail;

std::vector<std::filesystem::path> split_paths(std::string_view text) {
  std::vector<std::filesystem::path> paths;
  std::size_t position = 0;
  while (position <= text.size()) {
    const auto end = text.find('\n', position);
    auto line = end == std::string_view::npos ? text.substr(position) : text.substr(position, end - position);
    position = end == std::string_view::npos ? text.size() + 1 : end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) paths.push_back(utf8_path(line));
  }
  return paths;
}

constexpr CopyMessages k_payload_messages{"out bytes and size are required", "payload allocation failed"};

void release_payload(uint8_t* bytes) { std::free(bytes); }

using fo4::lod::append_count;
using fo4::lod::for_each_object_level;
using fo4::lod::json_quote;
using fo4::lod::k_min_tile_bytes;
using fo4::lod::object_tiles;
using fo4::lod::terrain_tiles;

std::vector<std::uint8_t> to_payload(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

// Invariant: non-finite floats become null so the payload always parses as JSON.
std::string json_number(float value) { return std::isfinite(value) ? std::to_string(value) : std::string{"null"}; }

template <std::size_t N>
std::string json_numbers(const std::array<float, N>& values) {
  std::string out;
  for (std::size_t index = 0; index < N; ++index) {
    if (index != 0) out.push_back(',');
    out += json_number(values[index]);
  }
  return out;
}

std::string json_form_key(const fo4::FormKey& key) {
  return "{\"plugin\":" + json_quote(key.owner().filename()) + ",\"localId\":" +
         std::to_string(key.local().value()) + "}";
}

std::uint64_t digest_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return 0;
  std::uint64_t hash = fo4::k_fnv64_offset;
  std::array<char, 1U << 16> buffer{};
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = static_cast<std::size_t>(in.gcount());
    hash = fo4::fnv1a64(std::string_view{buffer.data(), read}, hash);
  }
  return hash;
}

std::uint64_t digest_bytes(std::span<const std::byte> bytes) {
  return fo4::fnv1a64_bytes(bytes);
}

// Constraint: an input is keyed by file name, size and content or archive write time, never by absolute path.
std::optional<fo4::lod::LodInput> input_for_file(const std::filesystem::path& path, bool digest_content) {
  std::error_code status;
  const auto size = std::filesystem::file_size(path, status);
  if (status) return std::nullopt;
  const auto stamp = std::filesystem::last_write_time(path, status);
  if (status) return std::nullopt;
  return fo4::lod::LodInput{path.filename().string(), static_cast<std::uint64_t>(size),
                            digest_content ? digest_file(path) : 0,
                            digest_content ? 0 : static_cast<std::uint64_t>(stamp.time_since_epoch().count())};
}

// Invariant: a tile is rebuilt when a plugin changes, and the lodsettings file is an input too.
std::optional<fo4::lod::LodInput> settings_input(const radicle_lod& lod, const fo4::lod::LodWorld& world) {
  if (!fo4::lod::lod_worldspace_name_is_safe(world.name)) return std::nullopt;
  try {
    const auto path = fo4::lod::lod_settings_path(world.name);
    const auto location = lod.assets->locate(path);
    if (!location.has_value()) return std::nullopt;
    const auto bytes = lod.assets->read(*location);
    return fo4::lod::LodInput{path, static_cast<std::uint64_t>(bytes.size()), digest_bytes(bytes)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::vector<fo4::lod::LodInput> tile_inputs(const radicle_lod& lod, const fo4::lod::LodWorld& world) {
  auto inputs = lod.plugin_inputs;
  inputs.insert(inputs.end(), lod.archive_inputs.begin(), lod.archive_inputs.end());
  if (auto settings = settings_input(lod, world)) inputs.push_back(std::move(*settings));
  return inputs;
}

using LodInputList = std::vector<fo4::lod::LodInput>;
using LodInputsByTile = std::map<std::string, LodInputList>;

// Constraint: a data folder with no loose mesh, material or texture folder has no per-tile input to collect.
bool has_loose_object_folders(const std::filesystem::path& data_dir) {
  std::error_code status;
  for (const auto* folder : {"Meshes", "Materials", "Textures"}) {
    if (std::filesystem::is_directory(data_dir / folder, status)) return true;
  }
  return false;
}

// Invariant: a tile's own loose files are collected without terrain culling, so plan, generate and verify agree.
LodInputsByTile loose_inputs_by_tile(const radicle_lod& lod, const fo4::lod::LodWorld& world,
                                     std::int32_t level_min, std::int32_t level_max,
                                     fo4::lod::ObjectLooseMeshCache& meshes) {
  LodInputsByTile extras;
  if (lod.data_dir.empty() || !has_loose_object_folders(lod.data_dir)) return extras;
  // Invariant: a partial scope bakes no object tile, so nothing collects a per-tile loose input for it.
  if (fo4::lod::lod_level_scope_is_partial(world.settings, level_min, level_max)) return extras;
  const auto no_land = std::make_shared<const fo4::lod::LandGrids>();
  fo4::lod::for_each_object_level(world.settings, [&](std::int32_t level) {
    // Constraint: only a level in scope can bake, so a level outside it collects no input.
    if (level < level_min || level > level_max) return;
    const auto candidates = fo4::lod::collect_object_lod_candidates(*lod.environment, world.key, level, no_land);
    fo4::lod::collect_object_loose_inputs(*lod.assets, world.settings, world.name, level, candidates, extras,
                                          meshes);
  });
  return extras;
}

// Invariant: one tile's input list is the shared files plus that tile's own loose files, never another tile's.
LodInputList tile_input_list(const LodInputList& shared, const LodInputsByTile& extras, const std::string& key) {
  const auto found = extras.find(key);
  if (found == extras.end() || found->second.empty()) return shared;
  auto list = shared;
  list.insert(list.end(), found->second.begin(), found->second.end());
  return list;
}

// Invariant: verification also covers the files already on disk, so a stray tile never goes unreported.
std::vector<fo4::lod::LodTile> archive_tiles(const radicle_lod& lod, const std::string& world,
                                            std::map<std::string, std::uintmax_t>& sizes) {
  std::vector<fo4::lod::LodTile> tiles;
  const auto root = std::string{"Meshes\\Terrain\\"} + world + "\\";
  for (const auto* extension : {".btr", ".bto"}) {
    for (const auto& path : lod.assets->list(root, extension, false)) {
      try {
        auto tile = fo4::lod::parse_lod_tile_path(path.path);
        if (!tile.has_value()) continue;
        // Constraint: a listing key is lower-cased, so the size must come from the listing, not a rebuilt path.
        auto& size = sizes[fo4::lod::lod_ledger_key(*tile)];
        size = std::max(size, static_cast<std::uintmax_t>(path.size));
        tiles.push_back(std::move(*tile));
      } catch (const std::invalid_argument&) {
        continue;
      }
    }
  }
  return tiles;
}

}  // namespace

radicle_status radicle_lod_open(const char* plugin_paths, const char* data_dir, const char* archive_paths,
                                radicle_lod** out_lod) {
  clear_error();
  if (out_lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out session is required");
  out_lod[0] = nullptr;
  if (plugin_paths == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin paths are required");
  return guard_exceptions("lod open failed", [&]() -> radicle_status {
    const auto plugins = split_paths(plugin_paths);
    if (plugins.empty()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "at least one plugin path is required");
    const std::vector<std::filesystem::path> roots{
        data_dir == nullptr || *data_dir == '\0' ? std::filesystem::path{} : utf8_path(data_dir)};
    const auto archives = archive_paths == nullptr ? std::vector<std::filesystem::path>{} : split_paths(archive_paths);
    auto session = std::make_unique<radicle_lod>();
    session->data_dir = roots.front();
    session->environment = std::make_unique<fo4::env::Environment>(fo4::env::Environment::load(plugins));
    session->assets =
        std::make_unique<fo4::archive::AssetResolver>(fo4::archive::AssetResolver::open_roots(roots, archives));
    session->plugin_inputs.reserve(plugins.size());
    for (const auto& plugin : plugins) {
      if (auto input = input_for_file(plugin, true)) session->plugin_inputs.push_back(std::move(*input));
    }
    session->archive_inputs.reserve(archives.size());
    for (const auto& archive : archives) {
      if (auto input = input_for_file(archive, false)) session->archive_inputs.push_back(std::move(*input));
    }
    out_lod[0] = session.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_lod_destroy(radicle_lod* lod) { delete lod; }

radicle_status radicle_lod_worlds_json(const radicle_lod* lod, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out bytes and size are required");
  }
  out_bytes[0] = nullptr;
  out_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  return guard_exceptions("lod worlds failed", [&]() -> radicle_status {
    const auto worlds = fo4::lod::list_lod_worlds(*lod->environment, *lod->assets);
    std::string json{"["};
    for (std::size_t index = 0; index < worlds.size(); ++index) {
      const auto& world = worlds[index];
      if (index != 0) json.push_back(',');
      json += "{\"name\":" + json_quote(world.name);
      json += ",\"southwestX\":" + std::to_string(world.settings.southwest_x);
      json += ",\"southwestY\":" + std::to_string(world.settings.southwest_y);
      json += ",\"stride\":" + std::to_string(world.settings.stride);
      json += ",\"levelMin\":" + std::to_string(world.settings.level_min);
      json += ",\"levelMax\":" + std::to_string(world.settings.level_max);
      json += "}";
    }
    json += "]";
    return copy_out(to_payload(json), out_bytes, out_size, k_payload_messages);
  });
}

radicle_status radicle_lod_plan_json(const radicle_lod* lod, const char* ledger_text, uint8_t** out_plan,
                                     size_t* out_plan_size, uint8_t** out_ledger, size_t* out_ledger_size,
                                     uint8_t** out_commit, size_t* out_commit_size) {
  clear_error();
  if (out_plan == nullptr || out_plan_size == nullptr || out_ledger == nullptr || out_ledger_size == nullptr ||
      out_commit == nullptr || out_commit_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plan, ledger and commit outputs are required");
  }
  out_plan[0] = nullptr;
  out_plan_size[0] = 0;
  out_ledger[0] = nullptr;
  out_ledger_size[0] = 0;
  out_commit[0] = nullptr;
  out_commit_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  return guard_exceptions("lod plan failed", [&]() -> radicle_status {
    const auto ledger =
        fo4::lod::parse_lod_ledger(ledger_text == nullptr ? std::string_view{} : std::string_view{ledger_text});
    std::string json{"["};
    bool first = true;
    fo4::lod::LodLedger commit_ledger;
    // Constraint: plan only needs tile origins, so an empty LAND set keeps each level from decoding the world's LAND.
    const auto no_land = std::make_shared<const fo4::lod::LandGrids>();
    fo4::lod::ObjectLooseMeshCache loose_meshes;
    for (const auto& world : fo4::lod::list_lod_worlds(*lod->environment, *lod->assets)) {
      // Constraint: object tiles must be planned here too, or the plan under-reports what generate writes.
      // Invariant: plan only lists tiles, so land is collected as presence and no height grid is kept.
      const auto terrain =
          fo4::lod::collect_world_terrain(*lod->environment, world.key, fo4::lod::LandDetail::presence);
      auto tiles = terrain_tiles(world, terrain);
      for_each_object_level(world.settings, [&](std::int32_t level) {
        const auto candidates = fo4::lod::collect_object_lod_candidates(*lod->environment, world.key, level, no_land);
        auto origins = object_tiles(world, level, candidates);
        tiles.insert(tiles.end(), origins.begin(), origins.end());
      });
      const auto shared = tile_inputs(*lod, world);
      const auto extras = loose_inputs_by_tile(*lod, world, fo4::lod::k_lod_level_min, fo4::lod::k_lod_level_max,
                                              loose_meshes);
      std::vector<std::vector<fo4::lod::LodInput>> inputs;
      inputs.reserve(tiles.size());
      for (const auto& tile : tiles) inputs.push_back(tile_input_list(shared, extras, fo4::lod::lod_ledger_key(tile)));
      const auto plans = fo4::lod::plan_lod_rebakes(tiles, inputs, ledger);
      for (const auto& plan : plans) {
        if (plan.rebuild) fo4::lod::commit_lod_rebake(commit_ledger, plan);
        if (!first) json.push_back(',');
        first = false;
        json += "{\"world\":" + json_quote(plan.tile.worldspace);
        json += ",\"kind\":";
        json += json_quote(fo4::lod::lod_tile_kind_name(plan.tile.kind));
        json += ",\"level\":" + std::to_string(plan.tile.level);
        json += ",\"x\":" + std::to_string(plan.tile.x);
        json += ",\"y\":" + std::to_string(plan.tile.y);
        json += ",\"inputs\":" + std::to_string(plan.inputs);
        json += ",\"rebuild\":";
        json += plan.rebuild ? "true" : "false";
        json += "}";
      }
    }
    json += "]";
    const auto plan_payload = to_payload(json);
    const auto ledger_payload = to_payload(fo4::lod::format_lod_ledger(ledger));
    const auto commit_payload = to_payload(fo4::lod::format_lod_ledger(commit_ledger));
    const auto status = copy_out(plan_payload, out_plan, out_plan_size, k_payload_messages);
    if (status != RADICLE_STATUS_OK) return status;
    const auto ledger_status = copy_out(ledger_payload, out_ledger, out_ledger_size, k_payload_messages);
    if (ledger_status != RADICLE_STATUS_OK) {
      release_payload(out_plan[0]);
      out_plan[0] = nullptr;
      out_plan_size[0] = 0;
      return ledger_status;
    }
    const auto commit_status = copy_out(commit_payload, out_commit, out_commit_size, k_payload_messages);
    if (commit_status != RADICLE_STATUS_OK) {
      release_payload(out_plan[0]);
      out_plan[0] = nullptr;
      out_plan_size[0] = 0;
      release_payload(out_ledger[0]);
      out_ledger[0] = nullptr;
      out_ledger_size[0] = 0;
    }
    return commit_status;
  });
}

radicle_status radicle_lod_commit_json(const char* ledger_text, const char* commit_text, uint8_t** out_ledger,
                                       size_t* out_ledger_size) {
  clear_error();
  if (out_ledger == nullptr || out_ledger_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out ledger and size are required");
  }
  out_ledger[0] = nullptr;
  out_ledger_size[0] = 0;
  return guard_exceptions("lod commit failed", [&]() -> radicle_status {
    auto ledger =
        fo4::lod::parse_lod_ledger(ledger_text == nullptr ? std::string_view{} : std::string_view{ledger_text});
    const auto commits =
        fo4::lod::parse_lod_ledger(commit_text == nullptr ? std::string_view{} : std::string_view{commit_text});
    for (const auto& [key, value] : commits.hashes) ledger.hashes[key] = value;
    return copy_out(to_payload(fo4::lod::format_lod_ledger(ledger)), out_ledger, out_ledger_size, k_payload_messages);
  });
}

radicle_status radicle_lod_preview_nif(const radicle_lod* lod, const char* world, int32_t level, int32_t x, int32_t y,
                                       uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out bytes and size are required");
  }
  out_bytes[0] = nullptr;
  out_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  if (world == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "world name is required");
  return guard_exceptions("lod preview failed", [&]() -> radicle_status {
    for (const auto& candidate : fo4::lod::list_lod_worlds(*lod->environment, *lod->assets)) {
      if (!fo4::text::equals_folded(candidate.name, world)) continue;
      const auto bytes = fo4::lod::preview_terrain_tile(*lod->environment, candidate, level, x, y);
      const std::vector<std::uint8_t> payload(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              reinterpret_cast<const std::uint8_t*>(bytes.data()) + bytes.size());
      return copy_out(payload, out_bytes, out_size, k_payload_messages);
    }
    return fail(RADICLE_STATUS_NOT_FOUND, "LOD settings for that world are not loaded");
  });
}

radicle_status radicle_lod_preview_object_nif(const radicle_lod* lod, const char* world, int32_t level, int32_t x,
                                              int32_t y, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out bytes and size are required");
  }
  out_bytes[0] = nullptr;
  out_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  if (world == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "world name is required");
  return guard_exceptions("lod object preview failed", [&]() -> radicle_status {
    for (const auto& candidate : fo4::lod::list_lod_worlds(*lod->environment, *lod->assets)) {
      if (!fo4::text::equals_folded(candidate.name, world)) continue;
      const auto built =
          fo4::lod::preview_object_tile(*lod->environment, *lod->assets, candidate, level, x, y);
      // Constraint: an absent tile is a miss, so the panel can say why instead of previewing empty geometry.
      if (!built.has_value()) {
        return fail(RADICLE_STATUS_NOT_FOUND, "the object tile holds no shape at that level and cell");
      }
      const std::vector<std::uint8_t> payload(reinterpret_cast<const std::uint8_t*>(built->data()),
                                              reinterpret_cast<const std::uint8_t*>(built->data()) + built->size());
      return copy_out(payload, out_bytes, out_size, k_payload_messages);
    }
    return fail(RADICLE_STATUS_NOT_FOUND, "LOD settings for that world are not loaded");
  });
}

radicle_status radicle_lod_candidates_json(const radicle_lod* lod, const char* world, int32_t level,
                                           uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out bytes and size are required");
  }
  out_bytes[0] = nullptr;
  out_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  if (world == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "world name is required");
  return guard_exceptions("lod candidates failed", [&]() -> radicle_status {
    for (const auto& candidate : fo4::lod::list_lod_worlds(*lod->environment, *lod->assets)) {
      if (!fo4::text::equals_folded(candidate.name, world)) continue;
      // Constraint: the candidate listing never culls, so an empty LAND set skips decoding the world's LAND.
      const auto collection = fo4::lod::collect_object_lod_candidates(
          *lod->environment, candidate.key, level, std::make_shared<const fo4::lod::LandGrids>());
      std::string json{"{\"world\":" + json_quote(candidate.name)};
      json += ",\"level\":" + std::to_string(level);
      json += ",\"tiles\":" +
              std::to_string(fo4::lod::object_tile_origins(candidate.settings, level, collection).size());
      json += ",\"counts\":{\"referencesSeen\":" + std::to_string(collection.counts.references_seen);
      json += ",\"included\":" + std::to_string(collection.counts.included);
      json += ",\"skipped\":{";
      for (std::size_t index = 0; index < collection.counts.skipped.size(); ++index) {
        if (index != 0) json.push_back(',');
        const auto reason = static_cast<fo4::lod::LodCandidateSkip>(index);
        json += json_quote(fo4::lod::lod_candidate_skip_name(reason));
        json += ':' + std::to_string(collection.counts.skipped[index]);
      }
      json += "}},\"candidates\":[";
      for (std::size_t index = 0; index < collection.candidates.size(); ++index) {
        const auto& entry = collection.candidates[index];
        if (index != 0) json.push_back(',');
        json += "{\"reference\":" + json_form_key(entry.reference);
        json += ",\"base\":" + json_form_key(entry.base);
        json += ",\"position\":[" + json_numbers(entry.position) + "]";
        json += ",\"rotation\":[" + json_numbers(entry.rotation) + "]";
        json += ",\"scale\":" + json_number(entry.scale);
        json += ",\"mesh\":" + json_quote(entry.mesh);
        json += ",\"cellX\":" + std::to_string(entry.cell_x);
        json += ",\"cellY\":" + std::to_string(entry.cell_y);
        json += ",\"class\":" + json_quote(fo4::lod::lod_candidate_class_name(entry.class_tag));
        json += '}';
      }
      json += "]}";
      return copy_out(to_payload(json), out_bytes, out_size, k_payload_messages);
    }
    return fail(RADICLE_STATUS_NOT_FOUND, "LOD settings for that world are not loaded");
  });
}

namespace {

// Constraint: the cancel flag is written by the caller's thread, so it is read atomically, not through a plain load.
bool scope_cancelled(const radicle_lod_scope* scope) noexcept {
  if (scope == nullptr || scope->cancel == nullptr) return false;
  return std::atomic_ref<std::int32_t>(*scope->cancel).load(std::memory_order_relaxed) != 0;
}

radicle_status generate_report(const radicle_lod* lod, const char* world, const char* ledger_text, int32_t apply,
                               const radicle_lod_scope* scope, uint8_t** out_report, size_t* out_report_size,
                               uint8_t** out_ledger, size_t* out_ledger_size) {
  clear_error();
  if (out_report == nullptr || out_report_size == nullptr || out_ledger == nullptr ||
      out_ledger_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "report and ledger outputs are required");
  }
  out_report[0] = nullptr;
  out_report_size[0] = 0;
  out_ledger[0] = nullptr;
  out_ledger_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  // Constraint: only a write needs a data folder; a dry run reports the plan without one.
  if (apply != 0 && lod->data_dir.empty()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a data folder is required to write LOD tiles");
  }
  return guard_exceptions("lod generate failed", [&]() -> radicle_status {
    const auto ledger =
        fo4::lod::parse_lod_ledger(ledger_text == nullptr ? std::string_view{} : std::string_view{ledger_text});
    auto merged = ledger;
    fo4::lod::LodGenerateTotals totals;
    std::size_t unreadable = 0;
    auto worlds = fo4::lod::list_lod_worlds(*lod->environment, *lod->assets, &unreadable);
    // Invariant: a scoped run bakes one world, so every other world's tiles are left as they are.
    if (world != nullptr) {
      std::erase_if(worlds, [&](const fo4::lod::LodWorld& listed) {
        return !fo4::text::equals_folded(listed.name, world);
      });
    }
    // Constraint: a level bound of 0 means the world's own range, so a zeroed scope scopes nothing away.
    std::int32_t level_min = fo4::lod::k_lod_level_min;
    std::int32_t level_max = fo4::lod::k_lod_level_max;
    if (scope != nullptr) {
      const auto bad_bound = [](std::int32_t level) {
        return level != 0 && (level < fo4::lod::k_lod_level_min || level > fo4::lod::k_lod_level_max);
      };
      // Constraint: an out-of-range bound or an inverted range is a caller error, so it is refused, not clamped.
      if (bad_bound(scope->level_min) || bad_bound(scope->level_max)) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a level bound must be 0 or 4 to 32");
      }
      if (scope->level_min > 0) level_min = scope->level_min;
      if (scope->level_max > 0) level_max = scope->level_max;
      if (level_min > level_max) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the level minimum is above the level maximum");
      }
    }
    fo4::lod::LodGenerateProgress progress;
    if (scope != nullptr) {
      progress.report = scope->progress;
      progress.user = scope->user;
      progress.cancel = scope->cancel;
    }
    // Invariant: an applied run must write the lodsettings a landed mod world has no file for, or it is never baked.
    std::string settings_rows;
    std::size_t settings_written = 0;
    // Invariant: a derived world bakes from the terrain the derive step already collected.
    std::map<std::string, std::shared_ptr<const fo4::lod::WorldTerrain>> derived_terrain;
    // Invariant: a cancelled run derives no settings, because that world's cells were never read.
    const bool derive_settings = apply != 0 && !scope_cancelled(scope);
    if (apply != 0 && !derive_settings) totals.cancelled = true;
    if (derive_settings) {
      for (const auto& candidate : fo4::lod::list_lod_worlds_without_settings(*lod->environment, *lod->assets)) {
        // Constraint: a scoped run leaves every other world's settings alone, because its cells were never read.
        if (world != nullptr && !fo4::text::equals_folded(candidate.name, world)) continue;
        auto terrain = fo4::lod::collect_world_terrain(*lod->environment, candidate.key);
        if (terrain.land.grids.empty()) continue;
        std::int32_t minimum_x = 0;
        std::int32_t minimum_y = 0;
        std::int32_t maximum_x = 0;
        std::int32_t maximum_y = 0;
        bool first = true;
        for (const auto& grid : terrain.land.grids) {
          const auto cell_x = grid.first.first;
          const auto cell_y = grid.first.second;
          minimum_x = first ? cell_x : std::min(minimum_x, cell_x);
          minimum_y = first ? cell_y : std::min(minimum_y, cell_y);
          maximum_x = first ? cell_x : std::max(maximum_x, cell_x);
          maximum_y = first ? cell_y : std::max(maximum_y, cell_y);
          first = false;
        }
        const auto derived = fo4::lod::derive_lod_settings(minimum_x, minimum_y, maximum_x, maximum_y);
        std::string error;
        std::string state{"failed"};
        std::size_t bytes = 0;
        if (!derived.has_value()) {
          error = "the world's cell bounds are not a grid this generator can use";
        } else {
          try {
            const auto payload = fo4::lod::serialize_lod_settings(*derived);
            const auto target = lod->data_dir / "LODSettings" / (candidate.name + ".lod");
            if (fo4::lod::write_payload_at(target, payload, bytes, error)) state = "written";
          } catch (const std::exception& failure) {
            error = failure.what();
          }
        }
        if (!settings_rows.empty()) settings_rows.push_back(',');
        settings_rows += "{\"world\":" + json_quote(candidate.name);
        if (derived.has_value()) {
          settings_rows += ",\"southwestX\":" + std::to_string(derived->southwest_x);
          settings_rows += ",\"southwestY\":" + std::to_string(derived->southwest_y);
          settings_rows += ",\"stride\":" + std::to_string(derived->stride);
          settings_rows += ",\"levelMin\":" + std::to_string(derived->level_min);
          settings_rows += ",\"levelMax\":" + std::to_string(derived->level_max);
        }
        settings_rows += ",\"status\":" + json_quote(state);
        settings_rows += ",\"bytes\":" + std::to_string(bytes);
        if (!error.empty()) settings_rows += ",\"error\":" + json_quote(error);
        settings_rows += '}';
        if (state != "written") continue;
        ++settings_written;
        // Invariant: the derived grid must bake in this same run, so the user never gets a file and no tiles.
        worlds.push_back(fo4::lod::LodWorld{candidate.key, candidate.name, *derived});
        derived_terrain.emplace(candidate.name,
                                std::make_shared<const fo4::lod::WorldTerrain>(std::move(terrain)));
      }
    }
    // Invariant: one call parses each loose mesh once, not once per level and world.
    fo4::lod::ObjectLooseMeshCache loose_meshes;
    for (const auto& candidate : worlds) {
      // Invariant: a cancel stops the world loop, so a later world is never opened for a run that already stopped.
      if (scope_cancelled(scope)) {
        totals.cancelled = true;
        break;
      }
      std::shared_ptr<const fo4::lod::WorldTerrain> terrain;
      const auto collected = derived_terrain.find(candidate.name);
      terrain = collected != derived_terrain.end()
                    ? collected->second
                    : std::make_shared<const fo4::lod::WorldTerrain>(
                          fo4::lod::collect_world_terrain(*lod->environment, candidate.key));
      const auto shared = tile_inputs(*lod, candidate);
      const auto extras = loose_inputs_by_tile(*lod, candidate, level_min, level_max, loose_meshes);
      fo4::lod::LodGenerateWorld context{*lod->environment, *lod->assets, lod->data_dir,
                                         candidate,         shared,       apply != 0, &extras};
      context.level_min = level_min;
      context.level_max = level_max;
      context.progress = scope == nullptr ? nullptr : &progress;
      fo4::lod::generate_terrain_stage(context, *terrain, merged, totals);
      fo4::lod::generate_object_stage(context, terrain, merged, totals);
    }
    if (world != nullptr && worlds.empty()) {
      return fail(RADICLE_STATUS_NOT_FOUND, "no LOD settings for that worldspace");
    }
    std::string json{"{\"apply\":"};
    json += apply == 0 ? "false" : "true";
    // Invariant: a scoped report must name the world it baked, so a partial bake is never read as the whole folder.
    if (world != nullptr) json += ",\"world\":" + json_quote(worlds.front().name);
    append_count(json, "written", totals.written);
    append_count(json, "wouldWrite", totals.would_write);
    append_path_list(json, "wouldOverwrite", totals.would_overwrite);
    append_path_list(json, "wouldDelete", totals.would_delete);
    append_count(json, "unsupportedCandidates", totals.unsupported);
    append_count(json, "empty", totals.empty);
    // Invariant: a partial level scope bakes no object tile, so the report names why instead of a zero count.
    if (!totals.objects_skipped.empty()) json += ",\"objectsSkipped\":" + json_quote(totals.objects_skipped);
    append_count(json, "texturesWritten", totals.textures_written);
    append_count(json, "texturesSkipped", totals.textures_skipped);
    append_count(json, "terrainTexturesWritten", totals.terrain_textures_written);
    append_count(json, "terrainTexturesSkipped", totals.terrain_textures_skipped);
    append_count(json, "terrainTexturesEmpty", totals.terrain_textures_empty);
    // Invariant: lost inputs are counted, so a run that wrote tiles with holes never reads as a clean run.
    append_count(json, "objectMissingMeshes", totals.object_missing_meshes);
    append_count(json, "objectDroppedTriangles", totals.object_dropped_triangles);
    append_count(json, "atlasEntries", totals.atlas_entries);
    append_count(json, "atlasMissingTextures", totals.atlas_missing_textures);
    append_count(json, "atlasClampedShapes", totals.atlas_clamped_shapes);
    append_count(json, "failed", totals.failed);
    append_count(json, "unreadableWorlds", unreadable);
    // Invariant: a written lodsettings carries its own count and rows, so a world that gained a grid is never silent.
    append_count(json, "lodSettingsWritten", settings_written);
    if (!settings_rows.empty()) json += ",\"lodSettings\":[" + settings_rows + "]";
    // Invariant: a cancelled run says so, and only when a stage saw the flag, so a late set still reads finished.
    if (totals.cancelled) json += ",\"cancelled\":true";
    // Invariant: a scoped report echoes its level range, so a partial bake is never mistaken for every level.
    if (scope != nullptr) {
      json += ",\"scope\":{\"levelMin\":" + std::to_string(level_min);
      json += ",\"levelMax\":" + std::to_string(level_max) + '}';
    }
    // Constraint: a reason nobody dropped for is omitted, so the breakdown only lists counts that happened.
    std::string drops;
    for (std::size_t reason = 0; reason < totals.object_drops.size(); ++reason) {
      if (totals.object_drops.at(reason) == 0) continue;
      if (!drops.empty()) drops.push_back(',');
      drops += json_quote(fo4::lod::lod_candidate_skip_name(static_cast<fo4::lod::LodCandidateSkip>(reason)));
      drops += ':' + std::to_string(totals.object_drops.at(reason));
    }
    if (!drops.empty()) json += ",\"objectDrops\":{" + drops + '}';
    if (!totals.terrain_loss.empty()) json += ",\"terrainLoss\":{" + totals.terrain_loss + '}';
    json += ",\"tiles\":[" + totals.tiles + "]";
    json += ",\"textures\":[" + totals.textures + "]}";
    const auto report_payload = to_payload(json);
    const auto ledger_payload = to_payload(fo4::lod::format_lod_ledger(merged));
    const auto status = copy_out(report_payload, out_report, out_report_size, k_payload_messages);
    if (status != RADICLE_STATUS_OK) return status;
    const auto ledger_status = copy_out(ledger_payload, out_ledger, out_ledger_size, k_payload_messages);
    if (ledger_status != RADICLE_STATUS_OK) {
      release_payload(out_report[0]);
      out_report[0] = nullptr;
      out_report_size[0] = 0;
    }
    return ledger_status;
  });
}

}  // namespace

radicle_status radicle_lod_generate_json(const radicle_lod* lod, const char* ledger_text, int32_t apply,
                                         uint8_t** out_report, size_t* out_report_size, uint8_t** out_ledger,
                                         size_t* out_ledger_size) {
  return generate_report(lod, nullptr, ledger_text, apply, nullptr, out_report, out_report_size, out_ledger,
                         out_ledger_size);
}

radicle_status radicle_lod_generate_world_json(const radicle_lod* lod, const char* world, const char* ledger_text,
                                               int32_t apply, uint8_t** out_report, size_t* out_report_size,
                                               uint8_t** out_ledger, size_t* out_ledger_size) {
  if (world == nullptr || *world == '\0') {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "world name is required");
  }
  return generate_report(lod, world, ledger_text, apply, nullptr, out_report, out_report_size, out_ledger,
                         out_ledger_size);
}

radicle_status radicle_lod_generate_scoped_json(const radicle_lod* lod, const char* ledger_text, int32_t apply,
                                                const char* world, const radicle_lod_scope* scope,
                                                uint8_t** out_report, size_t* out_report_size, uint8_t** out_ledger,
                                                size_t* out_ledger_size) {
  if (world != nullptr && *world == '\0') {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "world name is required");
  }
  return generate_report(lod, world, ledger_text, apply, scope, out_report, out_report_size, out_ledger,
                         out_ledger_size);
}

namespace {

radicle_status verify_report(const radicle_lod* lod, const char* scope, const char* ledger_text,
                             uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (out_bytes == nullptr || out_size == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out bytes and size are required");
  }
  out_bytes[0] = nullptr;
  out_size[0] = 0;
  if (lod == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "lod session is required");
  return guard_exceptions("lod verify failed", [&]() -> radicle_status {
    const auto ledger =
        fo4::lod::parse_lod_ledger(ledger_text == nullptr ? std::string_view{} : std::string_view{ledger_text});
    const bool check_disk = !lod->data_dir.empty();
    auto worlds = fo4::lod::list_lod_worlds(*lod->environment, *lod->assets);
    if (scope != nullptr) {
      const auto match = std::find_if(worlds.begin(), worlds.end(), [&](const fo4::lod::LodWorld& entry) {
        return fo4::text::equals_folded(entry.name, scope);
      });
      if (match == worlds.end()) return fail(RADICLE_STATUS_NOT_FOUND, "no LOD settings for that worldspace");
      const auto keep = *match;
      worlds.assign(1, keep);
    }
    // Constraint: verify only needs tile origins, so an empty LAND set keeps each level from decoding the world's LAND.
    const auto no_land = std::make_shared<const fo4::lod::LandGrids>();
    std::map<std::string, fo4::lod::LodTile> tiles;
    std::map<std::string, bool> archived;
    std::map<std::string, std::uintmax_t> on_disk;
    // Invariant: inputs are recomputed the same way generate commits them, so "current" means current.
    std::map<std::string, std::vector<fo4::lod::LodInput>> inputs_by_world;
    std::map<std::string, std::vector<fo4::lod::LodInput>> world_inputs;
    std::map<std::string, std::vector<fo4::lod::LodTile>> terrain_by_world;
    std::set<std::string> object_worlds;
    fo4::lod::ObjectLooseMeshCache loose_meshes;
    for (const auto& world : worlds) {
      // Invariant: verify only lists tiles from the terrain, so land is collected as presence and no height grid is kept.
      const auto terrain =
          fo4::lod::collect_world_terrain(*lod->environment, world.key, fo4::lod::LandDetail::presence);
      const auto shared = tile_inputs(*lod, world);
      const auto extras = loose_inputs_by_tile(*lod, world, fo4::lod::k_lod_level_min, fo4::lod::k_lod_level_max,
                                              loose_meshes);
      world_inputs[world.name] = shared;
      std::vector<fo4::lod::LodTile> listed = terrain_tiles(world, terrain);
      // Constraint: atlas rows must use the writer's land_cells rule, so verify asks only for a written DDS.
      const auto cells = fo4::lod::build_terrain_cell_index(*lod->environment, world.key);
      std::vector<fo4::lod::LodTile> with_land;
      for (const auto& tile : listed) {
        if (fo4::lod::terrain_tile_has_land(cells, *lod->environment, tile.level, tile.x, tile.y)) {
          with_land.push_back(tile);
        }
      }
      terrain_by_world[world.name] = std::move(with_land);
      // Invariant: written object tiles carry ledger rows, so verify must plan them to judge currency.
      for_each_object_level(world.settings, [&](std::int32_t level) {
        const auto candidates = fo4::lod::collect_object_lod_candidates(*lod->environment, world.key, level, no_land);
        auto origins = object_tiles(world, level, candidates);
        // Constraint: a world whose object tiles are all recorded empty has no atlas to judge.
        for (const auto& origin : origins) {
          const auto empty_record = ledger.hashes.find(fo4::lod::object_tile_empty_ledger_key(origin));
          const auto origin_inputs = tile_input_list(shared, extras, fo4::lod::lod_ledger_key(origin));
          if (empty_record == ledger.hashes.end() ||
              empty_record->second != fo4::lod::lod_tile_inputs_hash(origin, origin_inputs)) {
            object_worlds.insert(world.name);
            break;
          }
        }
        listed.insert(listed.end(), origins.begin(), origins.end());
      });
      const auto existing = archive_tiles(*lod, world.name, on_disk);
      // Constraint: only a file found in the data folder or an archive counts as inArchive.
      for (const auto& tile : existing) archived[fo4::lod::lod_ledger_key(tile)] = true;
      listed.insert(listed.end(), existing.begin(), existing.end());
      for (auto tile : listed) {
        const auto key = fo4::lod::lod_ledger_key(tile);
        inputs_by_world.emplace(key, tile_input_list(shared, extras, key));
        tiles.emplace(key, std::move(tile));
      }
    }
    for (const auto& entry : ledger.hashes) {
      if (tiles.count(entry.first) != 0) continue;
      auto tile = fo4::lod::parse_lod_ledger_key(entry.first);
      if (tile.has_value()) tiles.emplace(entry.first, std::move(*tile));
    }
    std::size_t missing = 0;
    std::size_t partial = 0;
    std::size_t ok = 0;
    std::size_t stale = 0;
    std::size_t unverified = 0;
    std::size_t empty_tiles = 0;
    std::string rows;
    for (const auto& entry : tiles) {
      const auto& tile = entry.second;
      const auto recorded = ledger.hashes.find(entry.first);
      const auto found_inputs = inputs_by_world.find(entry.first);
      std::span<const fo4::lod::LodInput> inputs{};
      if (found_inputs != inputs_by_world.end()) inputs = found_inputs->second;
      const bool current = recorded != ledger.hashes.end() &&
                           recorded->second == fo4::lod::lod_tile_inputs_hash(tile, inputs);
      std::error_code status;
      std::uintmax_t size = 0;
      bool present = false;
      if (check_disk) {
        const auto listed = on_disk.find(entry.first);
        if (listed != on_disk.end()) {
          size = listed->second;
          present = true;
        } else {
          size = std::filesystem::file_size(fo4::lod::lod_tile_local_path(lod->data_dir, tile), status);
          present = !status;
        }
      }
      // Constraint: a tile is archive-only only when no loose file of that name exists in the data folder.
      const bool loose = check_disk && std::filesystem::exists(fo4::lod::lod_tile_local_path(lod->data_dir, tile));
      std::string local{"unknown"};
      if (present) local = size < k_min_tile_bytes ? "partial" : "present";
      // Invariant: generate records a tile that built no shape, so its absent file is expected, not missing.
      bool known_empty = false;
      if (tile.kind == fo4::lod::LodTileKind::object && found_inputs != inputs_by_world.end()) {
        const auto empty_record = ledger.hashes.find(fo4::lod::object_tile_empty_ledger_key(tile));
        known_empty = empty_record != ledger.hashes.end() &&
                      empty_record->second == fo4::lod::lod_tile_inputs_hash(tile, inputs);
      }
      std::string state;
      if (!check_disk) {
        state = current ? "ok" : (known_empty ? "empty" : "stale");
        local = "no-data-dir";
        ++unverified;
      } else if (!present && known_empty) {
        state = "empty";
        ++empty_tiles;
      } else if (!present) {
        state = "missing";
        ++missing;
      } else if (size < k_min_tile_bytes) {
        state = "partial";
        ++partial;
      } else if (current) {
        state = "ok";
        ++ok;
        // Invariant: a tile only the archives carry and no ledger row names was never written here, so it is shipped.
      } else if (recorded == ledger.hashes.end() && archived.count(entry.first) != 0 && !loose) {
        state = "shipped";
        ++ok;
      } else {
        state = "stale";
        ++stale;
      }
      if (!rows.empty()) rows.push_back(',');
      rows += "{\"world\":" + json_quote(tile.worldspace);
      rows += ",\"kind\":" + json_quote(fo4::lod::lod_tile_kind_name(tile.kind));
      rows += ",\"level\":" + std::to_string(tile.level);
      rows += ",\"x\":" + std::to_string(tile.x);
      rows += ",\"y\":" + std::to_string(tile.y);
      rows += ",\"state\":" + json_quote(state);
      rows += ",\"local\":" + json_quote(local);
      rows += ",\"ledger\":" + json_quote(current ? "current" : "stale");
      rows += ",\"bytes\":" + std::to_string(present ? size : 0);
      rows += ",\"inArchive\":" + std::string{archived.count(entry.first) != 0 ? "true" : "false"};
      rows += '}';
    }
    fo4::lod::ObjectAtlasTextureReport textures;
    for (const auto& world : worlds) {
      if (object_worlds.count(world.name) == 0) continue;
      const auto found_world = world_inputs.find(world.name);
      std::span<const fo4::lod::LodInput> inputs{};
      if (found_world != world_inputs.end()) inputs = found_world->second;
      // Invariant: rows come from the writer's own helpers, so verify cannot disagree about where an atlas lives.
      textures.merge(
          fo4::lod::verify_object_atlas_textures(world.name, inputs, ledger.hashes, lod->data_dir, check_disk));
    }
    // Invariant: terrain atlases are judged through the writer's own path and key, so their rows must match the bake.
    fo4::lod::TerrainAtlasTextureReport terrain_textures;
    for (const auto& entry : terrain_by_world) {
      const auto found_world = world_inputs.find(entry.first);
      std::span<const fo4::lod::LodInput> inputs{};
      if (found_world != world_inputs.end()) inputs = found_world->second;
      const auto reported = fo4::lod::verify_terrain_atlas_textures(entry.first, inputs, entry.second, ledger.hashes,
                                                                   lod->data_dir, check_disk);
      terrain_textures.ok += reported.ok;
      terrain_textures.missing += reported.missing;
      terrain_textures.stale += reported.stale;
      terrain_textures.unverified += reported.unverified;
      terrain_textures.rows.insert(terrain_textures.rows.end(), reported.rows.begin(), reported.rows.end());
    }
    std::string texture_rows;
    for (const auto& row : textures.rows) {
      if (!texture_rows.empty()) texture_rows.push_back(',');
      texture_rows += "{\"world\":" + json_quote(row.world);
      texture_rows += ",\"kind\":\"texture\",\"map\":" + json_quote(fo4::lod::object_atlas_map_name(row.map));
      texture_rows += ",\"state\":" + json_quote(fo4::lod::object_atlas_texture_state_name(row.state));
      texture_rows += ",\"ledger\":" + json_quote(row.current ? "current" : "stale");
      texture_rows += ",\"bytes\":" + std::to_string(row.bytes);
      texture_rows += '}';
    }
    for (const auto& row : terrain_textures.rows) {
      if (!texture_rows.empty()) texture_rows.push_back(',');
      texture_rows += "{\"world\":" + json_quote(row.world);
      texture_rows += ",\"kind\":\"texture\",\"map\":" + json_quote(fo4::lod::terrain_texture_map_name(row.map));
      texture_rows += ",\"level\":" + std::to_string(row.level);
      texture_rows += ",\"x\":" + std::to_string(row.x);
      texture_rows += ",\"y\":" + std::to_string(row.y);
      texture_rows += ",\"state\":" + json_quote(fo4::lod::object_atlas_texture_state_name(row.state));
      texture_rows += ",\"ledger\":" + json_quote(row.current ? "current" : "stale");
      texture_rows += ",\"bytes\":" + std::to_string(row.bytes);
      texture_rows += '}';
    }
    std::string json{"{"};
    // Invariant: a scoped verify must name the world it judged, so its counts cannot read as the whole folder.
    if (scope != nullptr) json += "\"world\":" + json_quote(worlds.front().name) + ',';
    json += "\"missing\":" + std::to_string(missing);
    json += ",\"partial\":" + std::to_string(partial);
    json += ",\"ok\":" + std::to_string(ok);
    json += ",\"stale\":" + std::to_string(stale);
    json += ",\"unverified\":" + std::to_string(unverified);
    json += ",\"empty\":" + std::to_string(empty_tiles);
    json += ",\"tiles\":[" + rows + "]";
    json += ",\"texturesOk\":" + std::to_string(textures.ok + terrain_textures.ok);
    json += ",\"texturesMissing\":" + std::to_string(textures.missing + terrain_textures.missing);
    json += ",\"texturesStale\":" + std::to_string(textures.stale + terrain_textures.stale);
    json += ",\"texturesUnverified\":" + std::to_string(textures.unverified + terrain_textures.unverified);
    json += ",\"textures\":[" + texture_rows + "]";
    json += "}";
    const auto report_status = copy_out(to_payload(json), out_bytes, out_size, k_payload_messages);
    if (report_status != RADICLE_STATUS_OK) return report_status;
    // Invariant: a texture failure still returns the report, so the caller can name the offending atlas.
    if (!textures.failure.empty()) {
      return fail(textures.failure_missing ? RADICLE_STATUS_NOT_FOUND : RADICLE_STATUS_PARSE_ERROR,
                  textures.failure.c_str());
    }
    return RADICLE_STATUS_OK;
  });
}

}  // namespace

radicle_status radicle_lod_verify_json(const radicle_lod* lod, const char* ledger_text, uint8_t** out_bytes,
                                       size_t* out_size) {
  return verify_report(lod, nullptr, ledger_text, out_bytes, out_size);
}

radicle_status radicle_lod_verify_world_json(const radicle_lod* lod, const char* world, const char* ledger_text,
                                             uint8_t** out_bytes, size_t* out_size) {
  if (world == nullptr || *world == '\0') {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "world name is required");
  }
  return verify_report(lod, world, ledger_text, out_bytes, out_size);
}
