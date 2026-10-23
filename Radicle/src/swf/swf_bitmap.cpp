#include "swf_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::swf::detail {

SwfStatus decode_bitmap_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  SwfReader reader(data, 0);
  SwfBitmap bitmap;
  if (!reader.read_u16(bitmap.character_id) || !reader.read_u8(bitmap.format) ||
      !reader.read_u16(bitmap.width) || !reader.read_u16(bitmap.height)) {
    return SwfStatus::malformed_tag;
  }
  if (bitmap.format == 3) {
    std::uint8_t table_size = 0;
    if (!reader.read_u8(table_size)) {
      return SwfStatus::malformed_tag;
    }
    bitmap.color_table_size = table_size;
  }
  out_tag.bitmap = bitmap;
  return SwfStatus::ok;
}

}  // namespace fo4::swf::detail
