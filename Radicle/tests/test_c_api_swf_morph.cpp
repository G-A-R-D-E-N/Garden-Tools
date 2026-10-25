#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

void c_api_identity(SwfBuilder& body) {
  body.push_bits(0, 1);
  body.push_bits(0, 1);
  body.push_bits(0, 5);
  body.align();
}

std::vector<std::byte> morph_style_payload() {
  SwfBuilder start;
  start.u8(2);
  start.u8(0x00);
  start.u8(1);
  start.u8(2);
  start.u8(3);
  start.u8(4);
  start.u8(5);
  start.u8(6);
  start.u8(7);
  start.u8(8);
  start.u8(0x13);
  c_api_identity(start);
  c_api_identity(start);
  start.u8(0x11);
  start.u8(0);
  start.u8(9);
  start.u8(10);
  start.u8(11);
  start.u8(12);
  start.u8(255);
  start.u8(13);
  start.u8(14);
  start.u8(15);
  start.u8(16);
  start.u16(0x0100);
  start.u16(0x0200);
  start.u8(2);
  start.u16(40);
  start.u16(80);
  start.u8(0x28);
  start.u8(0x00);
  start.u16(0x0300);
  start.u16(0x0400);
  start.u8(0x00);
  start.u8(17);
  start.u8(18);
  start.u8(19);
  start.u8(20);
  start.u8(21);
  start.u8(22);
  start.u8(23);
  start.u8(24);
  start.u16(10);
  start.u16(20);
  start.u8(0x00);
  start.u8(0x00);
  start.u8(25);
  start.u8(26);
  start.u8(27);
  start.u8(28);
  start.u8(29);
  start.u8(30);
  start.u8(31);
  start.u8(32);
  start.u8(0x10);
  start.push_bits(0, 6);
  start.align();
  SwfBuilder body;
  body.u16(55);
  body.rect(0, 100, 0, 80, 9);
  body.rect(0, 120, 0, 90, 9);
  body.rect(0, 90, 0, 70, 9);
  body.rect(0, 110, 0, 80, 9);
  body.u8(0x02);
  body.u32(static_cast<std::uint32_t>(start.body.size()));
  body.bytes(start.body);
  body.u8(0x10);
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

std::vector<std::byte> morph_movie(std::span<const std::byte> payload) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(84, payload, false);
  file.tag(9, std::vector<std::byte>(4, std::byte{0}), false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

std::vector<std::byte> two_morph_movie() {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(84, swf_art_test::morph_payload(true), false);
  file.tag(84, morph_style_payload(), false);
  file.tag(9, std::vector<std::byte>(4, std::byte{0}), false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

}  // namespace

TEST(CApiSwfTest, ReadsTheMorphStylePairs) {
  const auto file = open(morph_movie(morph_style_payload()));
  ASSERT_NE(file, nullptr);

  radicle_swf_morph_shape morph{};
  ASSERT_EQ(radicle_swf_morph_at(file.get(), 0, &morph), RADICLE_STATUS_OK);
  EXPECT_EQ(morph.character_id, 55);
  EXPECT_EQ(morph.version, 2);
  EXPECT_EQ(morph.fill_style_count, 2u);
  EXPECT_EQ(morph.line_style_count, 2u);

  radicle_swf_morph_fill_style fill{};
  ASSERT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 0, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x00);
  EXPECT_EQ(fill.start_color, 0x04010203u);
  EXPECT_EQ(fill.end_color, 0x08050607u);
  EXPECT_EQ(fill.has_start_matrix, 0);
  EXPECT_EQ(fill.stop_count, 0u);

  ASSERT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 1, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x13);
  EXPECT_EQ(fill.has_start_matrix, 1);
  EXPECT_EQ(fill.start_matrix[0], 0x10000);
  EXPECT_EQ(fill.has_end_matrix, 1);
  EXPECT_EQ(fill.spread, 0);
  EXPECT_EQ(fill.has_start_focal, 1);
  EXPECT_EQ(fill.start_focal, 0x0100);
  EXPECT_EQ(fill.has_end_focal, 1);
  EXPECT_EQ(fill.end_focal, 0x0200);
  ASSERT_EQ(fill.stop_count, 1u);

  radicle_swf_morph_gradient_stop stop{};
  ASSERT_EQ(radicle_swf_morph_fill_stop_at(file.get(), 0, 1, 0, &stop), RADICLE_STATUS_OK);
  EXPECT_EQ(stop.start_ratio, 0);
  EXPECT_EQ(stop.end_ratio, 255);
  EXPECT_EQ(stop.start_color, 0x0C090A0Bu);
  EXPECT_EQ(stop.end_color, 0x100D0E0Fu);

  radicle_swf_morph_line_style line{};
  ASSERT_EQ(radicle_swf_morph_line_at(file.get(), 0, 0, &line), RADICLE_STATUS_OK);
  EXPECT_EQ(line.start_width, 40);
  EXPECT_EQ(line.end_width, 80);
  EXPECT_EQ(line.flags, 0x2800);
  EXPECT_EQ(line.has_fill, 1);
  EXPECT_EQ(line.has_start_miter, 1);
  EXPECT_EQ(line.start_miter, 0x0300);
  EXPECT_EQ(line.has_end_miter, 1);
  EXPECT_EQ(line.end_miter, 0x0400);

  ASSERT_EQ(radicle_swf_morph_line_fill_at(file.get(), 0, 0, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x00);
  EXPECT_EQ(fill.start_color, 0x14111213u);
  EXPECT_EQ(fill.end_color, 0x18151617u);

  ASSERT_EQ(radicle_swf_morph_line_at(file.get(), 0, 1, &line), RADICLE_STATUS_OK);
  EXPECT_EQ(line.start_width, 10);
  EXPECT_EQ(line.end_width, 20);
  EXPECT_EQ(line.has_fill, 0);
  EXPECT_EQ(line.has_start_miter, 0);
  EXPECT_EQ(line.start_color, 0x1C191A1Bu);
  EXPECT_EQ(line.end_color, 0x201D1E1Fu);

  EXPECT_EQ(radicle_swf_morph_line_fill_at(file.get(), 0, 1, &fill), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 2, &fill), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_fill_stop_at(file.get(), 0, 1, 1, &stop),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_line_at(file.get(), 0, 2, &line), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_fill_at(file.get(), 1, 0, &fill), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_morph_line_at(nullptr, 0, 0, &line), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfTest, ReadsEveryMorphFillKindThroughTheGetters) {
  const auto file = open(morph_movie(swf_art_test::morph_every_kind_payload()));
  ASSERT_NE(file, nullptr);

  radicle_swf_morph_shape morph{};
  ASSERT_EQ(radicle_swf_morph_at(file.get(), 0, &morph), RADICLE_STATUS_OK);
  EXPECT_EQ(morph.fill_style_count, 8u);
  EXPECT_EQ(morph.line_style_count, 2u);

  radicle_swf_morph_fill_style fill{};
  radicle_swf_morph_gradient_stop stop{};

  ASSERT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 1, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x10);
  EXPECT_EQ(fill.start_color, 0u);
  EXPECT_EQ(fill.has_start_matrix, 1);
  EXPECT_EQ(fill.start_matrix[0], 0x10000);
  EXPECT_EQ(fill.start_matrix[4], 5);
  EXPECT_EQ(fill.start_matrix[5], 6);
  EXPECT_EQ(fill.has_end_matrix, 1);
  EXPECT_EQ(fill.end_matrix[4], -5);
  EXPECT_EQ(fill.spread, 1);
  EXPECT_EQ(fill.interpolation, 1);
  EXPECT_EQ(fill.has_start_focal, 0);
  ASSERT_EQ(fill.stop_count, 1u);
  ASSERT_EQ(radicle_swf_morph_fill_stop_at(file.get(), 0, 1, 0, &stop), RADICLE_STATUS_OK);
  EXPECT_EQ(stop.start_ratio, 10);
  EXPECT_EQ(stop.start_color, 0x04010203u);
  EXPECT_EQ(stop.end_ratio, 20);
  EXPECT_EQ(stop.end_color, 0x08050607u);

  ASSERT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 2, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x12);
  EXPECT_EQ(fill.spread, 2);
  EXPECT_EQ(fill.interpolation, 2);
  EXPECT_EQ(fill.end_matrix[4], -7);
  ASSERT_EQ(fill.stop_count, 2u);
  ASSERT_EQ(radicle_swf_morph_fill_stop_at(file.get(), 0, 2, 1, &stop), RADICLE_STATUS_OK);
  EXPECT_EQ(stop.start_ratio, 50);
  EXPECT_EQ(stop.start_color, 0x14111213u);
  EXPECT_EQ(stop.end_color, 0x18151617u);

  ASSERT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 3, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x13);
  EXPECT_EQ(fill.has_start_focal, 1);
  EXPECT_EQ(fill.start_focal, 0x0140);
  EXPECT_EQ(fill.has_end_focal, 1);
  EXPECT_EQ(fill.end_focal, 0x00C0);
  EXPECT_EQ(fill.start_matrix[4], 9);

  struct BitmapKind {
    std::uint8_t kind;
    std::uint16_t id;
  };
  const BitmapKind bitmap_kinds[] = {{0x40, 21}, {0x41, 22}, {0x42, 23}, {0x43, 24}};
  for (std::size_t index = 0; index < 4; ++index) {
    ASSERT_EQ(radicle_swf_morph_fill_at(file.get(), 0, 4 + index, &fill), RADICLE_STATUS_OK);
    EXPECT_EQ(fill.kind, bitmap_kinds[index].kind);
    EXPECT_EQ(fill.bitmap_id, bitmap_kinds[index].id);
    EXPECT_EQ(fill.has_start_matrix, 1);
    EXPECT_EQ(fill.has_end_matrix, 1);
    EXPECT_EQ(fill.stop_count, 0u);
    EXPECT_EQ(fill.has_start_focal, 0);
  }

  radicle_swf_morph_line_style line{};
  ASSERT_EQ(radicle_swf_morph_line_at(file.get(), 0, 0, &line), RADICLE_STATUS_OK);
  EXPECT_EQ(line.flags, 0x2600);
  EXPECT_EQ(line.has_fill, 0);
  EXPECT_EQ(line.has_start_miter, 1);
  EXPECT_EQ(line.start_miter, 0x0300);
  EXPECT_EQ(line.has_end_miter, 1);
  EXPECT_EQ(line.end_miter, 0x0480);
  EXPECT_EQ(line.start_color, 0x33303132u);
  EXPECT_EQ(line.end_color, 0x37343536u);
  EXPECT_EQ(radicle_swf_morph_line_fill_at(file.get(), 0, 0, &fill), RADICLE_STATUS_NOT_FOUND);

  ASSERT_EQ(radicle_swf_morph_line_at(file.get(), 0, 1, &line), RADICLE_STATUS_OK);
  EXPECT_EQ(line.has_fill, 1);
  EXPECT_EQ(line.has_start_miter, 0);
  EXPECT_EQ(line.start_color, 0u);
  ASSERT_EQ(radicle_swf_morph_line_fill_at(file.get(), 0, 1, &fill), RADICLE_STATUS_OK);
  EXPECT_EQ(fill.kind, 0x10);
  EXPECT_EQ(fill.start_matrix[4], 19);
  EXPECT_EQ(fill.end_matrix[4], -19);
  EXPECT_EQ(fill.spread, 0);
  EXPECT_EQ(fill.interpolation, 2);
  ASSERT_EQ(fill.stop_count, 2u);

  ASSERT_EQ(radicle_swf_morph_line_fill_stop_at(file.get(), 0, 1, 0, &stop), RADICLE_STATUS_OK);
  EXPECT_EQ(stop.start_ratio, 90);
  EXPECT_EQ(stop.start_color, 0x43404142u);
  EXPECT_EQ(stop.end_ratio, 100);
  EXPECT_EQ(stop.end_color, 0x47444546u);
  ASSERT_EQ(radicle_swf_morph_line_fill_stop_at(file.get(), 0, 1, 1, &stop), RADICLE_STATUS_OK);
  EXPECT_EQ(stop.start_ratio, 110);
  EXPECT_EQ(stop.end_ratio, 120);
  EXPECT_EQ(stop.end_color, 0x4F4C4D4Eu);

  EXPECT_EQ(radicle_swf_morph_line_fill_stop_at(file.get(), 0, 1, 2, &stop),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_line_fill_stop_at(file.get(), 0, 0, 0, &stop),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_morph_line_fill_stop_at(file.get(), 0, 2, 0, &stop),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_line_fill_stop_at(nullptr, 0, 1, 0, &stop),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfTest, InterpolatesAMorphFrameAtARatio) {
  const auto file = open(morph_movie(swf_art_test::morph_payload(true)));
  ASSERT_NE(file, nullptr);

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_morph_frame_count(file.get(), 0, 0.5, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);

  radicle_swf_shape_record record{};
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.5, 0, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_MOVE);
  EXPECT_EQ(record.x, 0);
  EXPECT_EQ(record.y, 0);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.5, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_STRAIGHT_EDGE);
  EXPECT_EQ(record.x, 0);
  EXPECT_EQ(record.y, 0);

  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, -1.0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, 4);
  EXPECT_EQ(record.y, 6);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 2.0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, -4);
  EXPECT_EQ(record.y, -6);

  EXPECT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.5, count, &record),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_frame_count(file.get(), 0, 0.5, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_frame_count(nullptr, 0, 0.5, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_morph_frame_record_at(file.get(), 1, 0.5, 0, &record),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_morph_frame_count(file.get(), 1, 0.5, &count), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_morph_frame_record_at(nullptr, 0, 0.5, 0, &record),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfTest, RebuildsTheFrameWhenTheTagOrRatioChanges) {
  const auto file = open(two_morph_movie());
  ASSERT_NE(file, nullptr);

  std::size_t count = 0;
  radicle_swf_shape_record record{};

  ASSERT_EQ(radicle_swf_morph_frame_count(file.get(), 0, 0.5, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);

  ASSERT_EQ(radicle_swf_morph_frame_count(file.get(), 1, 0.5, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_swf_morph_frame_record_at(file.get(), 1, 0.5, 0, &record),
            RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_swf_morph_frame_count(file.get(), 0, 0.5, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.5, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.kind, RADICLE_SWF_RECORD_STRAIGHT_EDGE);
  EXPECT_EQ(record.x, 0);
  EXPECT_EQ(record.y, 0);

  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 1.0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, -4);
  EXPECT_EQ(record.y, -6);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, 4);
  EXPECT_EQ(record.y, 6);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.5, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, 0);
  EXPECT_EQ(record.y, 0);

  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 3.0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, -4);
  EXPECT_EQ(record.y, -6);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, -2.0, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, 4);
  EXPECT_EQ(record.y, 6);

  ASSERT_EQ(radicle_swf_morph_frame_count(file.get(), 0, 1.0, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);
  ASSERT_EQ(radicle_swf_morph_frame_record_at(file.get(), 0, 0.5, 1, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(record.x, 0);
}
