#pragma once

#include "fo4/swf/swf.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace swf_art_test {

using fo4::swf::SwfReadResult;
using fo4::swf::SwfStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

inline std::vector<std::byte> movie_with(std::uint16_t code, std::span<const std::byte> payload) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(code, payload, false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

inline SwfReadResult read_one(std::uint16_t code, std::span<const std::byte> payload) {
  const auto bytes = movie_with(code, payload);
  return fo4::swf::read_swf(bytes);
}

inline void rgb(SwfBuilder& builder, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
  builder.u8(red);
  builder.u8(green);
  builder.u8(blue);
}

inline void rgba(SwfBuilder& builder, std::uint32_t red, std::uint32_t green, std::uint32_t blue,
          std::uint32_t alpha) {
  rgb(builder, red, green, blue);
  builder.u8(alpha);
}

inline void signed_bits(SwfBuilder& builder, std::int32_t value, unsigned count) {
  builder.push_bits(static_cast<std::uint32_t>(value), count);
}

inline void identity_matrix(SwfBuilder& builder) {
  builder.push_bits(0, 1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 5);
  builder.align();
}

inline std::vector<std::byte> zlib_bytes(std::span<const std::uint8_t> raw) {
  uLongf packed = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> buffer(static_cast<std::size_t>(packed));
  EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(buffer.data()), &packed,
                      reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()),
                      Z_BEST_COMPRESSION),
            Z_OK);
  buffer.resize(static_cast<std::size_t>(packed));
  return buffer;
}

inline void shape_head(SwfBuilder& body, std::uint16_t id) {
  body.u16(id);
  body.rect(0, 2000, 0, 1000, 12);
}

inline void solid_fill(SwfBuilder& body, bool alpha, std::uint32_t value) {
  body.u8(0x00);
  rgb(body, (value >> 16) & 0xFFu, (value >> 8) & 0xFFu, value & 0xFFu);
  if (alpha) {
    body.u8(0xFF);
  }
}

inline std::vector<std::byte> define_shape_3_payload() {
  SwfBuilder body;
  shape_head(body, 5);
  body.u8(3);
  solid_fill(body, true, 0x112233);
  body.u8(0x10);
  identity_matrix(body);
  body.u8(0x40 | 0x02);
  body.u8(0);
  rgba(body, 255, 0, 0, 255);
  body.u8(255);
  rgba(body, 0, 0, 255, 128);
  body.u8(0x41);
  body.u16(7);
  identity_matrix(body);
  body.u8(1);
  body.u16(20);
  rgba(body, 0x09, 0x08, 0x07, 0xFF);
  body.u8(0x22);
  body.push_bits(0, 1);
  body.push_bits(0x0F, 5);
  body.push_bits(10, 5);
  signed_bits(body, 100, 10);
  signed_bits(body, -50, 10);
  body.push_bits(1, 2);
  body.push_bits(2, 2);
  body.push_bits(1, 2);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(4, 4);
  body.push_bits(1, 1);
  signed_bits(body, 10, 6);
  signed_bits(body, -20, 6);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(4, 4);
  body.push_bits(0, 1);
  body.push_bits(1, 1);
  signed_bits(body, 30, 6);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(4, 4);
  body.push_bits(0, 1);
  body.push_bits(0, 1);
  signed_bits(body, -15, 6);
  body.push_bits(1, 1);
  body.push_bits(0, 1);
  body.push_bits(4, 4);
  signed_bits(body, 5, 6);
  signed_bits(body, 5, 6);
  signed_bits(body, 10, 6);
  signed_bits(body, 0, 6);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> define_shape_4_payload() {
  SwfBuilder body;
  body.u16(9);
  body.rect(0, 1000, 0, 500, 11);
  body.rect(0, 900, 0, 400, 11);
  body.u8(0x04);
  body.u8(1);
  solid_fill(body, true, 0x445566);
  body.u8(2);
  body.u16(40);
  // Requires spec bit order: StartCapStyle occupies the top two bits of the field's first byte.
  body.u8(0x60);
  body.u8(0x00);
  body.u16(0x0300);
  rgba(body, 0x0A, 0x0B, 0x0C, 0xFF);
  body.u16(5);
  body.u8(0x08);
  body.u8(0x00);
  solid_fill(body, true, 0x778899);
  body.u8(0x12);
  body.push_bits(0, 1);
  body.push_bits(0x01, 5);
  body.push_bits(8, 5);
  signed_bits(body, 16, 8);
  signed_bits(body, 8, 8);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> define_shape_2_new_styles_lines_payload() {
  SwfBuilder body;
  shape_head(body, 23);
  body.u8(1);
  solid_fill(body, false, 0x010203);
  body.u8(1);
  body.u16(5);
  rgb(body, 0x14, 0x15, 0x16);
  body.u8(0x11);
  body.push_bits(0, 1);
  body.push_bits(0x10, 5);
  body.align();
  body.u8(2);
  solid_fill(body, false, 0x040506);
  solid_fill(body, false, 0x0B0C0D);
  body.u8(1);
  body.u16(7);
  rgb(body, 0x0E, 0x0F, 0x10);
  body.u8(0x21);
  body.push_bits(0, 1);
  body.push_bits(0x0A, 5);
  body.push_bits(1, 2);
  body.push_bits(1, 1);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> define_shape_2_new_styles_payload() {
  SwfBuilder body;
  shape_head(body, 21);
  body.u8(1);
  solid_fill(body, false, 0x010203);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x10, 5);
  body.align();
  body.u8(2);
  solid_fill(body, false, 0x040506);
  solid_fill(body, false, 0x070809);
  body.u8(0);
  body.u8(0x20);
  body.push_bits(0, 1);
  body.push_bits(0x02, 5);
  body.push_bits(2, 2);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> define_shape_2_extended_counts_payload() {
  SwfBuilder body;
  shape_head(body, 33);
  body.u8(0xFF);
  body.u16(3);
  solid_fill(body, false, 0x101010);
  solid_fill(body, false, 0x202020);
  solid_fill(body, false, 0x303030);
  body.u8(0xFF);
  body.u16(0);
  body.u8(0x30);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> start_edges(const std::vector<std::byte>& styles_and_bits) {
  SwfBuilder body;
  body.bytes(styles_and_bits);
  body.push_bits(0, 1);
  body.push_bits(0x01, 5);
  body.push_bits(8, 5);
  signed_bits(body, 1, 8);
  signed_bits(body, 2, 8);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(2, 4);
  body.push_bits(1, 1);
  signed_bits(body, 4, 4);
  signed_bits(body, 6, 4);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> end_edges() {
  SwfBuilder body;
  body.u8(0);
  body.push_bits(0, 1);
  body.push_bits(0x01, 5);
  body.push_bits(8, 5);
  signed_bits(body, -1, 8);
  signed_bits(body, -2, 8);
  body.push_bits(1, 1);
  body.push_bits(1, 1);
  body.push_bits(2, 4);
  body.push_bits(1, 1);
  signed_bits(body, -4, 4);
  signed_bits(body, -6, 4);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> morph_styles(bool version_two) {
  SwfBuilder body;
  body.u8(2);
  body.u8(0x00);
  rgba(body, 1, 2, 3, 4);
  rgba(body, 5, 6, 7, 8);
  body.u8(0x10);
  identity_matrix(body);
  identity_matrix(body);
  body.u8(0x11);
  body.u8(0);
  rgba(body, 9, 10, 11, 12);
  body.u8(200);
  rgba(body, 13, 14, 15, 16);
  body.u8(1);
  body.u16(30);
  body.u16(60);
  if (version_two) {
    body.u8(0x08);
    body.u8(0x00);
    body.u8(0x00);
    rgba(body, 17, 18, 19, 20);
    rgba(body, 21, 22, 23, 24);
  } else {
    rgba(body, 17, 18, 19, 20);
    rgba(body, 21, 22, 23, 24);
  }
  body.u8(0x10);
  return body.body;
}

inline std::vector<std::byte> morph_payload(bool version_two) {
  SwfBuilder body;
  body.u16(12);
  body.rect(0, 800, 0, 600, 12);
  body.rect(0, 900, 0, 700, 12);
  if (version_two) {
    body.rect(0, 700, 0, 500, 12);
    body.rect(0, 750, 0, 550, 12);
    body.u8(0x02);
  }
  const auto middle = start_edges(morph_styles(version_two));
  body.u32(static_cast<std::uint32_t>(middle.size()));
  body.bytes(middle);
  const auto tail = end_edges();
  body.bytes(tail);
  return body.body;
}

inline std::vector<std::byte> morph_nested_solid() {
  SwfBuilder body;
  body.u8(1);
  body.u8(0x00);
  rgba(body, 1, 2, 3, 4);
  rgba(body, 5, 6, 7, 8);
  body.u8(0);
  body.u8(0x20);
  return body.body;
}

inline std::vector<std::byte> morph_nested_fill_kinds() {
  SwfBuilder body;
  body.u8(3);
  body.u8(0x00);
  rgba(body, 1, 2, 3, 4);
  rgba(body, 5, 6, 7, 8);
  body.u8(0x13);
  identity_matrix(body);
  identity_matrix(body);
  body.u8(0x12);
  body.u8(0);
  rgba(body, 9, 10, 11, 12);
  body.u8(200);
  rgba(body, 13, 14, 15, 16);
  body.u8(0);
  rgba(body, 17, 18, 19, 20);
  body.u8(250);
  rgba(body, 21, 22, 23, 24);
  body.u16(0x0040);
  body.u16(0x0020);
  body.u8(0x42);
  body.u16(9);
  identity_matrix(body);
  identity_matrix(body);
  body.u8(1);
  body.u16(40);
  body.u16(80);
  rgba(body, 25, 26, 27, 28);
  rgba(body, 29, 30, 31, 32);
  body.u8(0x20);
  return body.body;
}

inline void morph_translate_matrix(SwfBuilder& body, std::int32_t x, std::int32_t y) {
  body.push_bits(0, 1);
  body.push_bits(0, 1);
  body.push_bits(6, 5);
  signed_bits(body, x, 6);
  signed_bits(body, y, 6);
  body.align();
}

inline void morph_translate_pair(SwfBuilder& body, std::int32_t x, std::int32_t y) {
  morph_translate_matrix(body, x, y);
  morph_translate_matrix(body, -x, -y);
}

inline std::vector<std::byte> morph_every_kind_styles() {
  SwfBuilder body;
  body.u8(8);
  body.u8(0x00);
  rgba(body, 0x11, 0x22, 0x33, 0x44);
  rgba(body, 0x55, 0x66, 0x77, 0x88);
  body.u8(0x10);
  morph_translate_pair(body, 5, 6);
  body.u8(0x51);
  body.u8(10);
  rgba(body, 1, 2, 3, 4);
  body.u8(20);
  rgba(body, 5, 6, 7, 8);
  body.u8(0x12);
  morph_translate_pair(body, 7, 8);
  body.u8(0xA2);
  body.u8(30);
  rgba(body, 9, 10, 11, 12);
  body.u8(40);
  rgba(body, 13, 14, 15, 16);
  body.u8(50);
  rgba(body, 17, 18, 19, 20);
  body.u8(60);
  rgba(body, 21, 22, 23, 24);
  body.u8(0x13);
  morph_translate_pair(body, 9, 10);
  body.u8(0x51);
  body.u8(70);
  rgba(body, 25, 26, 27, 28);
  body.u8(80);
  rgba(body, 29, 30, 31, 32);
  body.u16(0x0140);
  body.u16(0x00C0);
  const std::uint8_t bitmap_kinds[] = {0x40, 0x41, 0x42, 0x43};
  for (unsigned index = 0; index < 4; ++index) {
    body.u8(bitmap_kinds[index]);
    body.u16(static_cast<std::uint32_t>(21 + index));
    morph_translate_pair(body, static_cast<std::int32_t>(11 + 2 * index),
                         static_cast<std::int32_t>(12 + 2 * index));
  }
  body.u8(2);
  body.u16(40);
  body.u16(80);
  body.u8(0x26);
  body.u8(0x00);
  body.u16(0x0300);
  body.u16(0x0480);
  rgba(body, 0x30, 0x31, 0x32, 0x33);
  rgba(body, 0x34, 0x35, 0x36, 0x37);
  body.u16(5);
  body.u16(10);
  body.u8(0x08);
  body.u8(0x00);
  body.u8(0x10);
  morph_translate_pair(body, 19, 20);
  body.u8(0x22);
  body.u8(90);
  rgba(body, 0x40, 0x41, 0x42, 0x43);
  body.u8(100);
  rgba(body, 0x44, 0x45, 0x46, 0x47);
  body.u8(110);
  rgba(body, 0x48, 0x49, 0x4A, 0x4B);
  body.u8(120);
  rgba(body, 0x4C, 0x4D, 0x4E, 0x4F);
  body.u8(0x43);
  return body.body;
}

inline std::vector<std::byte> morph_every_kind_payload() {
  SwfBuilder body;
  body.u16(16);
  body.rect(0, 500, 0, 400, 11);
  body.rect(0, 520, 0, 420, 11);
  body.rect(0, 480, 0, 380, 11);
  body.rect(0, 500, 0, 400, 11);
  body.u8(0x00);
  const auto middle = start_edges(morph_every_kind_styles());
  body.u32(static_cast<std::uint32_t>(middle.size()));
  body.bytes(middle);
  body.bytes(end_edges());
  return body.body;
}

inline std::vector<std::byte> morph_new_styles_edges(const std::vector<std::byte>& styles_and_bits,
                                              const std::vector<std::byte>& nested) {
  SwfBuilder body;
  body.bytes(styles_and_bits);
  body.push_bits(0, 1);
  body.push_bits(0x10, 5);
  body.align();
  body.bytes(nested);
  body.push_bits(0, 1);
  body.push_bits(0x02, 5);
  body.push_bits(1, 2);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

inline std::vector<std::byte> morph_new_styles_payload(const std::vector<std::byte>& nested) {
  SwfBuilder body;
  body.u16(14);
  body.rect(0, 100, 0, 100, 10);
  body.rect(0, 120, 0, 90, 10);
  const auto start = morph_new_styles_edges(morph_styles(false), nested);
  body.u32(static_cast<std::uint32_t>(start.size()));
  body.bytes(start);
  body.bytes(morph_new_styles_edges({std::byte{0x20}}, nested));
  return body.body;
}

// COLORMAPDATA is one compressed block: the palette must precede the pixel bytes inside the stream.
inline std::vector<std::uint8_t> colour_map_palette(std::uint8_t table_size, bool alpha) {
  std::vector<std::uint8_t> palette;
  for (std::uint32_t index = 0; index <= table_size; ++index) {
    palette.push_back(static_cast<std::uint8_t>(index + 1));
    palette.push_back(static_cast<std::uint8_t>(index + 2));
    palette.push_back(static_cast<std::uint8_t>(index + 3));
    if (alpha) {
      palette.push_back(0xFF);
    }
  }
  return palette;
}

inline std::vector<std::byte> lossless_payload(std::uint16_t format, std::uint16_t width,
                                        std::uint16_t height, std::size_t row_bytes,
                                        std::uint8_t table_size, bool alpha) {
  SwfBuilder body;
  body.u16(3);
  body.u8(format);
  body.u16(width);
  body.u16(height);
  std::vector<std::uint8_t> raw(row_bytes * height, 0x7F);
  if (format == 3) {
    body.u8(table_size);
    const auto palette = colour_map_palette(table_size, alpha);
    raw.insert(raw.begin(), palette.begin(), palette.end());
  }
  body.bytes(zlib_bytes(raw));
  return body.body;
}

inline std::vector<std::byte> lossless_raw_payload(std::uint16_t format, std::uint16_t width,
                                                   std::uint16_t height, std::uint8_t table_size,
                                                   std::span<const std::uint8_t> raw) {
  SwfBuilder body;
  body.u16(3);
  body.u8(format);
  body.u16(width);
  body.u16(height);
  if (format == 3) {
    body.u8(table_size);
  }
  body.bytes(zlib_bytes(raw));
  return body.body;
}

struct PngChunk {
  std::string type;
  std::span<const std::uint8_t> data;
  std::uint32_t stored_crc{0};
  std::uint32_t computed_crc{0};
};

inline std::uint32_t png_u32(std::span<const std::uint8_t> bytes, std::size_t at) {
  return (std::uint32_t{bytes[at]} << 24) | (std::uint32_t{bytes[at + 1]} << 16) |
         (std::uint32_t{bytes[at + 2]} << 8) | std::uint32_t{bytes[at + 3]};
}

inline std::vector<PngChunk> png_chunks(std::span<const std::uint8_t> png) {
  std::vector<PngChunk> chunks;
  std::size_t at = 8;
  while (at + 12 <= png.size() && at + 12 + png_u32(png, at) <= png.size()) {
    const auto length = png_u32(png, at);
    const auto data = png.subspan(at + 8, length);
    auto crc = crc32(0L, png.data() + at + 4, 4);
    crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
    chunks.push_back({std::string(png.begin() + at + 4, png.begin() + at + 8), data,
                      png_u32(png, at + 8 + length), static_cast<std::uint32_t>(crc)});
    at += 12 + length;
  }
  return chunks;
}

// A JPEG marker length must be big-endian; every SWF field around it is little-endian.
inline void be16(SwfBuilder& builder, std::uint32_t value) {
  builder.u8((value >> 8) & 0xFFu);
  builder.u8(value & 0xFFu);
}

inline std::vector<std::byte> minimal_jpeg(std::uint16_t width, std::uint16_t height) {
  SwfBuilder body;
  body.u8(0xFF);
  body.u8(0xD8);
  body.u8(0xFF);
  body.u8(0xC0);
  be16(body, 11);
  body.u8(8);
  be16(body, height);
  be16(body, width);
  body.u8(1);
  body.u8(1);
  body.u8(0x11);
  body.u8(0);
  body.u8(0xFF);
  body.u8(0xD9);
  return body.body;
}

inline std::vector<std::byte> jpeg3_payload(std::span<const std::byte> image, std::size_t alpha_pixels) {
  SwfBuilder body;
  body.u16(4);
  body.u32(static_cast<std::uint32_t>(image.size()));
  body.bytes(image);
  std::vector<std::uint8_t> plane(alpha_pixels, 0x5A);
  body.bytes(zlib_bytes(plane));
  return body.body;
}

}  // namespace swf_art_test
