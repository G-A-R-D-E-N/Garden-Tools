#include "fo4/previs/csg.hpp"

#include "fo4/archive/ba2_hash.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/shape_mesh.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "../nif/write_model.hpp"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fo4::previs {
namespace {

constexpr std::size_t k_header_bytes = 12;
constexpr std::size_t k_zlib_header_bytes = 2;
constexpr std::size_t k_index_bytes = 2;

// Constraint: only a descriptor a shipped container declares has a stride, measured on the six vanilla containers.
constexpr std::uint64_t k_desc_12 = 0x0000900000020003ull;
constexpr std::uint64_t k_desc_16 = 0x0002900003020004ull;
constexpr std::uint64_t k_desc_20 = 0x0001B00000430205ull;
constexpr std::uint64_t k_desc_24 = 0x0003B00005430206ull;

// Constraint: the attribute flags, without the full-precision bit, of each shipped descriptor, measured on the same containers.
constexpr std::uint32_t k_flags_12 = 0x09;
constexpr std::uint32_t k_flags_16 = 0x29;
constexpr std::uint32_t k_flags_20 = 0x1B;
constexpr std::uint32_t k_flags_24 = 0x3B;
constexpr std::size_t k_full_position_bytes = 16;
constexpr std::size_t k_half_position_bytes = 8;
constexpr std::size_t k_position_slot_floats = 4;

std::uint32_t u32le(std::span<const std::byte> data, std::size_t at) {
  std::uint32_t v = 0;
  std::memcpy(&v, data.data() + at, 4);
  return v;
}

void append_le(std::vector<std::byte>& out, std::uint32_t v) {
  out.push_back(static_cast<std::byte>(v & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
  out.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
}

bool magic_is(std::span<const std::byte> data, std::string_view magic) {
  if (data.size() < 4) return false;
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<char>(data[i]) != magic[i]) return false;
  }
  return true;
}

// Invariant: a half's 5-bit exponent tops out at 65504, so a larger magnitude has no half to round to.
constexpr float k_half_max = 65504.0f;

float half_to_float(std::uint16_t half) noexcept {
  const auto exponent = static_cast<int>((half >> 10U) & 0x1Fu);
  const auto mantissa = static_cast<int>(half & 0x3FFu);
  const float magnitude = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                                        : std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25);
  return (half & 0x8000u) != 0 ? -magnitude : magnitude;
}

bool is_zlib_header(std::span<const std::byte> chunk) {
  return chunk.size() >= k_zlib_header_bytes && std::to_integer<std::uint8_t>(chunk[0]) == 0x78 &&
         std::to_integer<std::uint8_t>(chunk[1]) == 0x9C;
}

}  // namespace

CsgFile parse_csg(std::span<const std::byte> data) {
  if (data.size() < k_header_bytes) throw parse_error(0, "CSG is shorter than its header");
  if (!magic_is(data, "bcsg")) throw parse_error(0, "CSG has a bad magic (only bcsg observed)");
  const auto row_count = u32le(data, 4);
  const auto chunk_count = u32le(data, 8);
  const std::uint64_t table_end = k_header_bytes + std::uint64_t{chunk_count} * k_csg_chunk_entry_bytes +
                                  std::uint64_t{row_count} * k_csg_row_bytes;
  if (table_end > data.size()) throw parse_error(4, "CSG tables run past the end of the file");
  CsgFile file;
  file.rows.reserve(row_count);
  file.chunks.reserve(chunk_count);
  const std::size_t chunk_table = k_header_bytes;
  const std::size_t row_table = chunk_table + static_cast<std::size_t>(chunk_count) * k_csg_chunk_entry_bytes;
  std::uint64_t expected_offset = table_end;
  for (std::uint64_t i = 0; i < chunk_count; ++i) {
    const std::size_t at = chunk_table + static_cast<std::size_t>(i) * k_csg_chunk_entry_bytes;
    const auto size = u32le(data, at);
    const auto offset = u32le(data, at + 4);
    if (std::uint64_t{offset} + size > data.size()) {
      throw parse_error(at, "CSG chunk runs past the end of the file");
    }
    if (offset != expected_offset) {
      throw parse_error(at, "CSG chunks are not contiguous from the table end");
    }
    file.chunks.push_back(std::vector<std::byte>(data.begin() + offset, data.begin() + offset + size));
    expected_offset = std::uint64_t{offset} + size;
  }
  if (expected_offset != data.size()) throw parse_error(8, "CSG chunks do not end at EOF");
  for (std::uint64_t i = 0; i < row_count; ++i) {
    CsgRow row;
    std::memcpy(row.bytes.data(), data.data() + row_table + static_cast<std::size_t>(i) * k_csg_row_bytes,
                k_csg_row_bytes);
    file.rows.push_back(row);
  }
  return file;
}

std::vector<std::byte> write_csg(const CsgFile& file) {
  if (file.chunks.size() > 0xFFFFFFFFu || file.rows.size() > 0xFFFFFFFFu) {
    throw parse_error(0, "CSG table count exceeds u32");
  }
  const auto chunk_count = static_cast<std::uint32_t>(file.chunks.size());
  const auto row_count = static_cast<std::uint32_t>(file.rows.size());
  std::vector<std::byte> out;
  for (const char c : std::string_view{"bcsg"}) out.push_back(static_cast<std::byte>(c));
  append_le(out, row_count);
  append_le(out, chunk_count);
  std::uint64_t payload_at = k_header_bytes + file.chunks.size() * k_csg_chunk_entry_bytes +
                             file.rows.size() * k_csg_row_bytes;
  for (const auto& chunk : file.chunks) {
    if (chunk.size() > 0xFFFFFFFFu || payload_at > 0xFFFFFFFFu ||
        payload_at + chunk.size() > 0xFFFFFFFFu) {
      throw parse_error(0, "CSG chunk offset exceeds u32");
    }
    append_le(out, static_cast<std::uint32_t>(chunk.size()));
    append_le(out, static_cast<std::uint32_t>(payload_at));
    payload_at += chunk.size();
  }
  for (const auto& row : file.rows) {
    out.insert(out.end(), row.bytes.begin(), row.bytes.end());
  }
  for (const auto& chunk : file.chunks) {
    out.insert(out.end(), chunk.begin(), chunk.end());
  }
  return out;
}

std::vector<std::string> validate_csg(std::span<const std::byte> data) {
  CsgFile file;
  try {
    file = parse_csg(data);
  } catch (const std::exception& e) {
    return {e.what()};
  }
  std::vector<std::string> issues;
  for (std::size_t i = 0; i < file.chunks.size(); ++i) {
    if (!is_zlib_header(file.chunks[i])) {
      issues.push_back("CSG chunk " + std::to_string(i) + " does not start with 78 9c");
      break;
    }
  }
  return issues;
}

CsgMeshRow decode_csg_row(std::span<const std::byte> row) {
  if (row.size() < k_csg_row_bytes) throw parse_error(0, "a CSG row is 20 bytes");
  CsgMeshRow out;
  out.data_offset = u32le(row, 0);
  const std::uint64_t low = u32le(row, 4);
  const std::uint64_t high = u32le(row, 8);
  out.vertex_desc = low | (high << 32U);
  out.vertex_count = u32le(row, 12);
  out.index_count = u32le(row, 16);
  return out;
}

void encode_csg_row(const CsgMeshRow& row, std::span<std::byte> out) {
  if (out.size() < k_csg_row_bytes) throw parse_error(0, "a CSG row is 20 bytes");
  const auto put = [&out](std::size_t at, std::uint32_t value) {
    std::memcpy(out.data() + at, &value, 4);
  };
  put(0, row.data_offset);
  put(4, static_cast<std::uint32_t>(row.vertex_desc & 0xFFFFFFFFULL));
  put(8, static_cast<std::uint32_t>(row.vertex_desc >> 32U));
  put(12, row.vertex_count);
  put(16, row.index_count);
}

CsgBuilt build_csg(std::span<const CsgMeshObject> meshes, std::span<const std::byte> existing) {
  if (meshes.empty()) throw parse_error(0, "a CSG needs at least one mesh");
  CsgFile file;
  std::vector<std::byte> stream;
  if (!existing.empty()) {
    file = parse_csg(existing);
    stream = inflate_csg_stream(file);
    file.chunks.clear();
  }
  CsgBuilt built;
  for (const auto& mesh : meshes) {
    const auto stride = mesh.vertex_bytes();
    if (stride == 0 || mesh.vertex_count == 0) throw parse_error(0, "a CSG mesh needs vertices");
    if (!csg_vertex_stride(mesh.vertex_desc)) throw parse_error(0, "a CSG mesh's vertex descriptor is not one the game ships");
    if (mesh.vertices.size() != static_cast<std::size_t>(mesh.vertex_count) * stride) {
      throw parse_error(0, "a CSG mesh's vertex bytes do not match its vertex count");
    }
    if (mesh.indices.size() % 3 != 0 || mesh.indices.empty()) {
      throw parse_error(0, "a CSG mesh's triangle indices must be whole triangles");
    }
    // Constraint: each level addresses its own vertex block, and an authored mesh may leave the blocks empty.
    std::array<std::uint32_t, k_previs_lod_levels> level_count{};
    std::uint32_t declared = 0;
    for (const auto count : mesh.level_vertex_count) declared += count;
    if (declared == 0) {
      level_count[0] = mesh.vertex_count;
    } else {
      if (declared != mesh.vertex_count) {
        throw parse_error(0, "a CSG mesh's LOD vertex blocks do not add up to its vertex count");
      }
      level_count = mesh.level_vertex_count;
    }
    std::array<std::uint32_t, k_previs_lod_levels> level_offset{};
    std::uint32_t next_vertex = 0;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      level_offset[level] = next_vertex;
      next_vertex += level_count[level];
    }
    std::size_t index_words = 0;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      const auto level_indices = mesh.level_indices(level);
      if (level_indices.size() % 3 != 0) throw parse_error(0, "a CSG mesh's LOD indices must be whole triangles");
      for (const auto index : level_indices) {
        if (index < level_offset[level] || index >= level_offset[level] + level_count[level]) {
          throw parse_error(0, "a CSG mesh's LOD index runs past its own vertex block");
        }
      }
      index_words += level_indices.size();
    }
    if (stream.size() > 0xFFFFFFFFULL) throw parse_error(0, "CSG stream exceeds u32 offsets");
    CsgMeshRow row;
    row.data_offset = static_cast<std::uint32_t>(stream.size());
    row.vertex_desc = mesh.vertex_desc;
    row.vertex_count = mesh.vertex_count;
    row.index_count = static_cast<std::uint32_t>(index_words);
    row.lod_vertex_count = level_count;
    row.lod_vertex_offset = level_offset;
    std::size_t last_level = 0;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      row.lod_triangle_count[level] = static_cast<std::uint32_t>(mesh.level_indices(level).size() / 3);
      if (row.lod_triangle_count[level] != 0) last_level = level;
    }
    std::uint32_t next_word = 0;
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      // Constraint: vanilla writes the word offset up to the last level that holds triangles and zero past it, measured on 303,835 objects.
      row.lod_index_offset[level] = level <= last_level ? next_word : 0u;
      next_word += static_cast<std::uint32_t>(mesh.level_indices(level).size());
    }
    if (next_word != index_words) throw parse_error(0, "a CSG mesh's LOD words do not add up");
    CsgRow raw;
    encode_csg_row(row, raw.bytes);
    file.rows.push_back(raw);
    built.rows.push_back(row);
    stream.insert(stream.end(), mesh.vertices.begin(), mesh.vertices.end());
    for (std::size_t level = 0; level < k_previs_lod_levels; ++level) {
      for (const auto index : mesh.level_indices(level)) {
        stream.push_back(static_cast<std::byte>(index & 0xFFu));
        stream.push_back(static_cast<std::byte>((index >> 8U) & 0xFFu));
      }
    }
  }
  for (std::size_t at = 0; at < stream.size(); at += k_csg_inflated_chunk_max) {
    const auto length = std::min(k_csg_inflated_chunk_max, stream.size() - at);
    uLongf bound = compressBound(static_cast<uLong>(length));
    std::vector<std::byte> chunk(bound);
    const int rc = compress2(reinterpret_cast<Bytef*>(chunk.data()), &bound,
                             reinterpret_cast<const Bytef*>(stream.data() + at), static_cast<uLong>(length),
                             Z_DEFAULT_COMPRESSION);
    if (rc != Z_OK) throw parse_error(0, "CSG chunk compressor failed");
    chunk.resize(bound);
    file.chunks.push_back(std::move(chunk));
  }
  built.bytes = write_csg(file);
  const auto issues = validate_csg(built.bytes);
  if (!issues.empty()) throw parse_error(0, issues.front());
  return built;
}

std::vector<std::byte> inflate_csg_stream(const CsgFile& file) {
  std::vector<std::byte> out;
  for (const auto& chunk : file.chunks) {
    const auto bytes = decompress_csg_chunk(chunk);
    out.insert(out.end(), bytes.begin(), bytes.end());
  }
  return out;
}

std::uint32_t csg_file_hash(std::string_view plugin_filename) {
  const std::filesystem::path path{std::string(plugin_filename)};
  const auto stem = path.stem().string();
  const auto text = archive::normalize_ba2_name(stem + " - Geometry");
  return archive::ba2_crc32(std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()));
}

std::vector<std::byte> decompress_csg_chunk(std::span<const std::byte> chunk) {
  if (chunk.empty()) throw parse_error(0, "CSG chunk is empty");
  std::vector<std::byte> out(k_csg_inflated_chunk_max);
  z_stream stream{};
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(chunk.data()));
  stream.avail_in = static_cast<uInt>(chunk.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  if (inflateInit(&stream) != Z_OK) throw parse_error(0, "CSG chunk inflater failed to start");
  const int rc = inflate(&stream, Z_FINISH);
  const std::size_t produced = out.size() - stream.avail_out;
  inflateEnd(&stream);
  if (rc != Z_STREAM_END) throw parse_error(0, "CSG chunk does not inflate to at most 65536 bytes");
  out.resize(produced);
  return out;
}

std::optional<std::size_t> csg_vertex_stride(std::uint64_t vertex_desc) noexcept {
  if (vertex_desc == k_desc_12) return 12;
  if (vertex_desc == k_desc_16) return 16;
  if (vertex_desc == k_desc_20) return 20;
  if (vertex_desc == k_desc_24) return 24;
  return std::nullopt;
}

std::optional<std::size_t> csg_vertex_stride(const CsgMeshRow& row) noexcept {
  return csg_vertex_stride(row.vertex_desc);
}

std::optional<std::uint64_t> csg_shipped_descriptor(std::size_t stride) noexcept {
  for (const auto desc : {k_desc_12, k_desc_16, k_desc_20, k_desc_24}) {
    if (csg_vertex_stride(desc) == stride) return desc;
  }
  return std::nullopt;
}

CsgMeshObject csg_mesh_from_shape(const nif::ShapeMeshData& shape) {
  const nif::VertexDesc source{shape.vertex_desc};
  if (const auto fault = nif::vertex_layout_fault(source); !fault.empty()) {
    throw parse_error(0, "the shape's vertex layout " + fault);
  }
  const auto flags = source.flags() & ~nif::k_vertex_full_precision;
  std::uint64_t shipped = 0;
  if (flags == k_flags_12) shipped = k_desc_12;
  if (flags == k_flags_16) shipped = k_desc_16;
  if (flags == k_flags_20) shipped = k_desc_20;
  if (flags == k_flags_24) shipped = k_desc_24;
  if (shipped == 0) {
    std::ostringstream text;
    text << "the shape's vertex attributes (flags 0x" << std::hex << source.flags()
         << ") are none of the four layouts the game ships";
    throw parse_error(0, text.str());
  }
  const std::size_t stride = source.size_bytes();
  if (stride == 0 || shape.vertices.empty() || shape.vertices.size() % stride != 0) {
    throw parse_error(0, "the shape's vertex bytes are not whole vertices");
  }
  CsgMeshObject out;
  out.vertex_desc = shipped;
  out.vertex_count = static_cast<std::uint32_t>(shape.vertices.size() / stride);
  out.indices = shape.indices;
  if (!source.full_precision()) {
    out.vertices = shape.vertices;
    return out;
  }
  const std::size_t narrowed = stride - (k_full_position_bytes - k_half_position_bytes);
  out.vertices.reserve(std::size_t{out.vertex_count} * narrowed);
  for (std::size_t vertex = 0; vertex < out.vertex_count; ++vertex) {
    const std::byte* at = shape.vertices.data() + vertex * stride;
    for (std::size_t lane = 0; lane < k_position_slot_floats; ++lane) {
      float value = 0.0f;
      std::memcpy(&value, at + lane * sizeof value, sizeof value);
      if (!std::isfinite(value) || std::fabs(value) > k_half_max) {
        throw parse_error(0, "a full-precision position lane is not finite or is past the half range of 65504 at vertex " +
                                 std::to_string(vertex));
      }
      // Invariant: a value between two halves must go to the even one.
      const auto half = nif::write_detail::half_bits_even(value);
      out.vertices.push_back(static_cast<std::byte>(half & 0xFFu));
      out.vertices.push_back(static_cast<std::byte>(half >> 8U));
    }
    out.vertices.insert(out.vertices.end(), at + k_full_position_bytes, at + stride);
  }
  return out;
}

std::vector<float> csg_mesh_positions(const CsgMeshObject& mesh) {
  const auto stride = csg_vertex_stride(mesh.vertex_desc);
  if (!stride || mesh.vertices.size() != std::size_t{mesh.vertex_count} * *stride) {
    throw parse_error(0, "a CSG mesh's vertex bytes do not match a shipped layout");
  }
  std::vector<float> out;
  out.reserve(std::size_t{mesh.vertex_count} * 3u);
  for (std::size_t vertex = 0; vertex < mesh.vertex_count; ++vertex) {
    for (std::size_t lane = 0; lane < 3; ++lane) {
      const auto at = vertex * *stride + lane * 2u;
      const auto bits = static_cast<std::uint16_t>(std::to_integer<unsigned>(mesh.vertices[at]) |
                                                   (std::to_integer<unsigned>(mesh.vertices[at + 1]) << 8U));
      if ((bits & 0x7C00u) == 0x7C00u) {
        throw parse_error(0, "a CSG mesh's position lane is a half infinity or NaN at vertex " + std::to_string(vertex));
      }
      out.push_back(half_to_float(bits));
    }
  }
  return out;
}

std::size_t csg_row_stream_bytes(const CsgMeshRow& row) noexcept {
  const auto stride = csg_vertex_stride(row);
  if (!stride) return 0;
  return std::size_t{row.vertex_count} * *stride + std::size_t{row.index_count} * k_index_bytes;
}

CsgRowIndex CsgRowIndex::build(const CsgFile& file) {
  CsgRowIndex index;
  index.rows_.reserve(file.rows.size());
  for (const auto& row : file.rows) index.rows_.push_back(decode_csg_row(row.bytes));
  for (std::size_t i = 1; i < index.rows_.size(); ++i) {
    if (index.rows_[i].data_offset < index.rows_[i - 1].data_offset) ++index.unsorted_rows_;
  }
  if (index.unsorted_rows_ != 0) {
    std::stable_sort(index.rows_.begin(), index.rows_.end(),
                     [](const CsgMeshRow& a, const CsgMeshRow& b) { return a.data_offset < b.data_offset; });
  }
  if (!file.chunks.empty()) {
    const auto last = decompress_csg_chunk(file.chunks.back());
    index.length_ = std::uint64_t{file.chunks.size() - 1} * k_csg_inflated_chunk_max + last.size();
  }
  return index;
}

const CsgMeshRow* CsgRowIndex::find(std::uint32_t data_offset) const {
  const auto at = std::lower_bound(rows_.begin(), rows_.end(), data_offset,
                                   [](const CsgMeshRow& row, std::uint32_t offset) { return row.data_offset < offset; });
  if (at == rows_.end() || at->data_offset != data_offset) return nullptr;
  return &*at;
}

CsgCellVerdict check_csg_cell(const CsgRowIndex& index, std::span<const CsgMeshSpan> meshes) {
  CsgCellVerdict verdict;
  verdict.checked = true;
  bool skipped = false;
  std::vector<std::string> skip_notes;
  // Constraint: a table stored out of order is malformed for the reader, so it is a verdict, not a repair.
  if (index.unsorted_rows() != 0) {
    verdict.broken = true;
    verdict.issues.push_back("CSG row table is not sorted by stream offset (" +
                             std::to_string(index.unsorted_rows()) + " rows out of order)");
  }
  for (const auto& mesh : meshes) {
    const CsgMeshRow* row = index.find(mesh.data_offset);
    if (row == nullptr) {
      verdict.broken = true;
      verdict.issues.push_back("CSG has no row at stream offset " + std::to_string(mesh.data_offset) +
                               " for OC id " + std::to_string(mesh.mesh_id));
      continue;
    }
    const auto stride = csg_vertex_stride(*row);
    if (!stride) {
      skipped = true;
      skip_notes.push_back("CSG row at stream offset " + std::to_string(row->data_offset) +
                           " uses an unmeasured vertex layout");
      continue;
    }
    // Ordering: the descriptor check runs before the count check, since matching counts cannot prove the layout.
    if (mesh.vertex_desc != 0 && row->vertex_desc != mesh.vertex_desc) {
      verdict.broken = true;
      verdict.issues.push_back("CSG row at stream offset " + std::to_string(row->data_offset) + " packs " +
                               std::to_string(*stride) + "-byte vertices, but OC id " +
                               std::to_string(mesh.mesh_id) + " declares " + std::to_string(mesh.vertex_bytes()) +
                               "-byte ones");
      continue;
    }
    if (row->vertex_count != mesh.vertex_count || row->index_count != mesh.index_count) {
      verdict.broken = true;
      verdict.issues.push_back("CSG row at stream offset " + std::to_string(row->data_offset) + " stores " +
                               std::to_string(row->vertex_count) + " vertices and " +
                               std::to_string(row->index_count) + " indices, but OC id " +
                               std::to_string(mesh.mesh_id) + " spans " + std::to_string(mesh.vertex_count) +
                               " vertices and " + std::to_string(mesh.index_count) + " indices");
      continue;
    }
    const std::uint64_t end = std::uint64_t{row->data_offset} + csg_row_stream_bytes(*row);
    if (end > index.stream_length()) {
      verdict.broken = true;
      verdict.issues.push_back("CSG mesh at stream offset " + std::to_string(row->data_offset) + " ends at " +
                               std::to_string(end) + ", past the end of the inflated stream at " +
                               std::to_string(index.stream_length()));
    }
  }
  if (skipped && !verdict.broken) {
    verdict.checked = false;
    verdict.issues = std::move(skip_notes);
  }
  return verdict;
}

}  // namespace fo4::previs
