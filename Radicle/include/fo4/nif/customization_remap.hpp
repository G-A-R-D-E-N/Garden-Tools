#pragma once

#include "fo4/nif/nif.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {

inline constexpr std::string_view k_customization_remap_name = "CustomizationRemapData";
inline constexpr std::string_view k_customization_remap_new_bones_name = "CustomizationRemapNewBonesData";
inline constexpr std::size_t k_remap_record_bytes = 12;
inline constexpr std::size_t k_remap_new_bone_bytes = 208;
inline constexpr std::size_t k_remap_new_bone_name_bytes = 16;
inline constexpr std::size_t k_remap_new_bone_matrix_at = 0x90;

// Invariant: one record is the skin record, four half-float weights then four u8 indices.
struct RemapRecord {
  std::array<float, 4> weight{};
  std::array<std::uint8_t, 4> bone{};
};

// Invariant: an entry is kept raw so a round trip is byte-exact, and only the name and matrix have typed access.
struct RemapNewBone {
  std::array<std::byte, k_remap_new_bone_bytes> raw{};
};

// Invariant: a block of -1 means the shape owns no such block, and new_bones then stays empty.
struct CustomizationRemap {
  std::int32_t remap_block{-1};
  std::int32_t new_bones_block{-1};
  std::vector<RemapRecord> records;
  std::vector<RemapNewBone> new_bones;
};

std::string remap_new_bone_name(const RemapNewBone& bone);
std::array<float, 16> remap_new_bone_matrix(const RemapNewBone& bone);
void set_remap_new_bone_matrix(RemapNewBone& bone, const std::array<float, 16>& matrix);

// Requires a tri-shape block; the blocks are the ones its own extra-data list refers to.
CustomizationRemap read_customization_remap(const NifFile& nif, std::uint32_t shape);

// Requires a shape that already owns the remap block, one record per vertex, and an owned new-bones block for new bones.
// Invariant: an owned new-bones block is left untouched when remap.new_bones is empty and remap.new_bones_block is -1.
std::vector<std::byte> write_customization_remap(const NifFile& nif, std::uint32_t shape,
                                                 const CustomizationRemap& remap);

// Requires a tri-shape block; a non-empty records list adds the remap block, a non-empty new_bones list adds the new-bones block.
// Invariant: a block the shape already owns is refused, never replaced, and the record count must equal the vertex count.
std::vector<std::byte> create_customization_remap(const NifFile& nif, std::uint32_t shape,
                                                  const CustomizationRemap& remap);

// Invariant: the checks report and never refuse, so an odd file stays readable; a record count that differs from the vertex count is reported here.
std::vector<std::string> validate_customization_remap(const CustomizationRemap& remap, std::size_t skin_bone_count,
                                                      std::size_t vertex_count);

// Requires every entry to name an existing record, so a new vertex needs the old vertex it came from.
std::vector<RemapRecord> reindex_remap_records(std::span<const RemapRecord> records,
                                               std::span<const std::int32_t> source_of_vertex);

}  // namespace fo4::nif
