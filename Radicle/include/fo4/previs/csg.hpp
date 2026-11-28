#pragma once

// Read-side invariant: chunk entries are (compressed size, absolute offset) and
// chunks must be contiguous from the table end to EOF.
// A `.psg`-shaped file must fail closed: its layout is unmeasured.
// Constraint: a row is offset, descriptor low, descriptor high, vertex count and index count, measured on all rows.

#include "fo4/previs/lod_levels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
struct ShapeMeshData;
}

namespace fo4::previs {

// Source of truth, measured: a shipped `_OC.NIF` carries no `BSTriShape` at all (0 shapes across 429 objects in 48 DLC extras), so its Shared extra is a `(file hash, stream offset)` table and the geometry it names is this CSG stream, never a shape in the same file.
inline constexpr std::string_view k_occluder_geometry_source = "csg";

inline constexpr std::size_t k_csg_row_bytes = 20;
inline constexpr std::size_t k_csg_chunk_entry_bytes = 8;
// Measured invariant: every chunk inflates to 65536 bytes except a smaller final chunk.
inline constexpr std::size_t k_csg_inflated_chunk_max = 65536;

struct CsgRow {
  std::array<std::byte, k_csg_row_bytes> bytes{};
};

struct CsgFile {
  std::vector<CsgRow> rows;
  std::vector<std::vector<std::byte>> chunks;
};

CsgFile parse_csg(std::span<const std::byte> data);
std::vector<std::byte> write_csg(const CsgFile& file);

// Structure check: magic, counts, table bounds, contiguous chunk offset ending at EOF, zlib headers.
std::vector<std::string> validate_csg(std::span<const std::byte> data);

// Decompresses one stored chunk; output must not exceed k_csg_inflated_chunk_max.
std::vector<std::byte> decompress_csg_chunk(std::span<const std::byte> chunk);

// Constraint: one mesh as the stream stores it, vertices at the descriptor stride then one u16 index list per LOD level.
struct CsgMeshObject {
  std::uint64_t vertex_desc{0};
  std::uint32_t vertex_count{0};
  std::vector<std::byte> vertices;
  std::vector<std::uint16_t> indices;
  // Constraint: levels above LOD0 own a vertex block of their own, so an empty list writes an absent level.
  std::array<std::vector<std::uint16_t>, k_previs_lod_levels - 1> higher_lod_indices{};
  // Constraint: a level's vertices are contiguous in the object buffer, measured on 241 of 241 tri-level objects.
  std::array<std::uint32_t, k_previs_lod_levels> level_vertex_count{};

  std::size_t vertex_bytes() const noexcept { return static_cast<std::size_t>(vertex_desc & 0xFu) * 4u; }

  std::vector<std::uint16_t> level_indices(std::size_t level) const {
    if (level == 0) return indices;
    return level <= higher_lod_indices.size() ? higher_lod_indices[level - 1] : std::vector<std::uint16_t>{};
  }

  std::size_t stream_bytes() const noexcept {
    std::size_t words = indices.size();
    for (const auto& level : higher_lod_indices) words += level.size();
    return vertex_count * vertex_bytes() + words * 2u;
  }
};

struct CsgMeshRow {
  std::uint32_t data_offset{0};
  std::uint64_t vertex_desc{0};
  std::uint32_t vertex_count{0};
  std::uint32_t index_count{0};
  // Constraint: the _OC object record repeats these counts and their u16 offsets inside the index block.
  std::array<std::uint32_t, k_previs_lod_levels> lod_triangle_count{};
  std::array<std::uint32_t, k_previs_lod_levels> lod_index_offset{};
  std::array<std::uint32_t, k_previs_lod_levels> lod_vertex_count{};
  std::array<std::uint32_t, k_previs_lod_levels> lod_vertex_offset{};
};

// Constraint: a row is exactly k_csg_row_bytes.
CsgMeshRow decode_csg_row(std::span<const std::byte> row);
void encode_csg_row(const CsgMeshRow& row, std::span<std::byte> out);

struct CsgBuilt {
  std::vector<std::byte> bytes;
  std::vector<CsgMeshRow> rows;
};

// Constraint: vertex bytes and index words are written as they stand, and a descriptor csg_vertex_stride rejects throws.
// Constraint: a non-empty existing container is kept as is and the new meshes are appended after its stream, so earlier row offsets stay valid.
CsgBuilt build_csg(std::span<const CsgMeshObject> meshes, std::span<const std::byte> existing = {});

std::vector<std::byte> inflate_csg_stream(const CsgFile& file);

// Constraint: measured on DLCworkshop01 and DLCCoast: ba2_crc32 of the lowercased stem plus " - geometry".
std::uint32_t csg_file_hash(std::string_view plugin_filename);

// Constraint: the stride comes only from a descriptor a shipped container declares (12, 16, 20 and 24 bytes); any other is a skip.
std::optional<std::size_t> csg_vertex_stride(const CsgMeshRow& row) noexcept;
std::optional<std::size_t> csg_vertex_stride(std::uint64_t vertex_desc) noexcept;
// Invariant: the one shipped descriptor of a stride (12, 16, 20 or 24), the inverse of csg_vertex_stride.
std::optional<std::uint64_t> csg_shipped_descriptor(std::size_t stride) noexcept;
// Invariant: a half shape keeps its bytes, a full-precision one narrows to half with ties to even, and any other shape or a lane past 65504 must throw parse_error.
CsgMeshObject csg_mesh_from_shape(const nif::ShapeMeshData& shape);
// Invariant: the x, y, z of every vertex as the written half bytes read back, so callers work from what the container holds.
std::vector<float> csg_mesh_positions(const CsgMeshObject& mesh);
// Invariant: bytes the row's mesh occupies in the inflated stream, 0 for an unmeasured descriptor, never a guess.
std::size_t csg_row_stream_bytes(const CsgMeshRow& row) noexcept;

// Invariant: rows are looked up by data_offset, so build() keys the table and counts any row stored below its predecessor.
class CsgRowIndex {
 public:
  // Invariant: only the final chunk is inflated, so stream_length() is exact and the build throws when that chunk fails.
  static CsgRowIndex build(const CsgFile& file);

  std::uint64_t stream_length() const noexcept { return length_; }
  const std::vector<CsgMeshRow>& rows() const noexcept { return rows_; }
  std::size_t unsorted_rows() const noexcept { return unsorted_rows_; }
  // Invariant: nullptr when no row starts at that offset, which is what a dangling _OC.NIF object looks up to.
  const CsgMeshRow* find(std::uint32_t data_offset) const;

 private:
  std::vector<CsgMeshRow> rows_;
  std::uint64_t length_{0};
  std::size_t unsorted_rows_{0};
};

// Constraint: every span must come from one cell's `_OC.NIF` object tables; index_count is the row's unit (3 per triangle).
struct CsgMeshSpan {
  std::uint32_t mesh_id{0};
  std::uint32_t data_offset{0};
  std::uint32_t vertex_count{0};
  std::uint32_t index_count{0};
  // Invariant: a zero vertex_desc must skip the descriptor comparison, never report a mismatch.
  std::uint64_t vertex_desc{0};
  // Invariant: the object's own container hash names the CSG holding its row, whatever folder its _OC.NIF sits in.
  std::uint32_t file_hash{0};

  std::size_t vertex_bytes() const noexcept { return static_cast<std::size_t>(vertex_desc & 0xFu) * 4u; }
};

struct CsgCellVerdict {
  // Invariant: an unmeasured vertex layout makes the whole answer a skip, never a verdict.
  bool checked{false};
  bool broken{false};
  std::vector<std::string> issues;
};

CsgCellVerdict check_csg_cell(const CsgRowIndex& index, std::span<const CsgMeshSpan> meshes);

}  // namespace fo4::previs

