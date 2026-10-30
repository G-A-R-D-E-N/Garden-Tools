#include "fo4/nif/vertex_desc.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace fo4::nif {
namespace {

constexpr std::size_t k_sphere_bytes = 16;
constexpr std::size_t k_reference_bytes = 12;
constexpr std::uint32_t k_small_position_bytes = 8;
constexpr std::uint32_t k_full_position_bytes = 16;
constexpr std::uint32_t k_component_bytes = 4;
constexpr std::uint32_t k_skin_bytes = 12;

// Invariant: the names are the VertexDesc attribute ids in order (commonlibf4, include/RE/B/BSGraphics.h).
constexpr std::array<std::string_view, k_vertex_attribute_count> k_attribute_names{
    "Position", "UV0", "UV1", "Normal", "Binormal", "Color", "Skinning", "LandData", "EyeData",
};

// Invariant: land data has no vertex storage.
std::uint32_t attribute_width(VertexAttribute attribute, bool full_precision) noexcept {
  switch (attribute) {
    case VertexAttribute::Position:
      return full_precision ? k_full_position_bytes : k_small_position_bytes;
    case VertexAttribute::Skinning:
      return k_skin_bytes;
    case VertexAttribute::LandData:
      return 0;
    default:
      return k_component_bytes;
  }
}

}  // namespace

std::uint32_t VertexDesc::attribute_bytes(VertexAttribute attribute) const noexcept {
  return attribute_width(attribute, full_precision());
}

std::size_t vertex_bytes_for(std::uint32_t flags) noexcept {
  const bool full_precision = (flags & k_vertex_full_precision) != 0;
  // Invariant: position is counted before the flags, whatever bit 0 says.
  std::size_t bytes = attribute_width(VertexAttribute::Position, full_precision);
  for (std::size_t id = 1; id < k_vertex_attribute_count; ++id) {
    if (((flags >> id) & 1U) == 0) continue;
    bytes += attribute_width(static_cast<VertexAttribute>(id), full_precision);
  }
  return bytes;
}

const char* vertex_attribute_name(VertexAttribute attribute) noexcept {
  const auto index = static_cast<std::size_t>(attribute);
  return index < k_attribute_names.size() ? k_attribute_names[index].data() : nullptr;
}

VertexDesc read_vertex_desc(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  detail::read_av_header(cursor, nif);
  cursor.read_i32();
  cursor.read_bytes(k_sphere_bytes);
  cursor.read_bytes(k_reference_bytes);
  const auto low = cursor.read_u32();
  const auto high = cursor.read_u32();
  return VertexDesc{static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32U)};
}

}  // namespace fo4::nif
