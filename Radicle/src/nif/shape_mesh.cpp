#include "fo4/nif/shape_mesh.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_triangle_bytes = 6;

}

std::optional<ShapeMeshData> read_shape_mesh(const NifFile& nif, std::uint32_t block) {
  if (block >= nif.blocks().size() || !is_shape_block_type(nif.blocks()[block].type)) return std::nullopt;
  auto cursor = detail::block_cursor(nif, block);
  (void)detail::read_av_header(cursor, nif);
  cursor.read_i32();
  cursor.read_bytes(16);
  cursor.read_i32();
  cursor.read_i32();
  cursor.read_i32();
  const auto descriptor_low = cursor.read_u32();
  const auto descriptor_high = cursor.read_u32();
  const auto descriptor =
      static_cast<std::uint64_t>(descriptor_low) | (static_cast<std::uint64_t>(descriptor_high) << 32U);
  const auto triangle_count = static_cast<std::size_t>(cursor.read_u32());
  const auto vertex_count = static_cast<std::size_t>(cursor.read_u16());
  const auto data_size = static_cast<std::size_t>(cursor.read_u32());
  const auto stride = static_cast<std::size_t>(descriptor & 0xFU) * 4U;
  const auto flags = static_cast<std::uint32_t>(descriptor >> 44U);
  if (data_size == 0 || vertex_count == 0 || stride == 0) return std::nullopt;
  if (vertex_count * stride + triangle_count * k_triangle_bytes != data_size) return std::nullopt;
  if (stride != vertex_bytes_for(flags)) return std::nullopt;
  if (data_size > cursor.remaining()) return std::nullopt;

  ShapeMeshData out;
  out.vertex_desc = descriptor;
  const auto vertex_bytes = cursor.read_bytes(vertex_count * stride);
  out.vertices.assign(vertex_bytes.begin(), vertex_bytes.end());
  const auto index_bytes = cursor.read_bytes(triangle_count * k_triangle_bytes);
  binary::Cursor indices(index_bytes);
  out.indices.reserve(triangle_count * 3U);
  for (std::size_t i = 0; i < triangle_count * 3U; ++i) {
    const auto index = indices.read_u16();
    if (static_cast<std::size_t>(index) >= vertex_count) return std::nullopt;
    out.indices.push_back(index);
  }
  return out;
}

std::optional<ShapeMeshData> read_first_shape_mesh(const NifFile& nif) {
  for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
    if (!is_shape_block_type(nif.blocks()[block].type)) continue;
    if (auto mesh = read_shape_mesh(nif, static_cast<std::uint32_t>(block))) return mesh;
  }
  return std::nullopt;
}

}  // namespace fo4::nif
