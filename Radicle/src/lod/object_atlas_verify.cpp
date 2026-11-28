#include "fo4/lod/object_atlas_verify.hpp"

#include <array>
#include <fstream>
#include <optional>
#include <system_error>

namespace fo4::lod {
namespace {

// Constraint: a BC3 DDS is 128 header plus whole 4x4 blocks, so anything smaller cannot be the written atlas.
constexpr std::uintmax_t k_min_atlas_bytes = 148;

std::optional<std::vector<std::byte>>
read_bytes(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return std::nullopt;
  std::vector<std::byte> bytes;
  std::array<char, 1U << 16> buffer{};
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = static_cast<std::size_t>(in.gcount());
    for (std::size_t index = 0; index < read; ++index) {
      bytes.push_back(
          static_cast<std::byte>(static_cast<unsigned char>(buffer[index])));
    }
  }
  return bytes;
}

} // namespace

std::string_view
object_atlas_texture_state_name(ObjectAtlasTextureState state) {
  switch (state) {
  case ObjectAtlasTextureState::ok:
    return "ok";
  case ObjectAtlasTextureState::missing:
    return "missing";
  case ObjectAtlasTextureState::stale:
    return "stale";
  case ObjectAtlasTextureState::unverified:
    return "unverified";
  }
  return "unverified";
}

ObjectAtlasTextureReport verify_object_atlas_textures(
    std::string_view worldspace, std::span<const LodInput> inputs,
    const std::map<std::string, std::uint64_t> &ledger,
    const std::filesystem::path &data_dir, bool check_disk) {
  ObjectAtlasTextureReport report;
  for (std::size_t index = 0; index < k_object_atlas_maps; ++index) {
    const auto map = static_cast<ObjectAtlasMap>(index);
    ObjectAtlasTextureRow row{};
    row.world = std::string{worldspace};
    row.map = map;
    const auto recorded = ledger.find(object_atlas_ledger_key(worldspace, map));
    row.current =
        recorded != ledger.end() &&
        recorded->second == object_atlas_inputs_hash(worldspace, map, inputs);
    if (!check_disk) {
      // Constraint: without a data folder there is no atlas file to judge, so it is unverified, not missing.
      row.state = row.current ? ObjectAtlasTextureState::ok
                              : ObjectAtlasTextureState::stale;
      ++report.unverified;
    } else {
      const auto target = object_atlas_local_path(data_dir, worldspace, map);
      std::error_code status;
      row.bytes = std::filesystem::file_size(target, status);
      if (status) {
        row.state = ObjectAtlasTextureState::missing;
        ++report.missing;
        // Constraint: an atlas the ledger never recorded was never generated, so it is missing but not a failure.
        if (report.failure.empty() && recorded != ledger.end()) {
          report.failure_missing = true;
          report.failure =
              "LOD atlas texture is missing: " + std::string{worldspace} + " " +
              std::string{object_atlas_map_name(map)};
        }
      } else {
        const auto bytes = read_bytes(target);
        // Invariant: anything but the square BC3 DDS the writer produces is stale, even when the ledger agrees.
        if (!bytes.has_value() || row.bytes < k_min_atlas_bytes ||
            !object_atlas_dds_well_formed(*bytes) || !row.current) {
          row.state = ObjectAtlasTextureState::stale;
          ++report.stale;
          if (report.failure.empty()) {
            report.failure =
                "LOD atlas texture is stale: " + std::string{worldspace} + " " +
                std::string{object_atlas_map_name(map)};
          }
        } else {
          row.state = ObjectAtlasTextureState::ok;
          ++report.ok;
        }
      }
    }
    report.rows.push_back(row);
  }
  return report;
}

TerrainAtlasTextureReport verify_terrain_atlas_textures(
    std::string_view worldspace, std::span<const LodInput> inputs,
    std::span<const LodTile> tiles,
    const std::map<std::string, std::uint64_t> &ledger,
    const std::filesystem::path &data_dir, bool check_disk) {
  TerrainAtlasTextureReport report;
  for (const auto &tile : tiles) {
    const auto hash = lod_texture_inputs_hash(tile, inputs);
    for (const auto map : {TerrainTextureMap::diffuse, TerrainTextureMap::normal}) {
      TerrainAtlasTextureRow row{};
      row.world = std::string{worldspace};
      row.level = tile.level;
      row.x = tile.x;
      row.y = tile.y;
      row.map = map;
      // Invariant: the ledger key is the terrain writer's, so a rebuilt atlas and a verified one cannot disagree.
      const auto recorded = ledger.find(terrain_texture_ledger_key(
          worldspace, tile.level, tile.x, tile.y, map));
      row.current = recorded != ledger.end() && recorded->second == hash;
      if (!check_disk) {
        // Constraint: without a data folder there is no atlas file to judge, so it is unverified, not missing.
        row.state = row.current ? ObjectAtlasTextureState::ok
                                : ObjectAtlasTextureState::stale;
        ++report.unverified;
      } else {
        const auto target = terrain_texture_local_path(
            data_dir, worldspace, tile.level, tile.x, tile.y, map);
        std::error_code status;
        row.bytes = std::filesystem::file_size(target, status);
        if (status) {
          row.state = ObjectAtlasTextureState::missing;
          ++report.missing;
        } else {
          const auto bytes = read_bytes(target);
          // Invariant: anything but the square BC3 chain the DDS writer emits is stale, even when the ledger agrees.
          if (!bytes.has_value() || row.bytes < k_min_atlas_bytes ||
              !object_atlas_dds_well_formed(*bytes) || !row.current) {
            row.state = ObjectAtlasTextureState::stale;
            ++report.stale;
          } else {
            row.state = ObjectAtlasTextureState::ok;
            ++report.ok;
          }
        }
      }
      report.rows.push_back(row);
    }
  }
  return report;
}

void ObjectAtlasTextureReport::merge(const ObjectAtlasTextureReport &other) {
  ok += other.ok;
  missing += other.missing;
  stale += other.stale;
  unverified += other.unverified;
  rows.insert(rows.end(), other.rows.begin(), other.rows.end());
  if (failure.empty() && !other.failure.empty()) {
    failure = other.failure;
    failure_missing = other.failure_missing;
  }
}

} // namespace fo4::lod
