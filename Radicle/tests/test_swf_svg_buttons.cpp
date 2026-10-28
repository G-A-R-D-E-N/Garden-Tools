#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_frame_svg.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

const std::string kStage =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n";

const std::string kRedSquare =
    "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#ff0000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

const std::string kBlueSquare =
    "<path d=\"M0 0L30 0L30 30L0 30L0 0Z\" fill=\"#0000ff\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n";

constexpr std::uint16_t kButtonId = 3;

struct Record {
  std::uint8_t flags;
  std::uint16_t character;
  std::uint16_t depth;
  std::int32_t x;
  std::int32_t y;
};

std::vector<std::byte> square_shape(std::uint16_t id, std::int32_t size, std::uint32_t rgb) {
  SwfBuilder body;
  body.u16(id);
  body.rect(0, size, 0, size, 12);
  body.u8(1);
  swf_art_test::solid_fill(body, false, rgb);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x03, 5);
  body.push_bits(12, 5);
  body.push_bits(0, 12);
  body.push_bits(0, 12);
  body.push_bits(1, 1);
  const std::int32_t edges[8] = {size, 0, 0, size, -size, 0, 0, -size};
  for (std::size_t index = 0; index < 8; index += 2) {
    body.push_bits(1, 1);
    body.push_bits(1, 1);
    body.push_bits(10, 4);
    body.push_bits(1, 1);
    body.push_bits(static_cast<std::uint32_t>(edges[index]) & 0xFFFu, 12);
    body.push_bits(static_cast<std::uint32_t>(edges[index + 1]) & 0xFFFu, 12);
  }
  body.push_bits(0, 6);
  body.align();
  return body.body;
}

void translate_matrix(SwfBuilder& body, std::int32_t x, std::int32_t y) {
  body.push_bits(0, 1);
  body.push_bits(0, 1);
  body.push_bits(10, 5);
  swf_art_test::signed_bits(body, x, 10);
  swf_art_test::signed_bits(body, y, 10);
  body.align();
}

void half_colour_transform(SwfBuilder& body) {
  body.push_bits(0, 1);
  body.push_bits(1, 1);
  body.push_bits(9, 4);
  for (int channel = 0; channel < 4; ++channel) {
    body.push_bits(128, 9);
  }
  body.align();
}

std::vector<std::byte> button_payload(std::uint16_t code, const std::vector<Record>& records) {
  SwfBuilder body;
  body.u16(kButtonId);
  if (code == fo4::swf::tag::kDefineButton2) {
    body.u8(0);
    body.u16(0);
  }
  for (const Record& record : records) {
    body.u8(record.flags);
    body.u16(record.character);
    body.u16(record.depth);
    translate_matrix(body, record.x, record.y);
    if (code == fo4::swf::tag::kDefineButton2) {
      half_colour_transform(body);
    }
  }
  body.u8(0);
  return body.body;
}

std::vector<std::byte> button_movie(std::uint16_t code, const std::vector<Record>& records) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(2, square_shape(1, 1000, 0xFF0000u), false);
  file.tag(2, square_shape(2, 600, 0x0000FFu), false);
  file.tag(code, button_payload(code, records), false);
  SwfBuilder place;
  place.u8(0x02);
  place.u16(1);
  place.u16(kButtonId);
  file.tag(26, place.body, false);
  file.tag(1);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

void frame_svg_of(std::uint16_t code, const std::vector<Record>& records, std::string& svg) {
  const auto result = fo4::swf::read_swf(button_movie(code, records));
  ASSERT_TRUE(result.ok());
  const auto display = fo4::swf::build_display_list(result.file);
  ASSERT_EQ(display.status, fo4::swf::SwfTimelineStatus::ok);
  svg = fo4::swf::frame_to_svg(result.file, display, 0);
}

}  // namespace

TEST(SwfFrameSvgButtons, DefineButton2UpRecordDrawsItsShapeAtItsMatrix) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton2, {{0x01, 1, 1, 200, 100}}, svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 10 5)\">\n" + kRedSquare + "</g>\n</svg>\n");
}

TEST(SwfFrameSvgButtons, HitOnlyRecordDrawsNothing) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton2, {{0x08, 1, 1, 0, 0}}, svg);
  EXPECT_EQ(svg, kStage + "</svg>\n");
}

TEST(SwfFrameSvgButtons, UpRecordsDrawInDepthOrderAndIgnoreTheirColourTransforms) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton2, {{0x01, 1, 5, 0, 0}, {0x01, 2, 3, 400, 0}}, svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 20 0)\">\n" + kBlueSquare + "</g>\n" +
                "<g transform=\"matrix(1 0 0 1 0 0)\">\n" + kRedSquare + "</g>\n" + "</svg>\n");
}

TEST(SwfFrameSvgButtons, DefineButtonRecordHasNoColourTransformBytes) {
  std::string svg;
  frame_svg_of(fo4::swf::tag::kDefineButton, {{0x01, 1, 1, 200, 100}}, svg);
  EXPECT_EQ(svg,
            kStage + "<g transform=\"matrix(1 0 0 1 10 5)\">\n" + kRedSquare + "</g>\n</svg>\n");
}
