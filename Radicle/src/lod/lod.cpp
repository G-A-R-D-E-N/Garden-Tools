#include "fo4/lod/lod.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace fo4::lod {
namespace {

constexpr std::string_view k_settings_dir = "lodsettings\\";
constexpr std::string_view k_settings_extension = ".lod";
constexpr std::string_view k_tile_root = "Meshes\\Terrain\\";
constexpr std::array<std::string_view, 4> k_shape_types{
    "BSTriShape", "BSSubIndexTriShape", "BSMeshLODTriShape", "BSDynamicTriShape"};
constexpr std::array<std::string_view, 5> k_older_shape_types{
    "NiTriShape", "NiTriStrips", "NiTriShapeData", "NiTriStripsData", "NiTriBasedGeom"};

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(
                                        std::to_integer<std::uint8_t>(bytes[offset + 1]))
                                    << 8U);
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(read_u16(bytes, offset)) |
         (static_cast<std::uint32_t>(read_u16(bytes, offset + 2)) << 16U);
}

void write_u16(std::span<std::byte> bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = std::byte{static_cast<std::uint8_t>(value & 0xFFU)};
  bytes[offset + 1] = std::byte{static_cast<std::uint8_t>(value >> 8U)};
}

void write_u32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  write_u16(bytes, offset, static_cast<std::uint16_t>(value & 0xFFFFU));
  write_u16(bytes, offset + 2, static_cast<std::uint16_t>(value >> 16U));
}

bool in_list(std::span<const std::string_view> list, std::string_view value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

std::string_view last_component(std::string_view path) {
  const auto separator = path.find_last_of("\\/");
  return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

std::string_view extension_of(std::string_view text) {
  const auto dot = text.find_last_of('.');
  return dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
}

bool iequals(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    const auto lower = [](char value) {
      return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    };
    if (lower(left[i]) != lower(right[i])) return false;
  }
  return true;
}

std::optional<std::int32_t> parse_i32(std::string_view text) {
  if (text.empty()) return std::nullopt;
  std::int32_t value = 0;
  const auto* first = text.data();
  const auto* last = first + text.size();
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc{} || result.ptr != last) return std::nullopt;
  return value;
}

std::string normalized_path(std::string_view path) {
  std::string out{path};
  std::replace(out.begin(), out.end(), '/', '\\');
  return out;
}

bool is_power_of_two(std::int32_t value) {
  return value > 0 && (static_cast<std::uint32_t>(value) & (static_cast<std::uint32_t>(value) - 1U)) == 0U;
}

bool shape_type(std::string_view type) {
  return in_list(std::span<const std::string_view>{k_shape_types}, type);
}

}  // namespace

LodSettings parse_lod_settings(std::span<const std::byte> bytes) {
  if (bytes.size() != k_lod_settings_size) {
    throw LodError("a Fallout 4 lodsettings record is 16 bytes, found " +
                   std::to_string(bytes.size()));
  }
  LodSettings settings;
  settings.southwest_x = static_cast<std::int16_t>(read_u16(bytes, 0));
  settings.southwest_y = static_cast<std::int16_t>(read_u16(bytes, 2));
  settings.stride = static_cast<std::int32_t>(read_u32(bytes, 4));
  settings.level_min = static_cast<std::int32_t>(read_u32(bytes, 8));
  settings.level_max = static_cast<std::int32_t>(read_u32(bytes, 12));
  if (!lod_settings_encodable(settings)) {
    throw LodError("lodsettings stride or level range is invalid: stride " +
                   std::to_string(settings.stride) + ", levels " +
                   std::to_string(settings.level_min) + ".." +
                   std::to_string(settings.level_max));
  }
  return settings;
}

bool lod_settings_encodable(const LodSettings& settings) noexcept {
  return settings.stride > 0 && settings.level_min > 0 &&
         settings.level_max >= settings.level_min;
}

std::array<std::byte, k_lod_settings_size> serialize_lod_settings(const LodSettings& settings) {
  if (!lod_settings_encodable(settings)) {
    throw LodError("lodsettings stride or level range is invalid: stride " +
                   std::to_string(settings.stride) + ", levels " +
                   std::to_string(settings.level_min) + ".." +
                   std::to_string(settings.level_max));
  }
  std::array<std::byte, k_lod_settings_size> bytes{};
  write_u16(bytes, 0, static_cast<std::uint16_t>(settings.southwest_x));
  write_u16(bytes, 2, static_cast<std::uint16_t>(settings.southwest_y));
  write_u32(bytes, 4, static_cast<std::uint32_t>(settings.stride));
  write_u32(bytes, 8, static_cast<std::uint32_t>(settings.level_min));
  write_u32(bytes, 12, static_cast<std::uint32_t>(settings.level_max));
  return bytes;
}

std::string lod_settings_path(std::string_view worldspace) {
  if (worldspace.empty()) throw LodError("a lodsettings path needs a worldspace name");
  return std::string{k_settings_dir} + std::string{worldspace} + std::string{k_settings_extension};
}

std::optional<std::string> lod_settings_worldspace(std::string_view path) {
  const auto file = last_component(path);
  if (!iequals(extension_of(file), "lod")) return std::nullopt;
  const auto dot = file.find_last_of('.');
  if (dot == std::string_view::npos || dot == 0) return std::nullopt;
  return std::string{file.substr(0, dot)};
}

std::string_view lod_tile_kind_name(LodTileKind kind) {
  return kind == LodTileKind::object ? "object" : "terrain";
}

std::optional<LodTile> parse_lod_tile_path(std::string_view path) {
  const auto file = last_component(path);
  const auto extension = extension_of(file);
  LodTile tile;
  if (iequals(extension, "bto")) {
    tile.kind = LodTileKind::object;
  } else if (iequals(extension, "btr")) {
    tile.kind = LodTileKind::terrain;
  } else {
    return std::nullopt;
  }
  const auto stem = file.substr(0, file.size() - extension.size() - 1);
  std::array<std::string_view, 4> parts{};
  std::size_t count = 0;
  std::size_t start = 0;
  while (true) {
    const auto dot = stem.find('.', start);
    if (count + 1 == parts.size()) {
      if (dot != std::string_view::npos) return std::nullopt;
      parts[count++] = stem.substr(start);
      break;
    }
    if (dot == std::string_view::npos) return std::nullopt;
    parts[count++] = stem.substr(start, dot - start);
    start = dot + 1;
  }
  if (parts[0].empty()) return std::nullopt;
  const auto level = parse_i32(parts[1]);
  const auto x = parse_i32(parts[2]);
  const auto y = parse_i32(parts[3]);
  if (!level || !x || !y || *level <= 0) return std::nullopt;
  tile.worldspace = std::string{parts[0]};
  tile.level = *level;
  tile.x = *x;
  tile.y = *y;
  return tile;
}

std::string format_lod_tile_path(const LodTile& tile) {
  if (tile.worldspace.empty()) throw LodError("a LOD tile path needs a worldspace name");
  if (tile.level <= 0) throw LodError("a LOD tile path needs a positive level");
  std::string path{k_tile_root};
  path += tile.worldspace;
  if (tile.kind == LodTileKind::object) path += "\\Objects";
  path += '\\';
  path += tile.worldspace;
  path += '.';
  path += std::to_string(tile.level);
  path += '.';
  path += std::to_string(tile.x);
  path += '.';
  path += std::to_string(tile.y);
  path += tile.kind == LodTileKind::object ? ".BTO" : ".BTR";
  return path;
}

bool lod_tile_path_follows_convention(std::string_view path, const LodTile& tile) {
  return iequals(normalized_path(path), normalized_path(format_lod_tile_path(tile)));
}

std::string_view lod_tile_placement_name(LodTilePlacement placement) {
  switch (placement) {
    case LodTilePlacement::ok:
      return "ok";
    case LodTilePlacement::level_out_of_range:
      return "level_out_of_range";
    case LodTilePlacement::level_not_power_of_two:
      return "level_not_power_of_two";
    case LodTilePlacement::not_block_aligned:
      return "not_block_aligned";
  }
  return "unknown";
}

LodTilePlacement check_lod_tile_placement(const LodSettings& settings, const LodTile& tile) {
  if (tile.level < settings.level_min || tile.level > settings.level_max) {
    return LodTilePlacement::level_out_of_range;
  }
  if (!is_power_of_two(tile.level)) return LodTilePlacement::level_not_power_of_two;
  const auto delta_x = static_cast<std::int64_t>(tile.x) - settings.southwest_x;
  const auto delta_y = static_cast<std::int64_t>(tile.y) - settings.southwest_y;
  if (delta_x % tile.level != 0 || delta_y % tile.level != 0) {
    return LodTilePlacement::not_block_aligned;
  }
  return LodTilePlacement::ok;
}

std::string_view lod_tile_structure_name(LodTileStructure structure) {
  switch (structure) {
    case LodTileStructure::ok:
      return "ok";
    case LodTileStructure::root_count:
      return "root_count";
    case LodTileStructure::root_mismatch:
      return "root_mismatch";
    case LodTileStructure::shape_type_mismatch:
      return "shape_type_mismatch";
    case LodTileStructure::no_shapes:
      return "no_shapes";
    case LodTileStructure::missing_bound_chain:
      return "missing_bound_chain";
    case LodTileStructure::bound_reference_mismatch:
      return "bound_reference_mismatch";
  }
  return "unknown";
}

namespace {

std::optional<std::int32_t> read_node_bound_ref(const nif::NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  try {
    binary::Cursor cursor{std::span<const std::byte>{nif.bytes()}.subspan(info.offset, info.size)};
    cursor.read_i32();
    const auto extra_count = cursor.read_u32();
    if (static_cast<std::size_t>(extra_count) > cursor.remaining() / 4) return std::nullopt;
    cursor.read_bytes(static_cast<std::size_t>(extra_count) * 4);
    cursor.read_i32();
    cursor.read_u32();
    cursor.read_bytes(13 * 4);
    cursor.read_i32();
    const auto child_count = cursor.read_u32();
    if (static_cast<std::size_t>(child_count) > cursor.remaining() / 4) return std::nullopt;
    cursor.read_bytes(static_cast<std::size_t>(child_count) * 4);
    return cursor.read_i32();
  } catch (const parse_error&) {
    return std::nullopt;
  }
}

std::optional<std::int32_t> read_multi_bound_data_ref(const nif::NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  try {
    binary::Cursor cursor{std::span<const std::byte>{nif.bytes()}.subspan(info.offset, info.size)};
    return cursor.read_i32();
  } catch (const parse_error&) {
    return std::nullopt;
  }
}

}  // namespace

LodTileStructure check_lod_tile_structure(const nif::NifFile& nif, LodTileKind kind) {
  if (nif.roots().size() != 1) return LodTileStructure::root_count;
  const auto root = nif.roots().front();
  if (root >= nif.blocks().size()) return LodTileStructure::root_count;
  const auto& root_type = nif.blocks()[root].type;
  if (kind == LodTileKind::object) {
    if (root_type != "NiNode") return LodTileStructure::root_mismatch;
  } else if (root_type != "BSMultiBoundNode") {
    return LodTileStructure::root_mismatch;
  }
  std::size_t shapes = 0;
  for (const auto& block : nif.blocks()) {
    if (in_list(std::span<const std::string_view>{k_older_shape_types}, block.type)) {
      return LodTileStructure::shape_type_mismatch;
    }
    if (!shape_type(block.type)) continue;
    ++shapes;
    if (kind == LodTileKind::object) {
      if (block.type != "BSSubIndexTriShape") return LodTileStructure::shape_type_mismatch;
    } else if (block.type != "BSTriShape" && block.type != "BSSubIndexTriShape") {
      return LodTileStructure::shape_type_mismatch;
    }
  }
  if (shapes == 0) return LodTileStructure::no_shapes;
  std::size_t bound_nodes = 0;
  for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
    const auto& type = nif.blocks()[block].type;
    if (type == "BSMultiBoundNode") {
      ++bound_nodes;
      const auto bound = read_node_bound_ref(nif, block);
      if (!bound.has_value() || *bound < 0 ||
          static_cast<std::size_t>(*bound) >= nif.blocks().size() ||
          nif.blocks()[static_cast<std::size_t>(*bound)].type != "BSMultiBound") {
        return LodTileStructure::bound_reference_mismatch;
      }
      continue;
    }
    if (type != "BSMultiBound") continue;
    const auto data = read_multi_bound_data_ref(nif, block);
    if (!data.has_value() || *data < 0 ||
        static_cast<std::size_t>(*data) >= nif.blocks().size() ||
        nif.blocks()[static_cast<std::size_t>(*data)].type != "BSMultiBoundAABB") {
      return LodTileStructure::bound_reference_mismatch;
    }
  }
  if (bound_nodes == 0) return LodTileStructure::missing_bound_chain;
  return LodTileStructure::ok;
}

}  // namespace fo4::lod
