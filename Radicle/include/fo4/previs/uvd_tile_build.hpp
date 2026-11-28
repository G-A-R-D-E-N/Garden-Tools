#pragma once

// Constraint: the builder emits one open tile that lists every object no closed occluder shell encloses, and leaves the remaining visibility to the retail tooling.

#include "fo4/previs/uvd_tile_model.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::previs {

inline constexpr char k_uvd_default_banner[] = "T 512.0 SO 128.0 SH 16.000 BF 100 F 0 CS 0.0 - 3.3.17 F 1 0 OG 0";
inline constexpr float k_uvd_lod_base = 512.0f;
inline constexpr std::uint32_t k_uvd_build_faces = 6;

struct UvdBuildObject {
  std::array<float, 6> box{};
  std::uint32_t id{0};
};

struct UvdBuildInput {
  std::optional<std::array<float, 6>> cell_box;
  std::vector<UvdBuildObject> objects;
  // Constraint: an occluder is one placed triangle, so a shell assembled from separate wall bodies can enclose.
  std::vector<std::array<float, 9>> occluders;
  std::string banner;
  std::optional<float> expand;
};

// Constraint: an object survives unless occluder triangles within 4096 units block the six axes from its centre and from each of its eight box corners; the survivors keep their input order.
std::vector<UvdBuildObject> uvd_cull_occluded(const UvdBuildInput& input);

// Constraint: the box of an xyz triple list, or nothing for an empty, ragged or non-finite list.
std::optional<std::array<float, 6>> uvd_positions_box(std::span<const float> xyz);

// Constraint: a tile's LOD value is cbrt(volume) / lod base rounded to float, measured exact on 321,002 retail tiles.
float uvd_tile_lod(const std::array<float, 6>& box, float lod_base);

// Constraint: expand is the largest power of two of at most longest side / 64 within 16..128, a guess that sits within a factor of two of retail on 99.3% of tiles.
float uvd_default_expand(const std::array<float, 6>& box);

// Constraint: the world box is the cell box joined with every object box, any axis without thickness is widened by one lod base, and a build with neither a box nor an object is refused.
std::array<float, 6> uvd_build_world_box(const UvdBuildInput& input);

// Constraint: throws UvdTileLayoutError for a non-finite or inverted box, a repeated object id, or an input with no extent.
UvdTileModel build_conservative_uvd_model(const UvdBuildInput& input);

// Constraint: the bytes are checked against validate_uvd_tiles before they are returned, so a builder bug cannot reach disk.
UvdBytes write_uvd_tome_tiled(const UvdBuildInput& input);

// Constraint: container issues first, then the tile model's reason or every violated invariant; an empty list is a structurally valid tome.
std::vector<std::string> validate_uvd_tome_tiled(std::span<const std::byte> bytes);

// Constraint: the tome lands at Vis/<plugin folder>/<cell low 24 as %08X>.uvd, and a generated tome's folder is the patch plugin's file name.
std::filesystem::path uvd_tome_path(const std::filesystem::path& data_dir, std::string_view plugin_folder,
                                    std::uint32_t cell_low24);

// Constraint: bytes that fail validation are refused before the file is opened, so a VISI never names a tome the reader rejects.
std::filesystem::path write_visi_tome(const std::filesystem::path& data_dir, std::string_view plugin_folder,
                                      std::uint32_t cell_low24, std::span<const std::byte> tome);

}  // namespace fo4::previs
