#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif {
namespace {

constexpr std::size_t k_connect_point_bytes = 40;
constexpr std::size_t k_sized_string_bytes = 4;

std::vector<std::uint32_t> blocks_of_type(const NifFile& nif, std::string_view type) {
  std::vector<std::uint32_t> blocks;
  for (std::size_t i = 0; i < nif.blocks().size(); ++i) {
    if (nif.blocks()[i].type == type) blocks.push_back(static_cast<std::uint32_t>(i));
  }
  return blocks;
}

ScannedConnectPoint read_connect_point(binary::Cursor& cursor) {
  ScannedConnectPoint point;
  point.parent = detail::read_sized_string(cursor);
  point.name = detail::read_sized_string(cursor);
  for (float& value : point.rotation_wxyz) value = cursor.read_f32();
  for (float& value : point.translation) value = cursor.read_f32();
  point.scale = cursor.read_f32();
  return point;
}

void append_connect_parents(const NifFile& nif, std::uint32_t block, std::vector<ScannedConnectPoint>& points) {
  auto cursor = detail::block_cursor(nif, block);
  detail::read_string_ref(cursor, nif);
  const auto count = cursor.read_u32();
  if (static_cast<std::size_t>(count) > cursor.remaining() / k_connect_point_bytes) {
    throw parse_error(cursor.absolute_offset(), "connect point list runs past the end of its block");
  }
  for (std::uint32_t i = 0; i < count; ++i) points.push_back(read_connect_point(cursor));
  if (!cursor.at_end()) {
    throw parse_error(cursor.absolute_offset(), "BSConnectPoint::Parents has bytes past its layout");
  }
}

ScannedConnectChildren read_connect_children_block(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  detail::read_string_ref(cursor, nif);
  ScannedConnectChildren children;
  children.skinned = cursor.read_u8() != 0;
  const auto count = cursor.read_u32();
  if (static_cast<std::size_t>(count) > cursor.remaining() / k_sized_string_bytes) {
    throw parse_error(cursor.absolute_offset(), "connect child list runs past the end of its block");
  }
  children.names.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) children.names.push_back(detail::read_sized_string(cursor));
  if (!cursor.at_end()) {
    throw parse_error(cursor.absolute_offset(), "BSConnectPoint::Children has bytes past its layout");
  }
  return children;
}

}  // namespace

std::vector<ScannedConnectPoint> read_connect_parents(const NifFile& nif) {
  std::vector<ScannedConnectPoint> points;
  for (const auto block : blocks_of_type(nif, "BSConnectPoint::Parents")) append_connect_parents(nif, block, points);
  return points;
}

std::vector<ScannedConnectChildren> read_connect_children(const NifFile& nif) {
  std::vector<ScannedConnectChildren> children;
  for (const auto block : blocks_of_type(nif, "BSConnectPoint::Children")) {
    children.push_back(read_connect_children_block(nif, block));
  }
  return children;
}

}  // namespace fo4::nif
