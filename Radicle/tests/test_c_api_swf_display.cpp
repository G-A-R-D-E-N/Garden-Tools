#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_frame_svg.hpp"
#include "fo4/swf/swf_bitmap_png.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using swf_test::SwfBuilder;
using swf_test::wrap_body;

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

struct FramesDeleter {
  void operator()(radicle_swf_frames* frames) const { radicle_swf_frames_destroy(frames); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;
using FramesHandle = std::unique_ptr<radicle_swf_frames, FramesDeleter>;

struct LayoutDeleter {
  void operator()(radicle_swf_layout* layout) const { radicle_swf_layout_destroy(layout); }
};

using LayoutHandle = std::unique_ptr<radicle_swf_layout, LayoutDeleter>;

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

std::vector<std::byte> matrix_scale_translate(std::int32_t scale, std::int32_t x,
                                              std::int32_t y) {
  SwfBuilder bits;
  bits.push_bits(1, 1);
  bits.push_bits(19, 5);
  bits.push_bits(static_cast<std::uint32_t>(scale), 19);
  bits.push_bits(static_cast<std::uint32_t>(scale), 19);
  bits.push_bits(0, 1);
  bits.push_bits(11, 5);
  bits.push_bits(static_cast<std::uint32_t>(x) & 0x7FFu, 11);
  bits.push_bits(static_cast<std::uint32_t>(y) & 0x7FFu, 11);
  bits.align();
  return bits.body;
}

std::vector<std::byte> place2(std::uint8_t flags, std::uint16_t depth, std::uint16_t character,
                              const std::vector<std::byte>& matrix, const std::string& name) {
  SwfBuilder body;
  body.u8(flags);
  body.u16(depth);
  body.u16(character);
  if (!matrix.empty()) {
    body.bytes(matrix);
  }
  if (!name.empty()) {
    body.text(name);
  }
  return body.body;
}

std::vector<std::byte> nested_movie() {
  SwfBuilder sprite;
  sprite.u16(9);
  sprite.u16(1);
  const auto child = place2(0x26, 1, 5, matrix_scale_translate(0x20000, 200, 100), "Cell");
  sprite.tag(26, child, false);
  sprite.tag(1);
  sprite.tag(0);

  const auto art = place2(0x26, 1, 9, matrix_scale_translate(0x8000, 100, 50), "Art");
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(32, swf_art_test::define_shape_3_payload(), false);
  body.tag(39, sprite.body, false);
  body.tag(26, art, false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

std::vector<std::byte> unplaceable_movie() {
  SwfBuilder move;
  move.u8(0x05);
  move.u16(3);
  move.push_bits(0, 1);
  move.push_bits(0, 1);
  move.push_bits(11, 5);
  move.push_bits(0, 11);
  move.push_bits(0, 11);
  move.align();
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(26, move.body, false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

std::vector<std::byte> red_square_shape() {
  SwfBuilder body;
  body.u16(1);
  body.rect(0, 1000, 0, 1000, 12);
  body.u8(1);
  body.u8(0x00);
  body.u8(0xFF);
  body.u8(0x00);
  body.u8(0x00);
  body.u8(0xFF);
  body.u8(0);
  body.u8(0x10);
  body.push_bits(0, 1);
  body.push_bits(0x03, 5);
  body.push_bits(12, 5);
  body.push_bits(0, 12);
  body.push_bits(0, 12);
  body.push_bits(1, 1);
  const std::int32_t edges[8] = {1000, 0, 0, 1000, -1000, 0, 0, -1000};
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

std::vector<std::byte> red_square_movie() {
  SwfBuilder body;
  body.rect(0, 11000, 0, 8000, 15);
  body.u16(0x0C00);
  body.u16(1);
  body.tag(32, red_square_shape(), false);
  body.tag(26, place2(0x06, 1, 1, matrix_scale_translate(0x10000, 0, 0), {}), false);
  body.tag(1);
  body.tag(0);
  return wrap_body(body.body, 10, false, false);
}

std::vector<std::byte> png_movie() {
  const std::vector<std::uint8_t> argb = {255, 10, 20, 30, 128, 64, 100, 32,
                                          0,   0,  0,  0,  170, 3,  170, 85};
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(1);
  file.tag(36, swf_art_test::lossless_raw_payload(5, 2, 2, 0, argb), false);
  file.tag(20, swf_art_test::lossless_payload(4, 2, 2, 4, 0, false), false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

std::vector<std::byte> jpeg_bytes(std::initializer_list<std::uint8_t> values) {
  std::vector<std::byte> out;
  for (const auto value : values) out.push_back(static_cast<std::byte>(value));
  return out;
}

std::vector<std::byte> defined_bits(std::uint16_t character, const std::vector<std::byte>& image) {
  SwfBuilder body;
  body.u16(character);
  body.bytes(image);
  return body.body;
}

std::vector<std::byte> with_leading_seam(const std::vector<std::byte>& image) {
  auto out = jpeg_bytes({0xFF, 0xD9, 0xFF, 0xD8});
  out.insert(out.end(), image.begin(), image.end());
  return out;
}

std::vector<std::byte> tables_movie(std::uint16_t code, const std::vector<std::byte>& image) {
  SwfBuilder file;
  file.rect(0, 11000, 0, 8000, 15);
  file.u16(0x0C00);
  file.u16(1);
  file.tag(8, jpeg_bytes({0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xD9}), false);
  file.tag(code, defined_bits(7, image), false);
  file.tag(0);
  return wrap_body(file.body, 10, false, false);
}

std::vector<std::uint8_t> as_u8(const std::vector<std::byte>& bytes) {
  std::vector<std::uint8_t> out;
  for (const auto byte : bytes) out.push_back(std::to_integer<std::uint8_t>(byte));
  return out;
}

std::vector<std::uint8_t> exported_jpeg(const radicle_swf* swf, size_t index) {
  uint8_t* jpeg = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_swf_bitmap_jpeg(swf, index, &jpeg, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::uint8_t> bytes(jpeg, jpeg + size);
  radicle_free_bytes(jpeg);
  return bytes;
}

std::vector<std::uint8_t> hand_spliced_jpeg() {
  return {0xFF, 0xD8, 0xAA, 0xBB, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x03, 0x00, 0x02, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xD9};
}

}  // namespace

TEST(CApiSwfFrames, ExposesPerFrameInstancesAndWorldBounds) {
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  FramesHandle frames(raw);
  ASSERT_NE(frames, nullptr);
  EXPECT_EQ(radicle_abi_version(), RADICLE_ABI_VERSION);
  ASSERT_EQ(radicle_swf_frames_timeline_count(frames.get()), 2u);

  std::uint16_t root_id = 1;
  std::size_t root_frames = 0;
  ASSERT_EQ(radicle_swf_frames_timeline_at(frames.get(), 0, &root_id, &root_frames),
            RADICLE_STATUS_OK);
  EXPECT_EQ(root_id, 0);
  EXPECT_EQ(root_frames, 1u);
  std::uint16_t sprite_id = 0;
  std::size_t sprite_frames = 0;
  ASSERT_EQ(radicle_swf_frames_timeline_at(frames.get(), 1, &sprite_id, &sprite_frames),
            RADICLE_STATUS_OK);
  EXPECT_EQ(sprite_id, 9);
  EXPECT_EQ(sprite_frames, 1u);

  std::size_t count = 0;
  ASSERT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 0, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 2u);

  radicle_swf_frames_instance art{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 0, &art), RADICLE_STATUS_OK);
  ASSERT_NE(art.path, nullptr);
  EXPECT_EQ(std::string(art.path), "Art");
  EXPECT_EQ(art.is_sprite, 1);
  EXPECT_EQ(art.has_bounds, 0);
  EXPECT_NE(art.instance_id, 0u);

  radicle_swf_frames_instance cell{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 1, &cell), RADICLE_STATUS_OK);
  ASSERT_NE(cell.path, nullptr);
  ASSERT_NE(cell.name, nullptr);
  EXPECT_EQ(std::string(cell.path), "Art/Cell");
  EXPECT_EQ(std::string(cell.name), "Cell");
  EXPECT_EQ(cell.depth, 1);
  EXPECT_EQ(cell.character_id, 5);
  EXPECT_EQ(cell.timeline_id, 9);
  EXPECT_EQ(cell.timeline_frame, 0u);
  EXPECT_EQ(cell.has_bounds, 1);
  EXPECT_EQ(cell.has_clip_depth, 0);
  EXPECT_DOUBLE_EQ(cell.world_transform.scale_x, 1.0);
  EXPECT_DOUBLE_EQ(cell.world_transform.translate_x, 10.0);
  EXPECT_DOUBLE_EQ(cell.world_transform.translate_y, 5.0);
  EXPECT_DOUBLE_EQ(cell.bounds.x, 10.0);
  EXPECT_DOUBLE_EQ(cell.bounds.y, 5.0);
  EXPECT_DOUBLE_EQ(cell.bounds.width, 100.0);
  EXPECT_DOUBLE_EQ(cell.bounds.height, 50.0);
}

TEST(CApiSwfFrames, ExposesFiltersAndDefaultEffects) {
  auto swf = open(swf_test::filtered_placement_movie(true));
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  FramesHandle frames(raw);
  radicle_swf_frames_effects effects{};
  ASSERT_EQ(radicle_swf_frames_instance_effects(frames.get(), 0, 0, 0, &effects), RADICLE_STATUS_OK);
  EXPECT_EQ(effects.has_color_transform, 0);
  EXPECT_EQ(effects.color_multiply[0], 256);
  EXPECT_EQ(effects.world_visible, 1);
  ASSERT_EQ(effects.filter_count, 2u);
  radicle_swf_frames_filter shadow{};
  radicle_swf_frames_filter blur{};
  ASSERT_EQ(radicle_swf_frames_instance_filter_at(frames.get(), 0, 0, 0, 0, &shadow), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_swf_frames_instance_filter_at(frames.get(), 0, 0, 0, 1, &blur), RADICLE_STATUS_OK);
  EXPECT_EQ(shadow.id, 0);
  EXPECT_EQ(blur.id, 1);
  EXPECT_EQ(shadow.color, 0x40102030u);
  EXPECT_DOUBLE_EQ(shadow.blur_x, 2.0);
  EXPECT_DOUBLE_EQ(shadow.blur_y, 3.0);
  EXPECT_DOUBLE_EQ(shadow.distance, 4.0);
  EXPECT_DOUBLE_EQ(shadow.strength, 1.5);
  EXPECT_EQ(shadow.passes, 1);
  EXPECT_DOUBLE_EQ(blur.blur_x, 5.0);
  EXPECT_EQ(blur.passes, 2);
  radicle_swf_frames_filter none{};
  EXPECT_EQ(radicle_swf_frames_instance_filter_at(frames.get(), 0, 0, 0, 2, &none),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_frames_instance_effects(frames.get(), 0, 0, 5, &effects),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfFrames, OutlivesTheMovieItWasBuiltFrom) {
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_frames* raw = nullptr;
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK);
  FramesHandle frames(raw);
  swf.reset();

  radicle_swf_frames_instance cell{};
  ASSERT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 1, &cell), RADICLE_STATUS_OK);
  ASSERT_NE(cell.path, nullptr);
  EXPECT_EQ(std::string(cell.path), "Art/Cell");
  std::uint16_t character_id = 0;
  std::size_t frame_count = 0;
  EXPECT_EQ(radicle_swf_frames_timeline_at(frames.get(), 1, &character_id, &frame_count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(character_id, 9);
}

TEST(CApiSwfFrames, RejectsBadArgumentsAndUnplaceableMovies) {
  radicle_swf_frames* raw = nullptr;
  EXPECT_EQ(radicle_swf_frames_open(nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(radicle_swf_frames_open(swf.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_swf_frames_open(swf.get(), &raw), RADICLE_STATUS_OK);
  FramesHandle frames(raw);

  EXPECT_EQ(radicle_swf_frames_timeline_count(nullptr), 0u);
  std::uint16_t character_id = 0;
  std::size_t frame_count = 0;
  EXPECT_EQ(radicle_swf_frames_timeline_at(frames.get(), 2, &character_id, &frame_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("range"), std::string::npos);
  std::size_t count = 0;
  EXPECT_EQ(radicle_swf_frames_instance_count(frames.get(), 0, 1, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_swf_frames_instance instance{};
  EXPECT_EQ(radicle_swf_frames_instance_at(frames.get(), 0, 0, 2, &instance),
            RADICLE_STATUS_INVALID_ARGUMENT);

  auto broken = open(unplaceable_movie());
  ASSERT_NE(broken, nullptr);
  radicle_swf_frames* out = nullptr;
  EXPECT_EQ(radicle_swf_frames_open(broken.get(), &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("depth"), std::string::npos)
      << radicle_last_error();
}

TEST(CApiSwfFrameSvg, ExportsTheRootFrameAsTheFrameToSvgDocument) {
  const auto bytes = red_square_movie();
  auto swf = open(bytes);
  ASSERT_NE(swf, nullptr);
  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_swf_frame_svg(swf.get(), 0, &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(text, nullptr);
  const std::string svg(reinterpret_cast<const char*>(text), size);
  EXPECT_EQ(text[size], 0);
  radicle_free_bytes(text);

  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok());
  const auto display = fo4::swf::build_display_list(result.file);
  EXPECT_EQ(svg, fo4::swf::frame_to_svg(result.file, display, 0));
  const std::string expected =
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n"
      "<g transform=\"matrix(1 0 0 1 0 0)\">\n"
      "<path d=\"M0 0L50 0L50 50L0 50L0 0Z\" fill=\"#ff0000\" fill-opacity=\"1\" fill-rule=\"nonzero\"/>\n"
      "</g>\n"
      "</svg>\n";
  EXPECT_EQ(svg, expected);
}

TEST(CApiSwfFrameSvg, FrameBeyondTheRootTimelineIsAnEmptyStageNotARefusal) {
  auto swf = open(red_square_movie());
  ASSERT_NE(swf, nullptr);
  std::uint8_t* text = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_swf_frame_svg(swf.get(), 3, &text, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(text, nullptr);
  const std::string svg(reinterpret_cast<const char*>(text), size);
  radicle_free_bytes(text);
  EXPECT_EQ(svg,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 550 400\" width=\"550\" height=\"400\">\n"
            "</svg>\n");
}

TEST(CApiSwfFrameSvg, RefusesNullArgumentsWithOutputsCleared) {
  auto swf = open(red_square_movie());
  ASSERT_NE(swf, nullptr);
  std::uint8_t sentinel = 0;
  std::uint8_t* text = &sentinel;
  std::size_t size = 99;
  EXPECT_EQ(radicle_swf_frame_svg(nullptr, 0, &text, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(size, 0u);

  text = &sentinel;
  size = 99;
  EXPECT_EQ(radicle_swf_frame_svg(swf.get(), 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(size, 0u);

  text = &sentinel;
  EXPECT_EQ(radicle_swf_frame_svg(swf.get(), 0, &text, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
}

// Invariant: the exported PNG is the byte stream bitmap_to_png encodes for the same tag, never a second encoder's output.
TEST(CApiSwfBitmapPng, ExportsTheCppEncoderBytesForALossless2Bitmap) {
  const auto movie = png_movie();
  const auto file = open(movie);
  ASSERT_NE(file, nullptr);
  const auto read = fo4::swf::read_swf(movie);
  ASSERT_TRUE(read.ok());
  const auto expected = fo4::swf::bitmap_to_png(*read.file.tags[1].bitmap);
  ASSERT_TRUE(expected.has_value());

  uint8_t* png = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_swf_bitmap_png(file.get(), 1, &png, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::vector<std::uint8_t> actual(png, png + size);
  radicle_free_bytes(png);
  EXPECT_EQ(actual, *expected);
  const std::vector<std::uint8_t> signature = {137, 80, 78, 71, 13, 10, 26, 10};
  ASSERT_GE(actual.size(), signature.size());
  EXPECT_EQ(std::vector<std::uint8_t>(actual.begin(), actual.begin() + 8), signature);
}

// Invariant: a refused PNG export clears both outputs, and a tag with no bitmap or a bitmap without a PNG export never yields bytes.
TEST(CApiSwfBitmapPng, RefusesNonBitmapAndUnencodableTagsWithOutputsCleared) {
  const auto file = open(png_movie());
  ASSERT_NE(file, nullptr);

  uint8_t sentinel = 0;
  uint8_t* png = nullptr;
  size_t size = 0;
  const auto refuse = [&](const radicle_swf* swf, size_t index, radicle_status status) {
    png = &sentinel;
    size = 77;
    EXPECT_EQ(radicle_swf_bitmap_png(swf, index, &png, &size), status) << radicle_last_error();
    EXPECT_EQ(png, nullptr);
    EXPECT_EQ(size, 0u);
  };
  radicle_swf_tag tag{};
  ASSERT_EQ(radicle_swf_tag_at(file.get(), 2, &tag), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(tag.code, 20);
  refuse(file.get(), 0, RADICLE_STATUS_NOT_FOUND);
  refuse(file.get(), 2, RADICLE_STATUS_INVALID_ARGUMENT);
  refuse(file.get(), 99, RADICLE_STATUS_INVALID_ARGUMENT);
  refuse(nullptr, 1, RADICLE_STATUS_INVALID_ARGUMENT);

  png = &sentinel;
  size = 77;
  EXPECT_EQ(radicle_swf_bitmap_png(file.get(), 1, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(size, 0u);
  png = &sentinel;
  EXPECT_EQ(radicle_swf_bitmap_png(file.get(), 1, &png, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(png, nullptr);
}

TEST(CApiSwfBitmapJpeg, SplicesTheJpegTablesOntoADefineBitsImage) {
  const auto file = open(tables_movie(6, swf_art_test::minimal_jpeg(2, 3)));
  ASSERT_NE(file, nullptr);
  EXPECT_EQ(exported_jpeg(file.get(), 1), hand_spliced_jpeg());
}

TEST(CApiSwfBitmapJpeg, DropsAnErroneousLeadingSeamForTagSixAndTagTwentyOne) {
  const auto spliced = open(tables_movie(6, with_leading_seam(swf_art_test::minimal_jpeg(2, 3))));
  ASSERT_NE(spliced, nullptr);
  EXPECT_EQ(exported_jpeg(spliced.get(), 1), hand_spliced_jpeg());

  const auto self_contained =
      open(swf_art_test::movie_with(21, defined_bits(4, with_leading_seam(swf_art_test::minimal_jpeg(2, 3)))));
  ASSERT_NE(self_contained, nullptr);
  EXPECT_EQ(exported_jpeg(self_contained.get(), 0), as_u8(swf_art_test::minimal_jpeg(2, 3)));
}

TEST(CApiSwfBitmapJpeg, RefusesNonJpegAndAlphaJpegTagsWithOutputsCleared) {
  const auto png = open(png_movie());
  const auto alpha = open(swf_art_test::movie_with(35, swf_art_test::jpeg3_payload(swf_art_test::minimal_jpeg(2, 3), 6)));
  ASSERT_NE(png, nullptr);
  ASSERT_NE(alpha, nullptr);

  uint8_t sentinel = 0;
  uint8_t* jpeg = nullptr;
  size_t size = 0;
  const auto refuse = [&](const radicle_swf* swf, size_t index, radicle_status status) {
    jpeg = &sentinel;
    size = 77;
    EXPECT_EQ(radicle_swf_bitmap_jpeg(swf, index, &jpeg, &size), status) << radicle_last_error();
    EXPECT_EQ(jpeg, nullptr);
    EXPECT_EQ(size, 0u);
  };
  refuse(png.get(), 0, RADICLE_STATUS_NOT_FOUND);
  refuse(png.get(), 1, RADICLE_STATUS_NOT_FOUND);
  refuse(png.get(), 2, RADICLE_STATUS_NOT_FOUND);
  refuse(alpha.get(), 0, RADICLE_STATUS_NOT_FOUND);
  refuse(png.get(), 99, RADICLE_STATUS_INVALID_ARGUMENT);
  refuse(nullptr, 1, RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiSwfBitmapJpeg, RejectsNullArgumentsWithOutputsCleared) {
  const auto file = open(tables_movie(6, swf_art_test::minimal_jpeg(2, 3)));
  ASSERT_NE(file, nullptr);

  uint8_t sentinel = 0;
  uint8_t* jpeg = &sentinel;
  size_t size = 77;
  EXPECT_EQ(radicle_swf_bitmap_jpeg(file.get(), 1, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(radicle_swf_bitmap_jpeg(file.get(), 1, &jpeg, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(jpeg, nullptr);
}

// Regression: the layout ABI dropped the composed rectangle or the parent path that the C# draw code reads by index.
TEST(CApiSwfLayout, ExposesStatesAndRowsOfANestedMovieThroughStableArrays) {
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  radicle_swf_layout* raw = nullptr;
  ASSERT_EQ(radicle_swf_layout_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  LayoutHandle layout(raw);
  ASSERT_NE(layout, nullptr);

  radicle_swf_layout_info info{};
  ASSERT_EQ(radicle_swf_layout_get_info(layout.get(), &info), RADICLE_STATUS_OK);
  EXPECT_DOUBLE_EQ(info.stage.width, 550.0);
  EXPECT_DOUBLE_EQ(info.stage.height, 400.0);
  EXPECT_DOUBLE_EQ(info.frame_rate, 12.0);
  EXPECT_EQ(info.scripted, 0);
  EXPECT_EQ(info.has_as3, 0);
  EXPECT_EQ(info.legacy_moves, 0);
  EXPECT_EQ(info.state_count, 1u);
  EXPECT_EQ(info.row_count, 2u);

  radicle_swf_layout_state state{};
  ASSERT_EQ(radicle_swf_layout_state_at(layout.get(), 0, &state), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(state.label), "");
  EXPECT_EQ(state.timeline_id, 0);
  EXPECT_EQ(state.first_row, 0u);
  EXPECT_EQ(state.row_count, 2u);

  radicle_swf_layout_row art{};
  ASSERT_EQ(radicle_swf_layout_row_at(layout.get(), 0, &art), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(art.path), "Art");
  EXPECT_EQ(std::string(art.parent_path), "");
  EXPECT_EQ(art.kind, RADICLE_SWF_CHARACTER_SPRITE);
  EXPECT_EQ(art.character_id, 9);
  EXPECT_EQ(art.clip_frame_count, 1u);
  EXPECT_EQ(art.has_rect, 1);

  radicle_swf_layout_row cell{};
  ASSERT_EQ(radicle_swf_layout_row_at(layout.get(), 1, &cell), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(cell.path), "Art/Cell");
  EXPECT_EQ(std::string(cell.parent_path), "Art");
  EXPECT_EQ(cell.kind, RADICLE_SWF_CHARACTER_SHAPE);
  EXPECT_EQ(cell.character_id, 5);
  EXPECT_EQ(cell.visible, 1);
  EXPECT_EQ(cell.has_rect, 1);
  EXPECT_DOUBLE_EQ(cell.rect.x, 10.0);
  EXPECT_DOUBLE_EQ(cell.rect.width, 100.0);
  EXPECT_DOUBLE_EQ(cell.world_transform.scale_x, 1.0);
  EXPECT_DOUBLE_EQ(cell.world_transform.translate_x, 10.0);
  EXPECT_EQ(cell.has_text, 0);
  EXPECT_EQ(std::string(cell.text.variable), "");
}

// Regression: a layout kept a pointer into the movie and read freed strings after the movie was destroyed, or accepted an out-of-range index.
TEST(CApiSwfLayout, RefusesBadArgumentsAndOutlivesTheMovieItWasBuiltFrom) {
  radicle_swf_layout* raw = nullptr;
  EXPECT_EQ(radicle_swf_layout_open(nullptr, &raw), RADICLE_STATUS_INVALID_ARGUMENT);
  auto swf = open(nested_movie());
  ASSERT_NE(swf, nullptr);
  EXPECT_EQ(radicle_swf_layout_open(swf.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_swf_layout_open(swf.get(), &raw), RADICLE_STATUS_OK);
  LayoutHandle layout(raw);

  radicle_swf_layout_row row{};
  EXPECT_EQ(radicle_swf_layout_row_at(layout.get(), 2, &row), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_row_at(nullptr, 0, &row), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_swf_layout_state state{};
  EXPECT_EQ(radicle_swf_layout_state_at(layout.get(), 1, &state), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_layout_get_info(layout.get(), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  swf.reset();
  ASSERT_EQ(radicle_swf_layout_row_at(layout.get(), 0, &row), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(row.path), "Art");
}

// Regression: a layout built before a plan kept serving its old rows instead of refusing with the stale message.
TEST(CApiSwfLayout, RefusesOnceAPlanMovesTheMovieRevision) {
  auto swf = open(swf_test::movie_of(1, {{43, swf_test::frame_label("a")}, {1, {}}}));
  ASSERT_NE(swf, nullptr);
  radicle_swf_layout* raw = nullptr;
  ASSERT_EQ(radicle_swf_layout_open(swf.get(), &raw), RADICLE_STATUS_OK);
  LayoutHandle layout(raw);

  const auto renamed = swf_test::frame_label("b");
  const radicle_swf_tag_replacement replacement{0, reinterpret_cast<const std::uint8_t*>(renamed.data()),
                                                renamed.size()};
  const radicle_swf_tag_plan plan{&replacement, 1, nullptr, 0, nullptr, 0};
  radicle_swf_plan_result result{};
  ASSERT_EQ(radicle_swf_apply_plan(swf.get(), &plan, &result), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(result.changed, 1);

  radicle_swf_layout_info info{};
  EXPECT_EQ(radicle_swf_layout_get_info(layout.get(), &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("built before"), std::string::npos)
      << radicle_last_error();

  radicle_swf_layout* reopened = nullptr;
  ASSERT_EQ(radicle_swf_layout_open(swf.get(), &reopened), RADICLE_STATUS_OK)
      << radicle_last_error();
  LayoutHandle after(reopened);
  EXPECT_EQ(radicle_swf_layout_get_info(after.get(), &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.state_count, 1u);
}

// Regression: the layout info reported one scripted flag, so a UI could not tell AS3 from an AVM1 timeline jump.
TEST(CApiSwfLayout, InfoSplitsAs3FromALegacyTimelineJump) {
  const std::vector<std::byte> abc{std::byte{0x00}};
  const std::vector<std::byte> jump{std::byte{0x81}, std::byte{0x02}, std::byte{0},
                                    std::byte{0},    std::byte{0},    std::byte{0}};
  const auto info_of = [](const std::vector<swf_test::TagPayload>& tags) {
    auto swf = open(swf_test::movie_of(1, tags));
    radicle_swf_layout* raw = nullptr;
    EXPECT_EQ(radicle_swf_layout_open(swf.get(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
    LayoutHandle layout(raw);
    radicle_swf_layout_info info{};
    EXPECT_EQ(radicle_swf_layout_get_info(layout.get(), &info), RADICLE_STATUS_OK);
    return info;
  };
  const auto as3 = info_of({{72, abc}, {1, {}}});
  EXPECT_EQ(as3.scripted, 1);
  EXPECT_EQ(as3.has_as3, 1);
  EXPECT_EQ(as3.legacy_moves, 0);
  const auto legacy = info_of({{12, jump}, {1, {}}});
  EXPECT_EQ(legacy.scripted, 1);
  EXPECT_EQ(legacy.has_as3, 0);
  EXPECT_EQ(legacy.legacy_moves, 1);
  const auto both = info_of({{12, jump}, {72, abc}, {1, {}}});
  EXPECT_EQ(both.has_as3, 1);
  EXPECT_EQ(both.legacy_moves, 1);
}

// Regression: an unplaceable movie returned an empty layout instead of PARSE_ERROR with the walk's message.
TEST(CApiSwfLayout, UnplaceableMovieIsAParseErrorWithNoLayout) {
  auto broken = open(unplaceable_movie());
  ASSERT_NE(broken, nullptr);
  radicle_swf_layout* out = nullptr;
  EXPECT_EQ(radicle_swf_layout_open(broken.get(), &out), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(out, nullptr);
  EXPECT_NE(std::string(radicle_last_error()).find("depth"), std::string::npos)
      << radicle_last_error();
}
