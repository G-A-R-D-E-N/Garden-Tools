#include "swf_internal.hpp"

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace fo4::swf::detail {

namespace {

constexpr std::size_t kMaxBitmapBytes = 64u * 1024u * 1024u;

constexpr std::size_t kMaxAlphaBytes = 64u * 1024u * 1024u;

struct JpegSize {
  std::uint16_t width{0};
  std::uint16_t height{0};
  bool known{false};
};

JpegSize scan_jpeg_size(std::span<const std::byte> data) {
  JpegSize size;
  std::size_t position = 0;
  if (data.size() < 4 || byte_at(data, 0) != 0xFF || byte_at(data, 1) != 0xD8) {
    return size;
  }
  position = 2;
  while (position + 1 < data.size()) {
    if (byte_at(data, position) != 0xFF) {
      return size;
    }
    auto marker_position = position + 1;
    while (marker_position < data.size() && byte_at(data, marker_position) == 0xFF) {
      ++marker_position;
    }
    if (marker_position >= data.size()) {
      return size;
    }
    const auto marker = byte_at(data, marker_position);
    position = marker_position + 1;
    if (marker == 0xD9 || marker == 0xDA) {
      return size;
    }
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }
    if (position + 1 >= data.size()) {
      return size;
    }
    const auto length = static_cast<std::size_t>(
        static_cast<std::uint16_t>(byte_at(data, position) << 8) | byte_at(data, position + 1));
    if (length < 2 || position + length > data.size()) {
      return size;
    }
    const bool is_sof = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 &&
                        marker != 0xCC;
    if (is_sof) {
      if (length < 7) {
        return size;
      }
      size.height = static_cast<std::uint16_t>((byte_at(data, position + 3) << 8) |
                                               byte_at(data, position + 4));
      size.width = static_cast<std::uint16_t>((byte_at(data, position + 5) << 8) |
                                              byte_at(data, position + 6));
      size.known = size.width != 0 && size.height != 0;
      return size;
    }
    position += length;
  }
  return size;
}

bool inflate_to(std::span<const std::byte> input, std::size_t expected) {
  if (expected > kMaxBitmapBytes) {
    return false;
  }
  std::vector<std::byte> unpacked(expected == 0 ? 1 : expected);
  uLongf length = static_cast<uLongf>(unpacked.size());
  const auto code = uncompress(reinterpret_cast<Bytef*>(unpacked.data()), &length,
                               reinterpret_cast<const Bytef*>(input.data()),
                               static_cast<uLong>(input.size()));
  return code == Z_OK && static_cast<std::size_t>(length) == expected;
}

bool inflate_fully(std::span<const std::byte> input, std::size_t limit, std::size_t& produced) {
  if (input.empty()) {
    return false;
  }
  z_stream stream{};
  if (inflateInit(&stream) != Z_OK) {
    return false;
  }
  stream.next_in = reinterpret_cast<Bytef*>(const_cast<std::byte*>(input.data()));
  stream.avail_in = static_cast<uInt>(input.size());
  std::vector<std::byte> buffer(64u * 1024u);
  std::size_t total = 0;
  bool ok = false;
  for (;;) {
    stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());
    const auto code = inflate(&stream, Z_NO_FLUSH);
    if (code == Z_STREAM_END) {
      total += buffer.size() - stream.avail_out;
      ok = true;
      break;
    }
    if (code != Z_OK) {
      break;
    }
    total += buffer.size() - stream.avail_out;
    if (total > limit) {
      break;
    }
  }
  inflateEnd(&stream);
  produced = total;
  return ok;
}

bool inflate_alpha(std::span<const std::byte> input, JpegSize size) {
  std::size_t produced = 0;
  if (!inflate_fully(input, kMaxAlphaBytes, produced)) {
    return false;
  }
  if (!size.known) {
    return true;
  }
  return produced == static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

}  // namespace

SwfStatus decode_bitmap_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  Cursor cursor{data, 0};
  SwfBitmap bitmap;
  if (!cursor.u16(bitmap.character_id) || !cursor.u8(bitmap.format) || !cursor.u16(bitmap.width) ||
      !cursor.u16(bitmap.height)) {
    return SwfStatus::malformed_tag;
  }
  if (bitmap.width == 0 || bitmap.height == 0) {
    return SwfStatus::malformed_tag;
  }
  std::size_t palette = 0;
  switch (bitmap.format) {
    case 3: {
      std::uint8_t table_size = 0;
      if (!cursor.u8(table_size)) {
        return SwfStatus::malformed_tag;
      }
      bitmap.color_table_size = table_size;
      palette = static_cast<std::size_t>(table_size + 1u) * (out_tag.code == tag::kDefineBitsLossless2 ? 4u : 3u);
      break;
    }
    case 4:
      palette = 0;
      break;
    case 5:
      palette = 0;
      break;
    default:
      return SwfStatus::malformed_tag;
  }
  const auto pixels = static_cast<std::size_t>(bitmap.format == 3 ? 1u : (bitmap.format == 4 ? 2u : 4u));
  const auto row = (static_cast<std::size_t>(bitmap.width) * pixels + 3u) & ~std::size_t{3};
  if (row > kMaxBitmapBytes / static_cast<std::size_t>(bitmap.height)) {
    return SwfStatus::malformed_tag;
  }
  const auto expected = row * static_cast<std::size_t>(bitmap.height) + palette;
  if (expected > kMaxBitmapBytes) {
    return SwfStatus::malformed_tag;
  }
  const auto packed = data.subspan(cursor.position);
  if (packed.empty() || !inflate_to(packed, expected)) {
    return SwfStatus::malformed_tag;
  }
  bitmap.data_size = packed.size();
  out_tag.bitmap = bitmap;
  return SwfStatus::ok;
}

SwfStatus decode_jpeg_tag(std::span<const std::byte> data, SwfTag& out_tag) {
  if (out_tag.code == tag::kJpegTables) {
    SwfJpegTables tables;
    tables.size = data.size();
    out_tag.jpeg_tables = tables;
    return SwfStatus::ok;
  }
  Cursor cursor{data, 0};
  SwfJpeg jpeg;
  if (!cursor.u16(jpeg.character_id)) {
    return SwfStatus::malformed_tag;
  }
  if (out_tag.code == tag::kDefineBits || out_tag.code == tag::kDefineBitsJpeg2) {
    jpeg.image_size = data.size() - cursor.position;
    if (jpeg.image_size == 0) {
      return SwfStatus::malformed_tag;
    }
    out_tag.jpeg = jpeg;
    return SwfStatus::ok;
  }
  std::uint32_t image_size = 0;
  if (!cursor.u32(image_size)) {
    return SwfStatus::malformed_tag;
  }
  if (out_tag.code == tag::kDefineBitsJpeg4 && !cursor.u16(jpeg.deblock)) {
    return SwfStatus::malformed_tag;
  }
  if (cursor.position > data.size() || data.size() - cursor.position < image_size) {
    return SwfStatus::malformed_tag;
  }
  const auto image = data.subspan(cursor.position, image_size);
  cursor.position += image_size;
  const auto alpha = data.subspan(cursor.position);
  jpeg.image_size = image_size;
  jpeg.has_alpha = !alpha.empty();
  jpeg.alpha_size = alpha.size();
  if (jpeg.has_alpha && !inflate_alpha(alpha, scan_jpeg_size(image))) {
    return SwfStatus::malformed_tag;
  }
  out_tag.jpeg = jpeg;
  return SwfStatus::ok;
}

}  // namespace fo4::swf::detail
