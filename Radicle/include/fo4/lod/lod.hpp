#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fo4::lod {

class LodError : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

inline constexpr std::size_t k_lod_settings_size = 16;
inline constexpr std::int32_t k_lod_level_min = 4;
inline constexpr std::int32_t k_lod_level_max = 32;

struct LodSettings {
  std::int16_t southwest_x{0};
  std::int16_t southwest_y{0};
  std::int32_t stride{0};
  std::int32_t level_min{k_lod_level_min};
  std::int32_t level_max{k_lod_level_max};

  friend bool operator==(const LodSettings&, const LodSettings&) = default;
};

LodSettings parse_lod_settings(std::span<const std::byte> bytes);

std::array<std::byte, k_lod_settings_size> serialize_lod_settings(const LodSettings& settings);

bool lod_settings_encodable(const LodSettings& settings) noexcept;

std::string lod_settings_path(std::string_view worldspace);

std::optional<std::string> lod_settings_worldspace(std::string_view path);

enum class LodTileKind : std::uint8_t { object, terrain };

std::string_view lod_tile_kind_name(LodTileKind kind);

struct LodTile {
  std::string worldspace;
  std::int32_t level{0};
  std::int32_t x{0};
  std::int32_t y{0};
  LodTileKind kind{LodTileKind::object};

  friend bool operator==(const LodTile&, const LodTile&) = default;
};

std::optional<LodTile> parse_lod_tile_path(std::string_view path);

std::string format_lod_tile_path(const LodTile& tile);

bool lod_tile_path_follows_convention(std::string_view path, const LodTile& tile);

enum class LodTilePlacement : std::uint8_t {
  ok,
  level_out_of_range,
  level_not_power_of_two,
  not_block_aligned,
};

std::string_view lod_tile_placement_name(LodTilePlacement placement);

LodTilePlacement check_lod_tile_placement(const LodSettings& settings, const LodTile& tile);

enum class LodTileStructure : std::uint8_t {
  ok,
  root_count,
  root_mismatch,
  shape_type_mismatch,
  no_shapes,
  missing_bound_chain,
  bound_reference_mismatch,
};

std::string_view lod_tile_structure_name(LodTileStructure structure);

LodTileStructure check_lod_tile_structure(const nif::NifFile& nif, LodTileKind kind);

}  // namespace fo4::lod
