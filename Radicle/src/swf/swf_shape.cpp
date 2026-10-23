#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::swf::detail {

SwfStatus decode_shape_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  SwfReader reader(data, 0);
  SwfShape shape;
  std::size_t next = 0;
  if (!reader.read_u16(shape.shape_id) ||
      !read_rect(data, reader.position(), shape.bounds, next)) {
    return SwfStatus::malformed_tag;
  }
  if (out_tag.code == tag::kDefineShape4) {
    std::size_t edge_next = 0;
    if (!read_rect(data, next, shape.edge_bounds, edge_next) || edge_next >= data.size()) {
      return SwfStatus::malformed_tag;
    }
    shape.flags = byte_at(data, edge_next);
    shape.has_edge_bounds = true;
  }
  out_tag.shape = shape;
  return SwfStatus::ok;
}

}  // namespace fo4::swf::detail
