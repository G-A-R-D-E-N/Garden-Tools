#pragma once

#include "fo4/previs/lod_levels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace fo4::previs {

std::size_t shared_combined_geom_extra_size(std::size_t object_count, std::size_t combined_per_object);

struct SharedGeomExtraLayout {
  std::size_t object_count{0};
  std::size_t combined_per_object{0};
};

bool measure_shared_combined_geom_extra(std::span<const std::byte> block, SharedGeomExtraLayout& out);

std::optional<SharedGeomExtraLayout> detect_shared_geom_layout(std::span<const std::byte> block);

bool walk_shared_combined_geom_extra(std::span<const std::byte> block);

std::size_t shared_combined_object_slots(std::span<const std::byte> block);

struct SharedCombinedRecordXform {
  float translation[3]{};
  float rotation[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  float scale{1.0f};
  // Constraint: the stored bound is the mesh's own sphere placed under this transform.
  float bound_center[3]{};
  float bound_radius{0.0f};
};

std::optional<std::size_t> shared_combined_record_offset(std::span<const std::byte> block,
                                                           std::size_t object_index,
                                                           std::size_t combined_index);

bool read_shared_combined_record_xform(std::span<const std::byte> block,
                                       std::size_t object_index,
                                       std::size_t combined_index,
                                       SharedCombinedRecordXform& out);

// Constraint: the record's grayscale word is left as it is and its bounding sphere is moved with the
// transform, so the written instance keeps the bounds of the mesh the record already describes.
bool write_shared_combined_record_xform(std::span<std::byte> block,
                                        std::size_t object_index,
                                        std::size_t combined_index,
                                        const SharedCombinedRecordXform& xform);

// One source mesh of a Shared extra, measured on the vanilla oracles. The extra opens with a table of
// (file hash, data offset) pairs; data_offset addresses the inflated `<plugin> - Geometry.csg` stream, where
// the mesh's vertices (vertex_desc's size each) are followed by its triangles (three u16 each).
struct SharedGeomObject {
  std::uint32_t file_hash{0};
  std::uint32_t data_offset{0};
  std::uint32_t vertex_count{0};
  std::uint32_t lod_levels{0};
  std::uint32_t triangle_count[3]{};
  std::uint32_t triangle_offset[3]{};
  std::uint32_t combined_count{0};
  std::uint64_t vertex_desc{0};

  std::size_t vertex_bytes() const noexcept { return static_cast<std::size_t>(vertex_desc & 0xFu) * 4u; }
  std::size_t triangles() const noexcept {
    return std::size_t{triangle_count[0]} + triangle_count[1] + triangle_count[2];
  }
  // Bytes the mesh occupies in the inflated CSG stream.
  std::size_t stream_bytes() const noexcept { return vertex_count * vertex_bytes() + triangles() * 6u; }
};

struct SharedGeomHeader {
  std::uint64_t vertex_desc{0};
  std::uint32_t vertex_count{0};
  std::uint32_t triangle_count{0};
  std::vector<SharedGeomObject> objects;
};

// Empty unless the block walks as the table layout (hash/offset pairs, then one record per object).
std::optional<SharedGeomHeader> read_shared_geom_header(std::span<const std::byte> block);

std::optional<std::vector<std::byte>> rebuild_compact_shared_geom_extra(
    std::span<const std::byte> template_block,
    std::size_t object_count,
    std::span<const SharedCombinedRecordXform> xforms);

// Constraint: one authored object is its CSG address, its shape descriptor and counts, and one record per placement.
struct SharedGeomObjectWrite {
  std::uint32_t data_offset{0};
  std::uint64_t vertex_desc{0};
  std::uint32_t vertex_count{0};
  // Constraint: one count per LOD level; vanilla may leave lower levels empty (e.g. {0,0,276}).
  std::array<std::uint32_t, k_previs_lod_levels> triangle_count{};
  std::uint32_t lod_levels{k_previs_lod_levels};
  std::vector<SharedCombinedRecordXform> records;
};

// Constraint: triangles are u16 indices, so one object holds at most 65535 vertices; the header totals are bounded by their u32 words only.
inline constexpr std::uint32_t k_shared_geom_max_object_vertices = 65535u;
// Constraint: this is the reader's own bound (header_walkable), not a game limit; a larger block would not re-read.
inline constexpr std::size_t k_shared_geom_max_objects = 512;

enum class SharedGeomBuildError {
  no_objects,
  object_without_records,
  object_without_geometry,
  too_many_objects,
  vertex_count_exceeds_u16,
  totals_exceed_u32,
  template_unsupported,
  result_unreadable,
};

// Constraint: the template must be table layout with one record, and supplies the fields the writer does not own.
std::expected<std::vector<std::byte>, SharedGeomBuildError> build_shared_geom_extra_checked(
    std::span<const std::byte> template_block,
    std::uint32_t file_hash,
    std::span<const SharedGeomObjectWrite> objects);

std::optional<std::vector<std::byte>> build_shared_geom_extra(std::span<const std::byte> template_block,
                                                              std::uint32_t file_hash,
                                                              std::span<const SharedGeomObjectWrite> objects);

}  // namespace fo4::previs
