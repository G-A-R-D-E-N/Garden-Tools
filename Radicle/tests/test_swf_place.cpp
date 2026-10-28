#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_timeline.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfMatrix;
using fo4::swf::SwfStatus;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;

std::vector<std::byte> movie_of(const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> place2_matrix_payload() {
  SwfBuilder builder;
  builder.u8(0x36);
  builder.u16(3);
  builder.u16(9);
  builder.push_bits(1, 1);
  builder.push_bits(19, 5);
  builder.push_bits(0x20000, 19);
  builder.push_bits(0x20000, 19);
  builder.push_bits(0, 1);
  builder.push_bits(16, 5);
  builder.push_bits(100, 16);
  builder.push_bits(200, 16);
  builder.align();
  builder.u16(7);
  builder.text("Hat");
  return builder.body;
}

std::vector<std::byte> place2_color_clip_payload() {
  SwfBuilder builder;
  builder.u8(0x4B);
  builder.u16(4);
  builder.u16(2);
  builder.push_bits(1, 1);
  builder.push_bits(1, 1);
  builder.push_bits(10, 4);
  builder.push_bits(128, 10);
  builder.push_bits(256, 10);
  builder.push_bits(256, 10);
  builder.push_bits(256, 10);
  builder.push_bits(10, 10);
  builder.push_bits(static_cast<std::uint32_t>(-20) & 0x3FFu, 10);
  builder.push_bits(0, 10);
  builder.push_bits(0, 10);
  builder.align();
  builder.u16(6);
  return builder.body;
}

std::vector<std::byte> place1_payload() {
  SwfBuilder builder;
  builder.u16(11);
  builder.u16(2);
  builder.push_bits(0, 1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 5);
  builder.align();
  builder.push_bits(0, 1);
  builder.push_bits(1, 1);
  builder.push_bits(8, 4);
  builder.push_bits(64, 8);
  builder.push_bits(100, 8);
  builder.push_bits(127, 8);
  builder.align();
  return builder.body;
}

std::vector<std::byte> place3_image_with_character_payload() {
  SwfBuilder builder;
  builder.u8(0x06);
  builder.u8(0x10);
  builder.u16(1);
  builder.u16(1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 5);
  builder.align();
  return builder.body;
}

std::vector<std::byte> place3_image_without_character_payload() {
  SwfBuilder builder;
  builder.u8(0x00);
  builder.u8(0x10);
  builder.u16(1);
  builder.text("BitmapData");
  return builder.body;
}

std::vector<std::byte> place3_class_name_payload() {
  SwfBuilder builder;
  builder.u8(0x02);
  builder.u8(0x08);
  builder.u16(1);
  builder.text("Hat");
  builder.u16(12);
  return builder.body;
}

std::vector<std::byte> place3_all_filter_kinds_payload() {
  SwfBuilder builder;
  builder.u8(0x00);
  builder.u8(0x03);
  builder.u16(1);
  builder.u8(6);
  builder.u8(2);
  for (int index = 0; index < 15; ++index) builder.u8(0);
  builder.u8(3);
  for (int index = 0; index < 27; ++index) builder.u8(0);
  builder.u8(4);
  builder.u8(3);
  for (int index = 0; index < 3 * 5 + 19; ++index) builder.u8(0);
  builder.u8(5);
  builder.u8(2);
  builder.u8(3);
  for (int index = 0; index < 13 + 4 * 2 * 3; ++index) builder.u8(0);
  builder.u8(6);
  for (int index = 0; index < 80; ++index) builder.u8(0);
  builder.u8(7);
  builder.u8(1);
  for (int index = 0; index < 5 + 19; ++index) builder.u8(0);
  builder.u8(9);
  return builder.body;
}

std::vector<std::byte> place3_filters_payload() {
  SwfBuilder builder;
  builder.u8(0x02);
  builder.u8(0x67);
  builder.u16(4);
  builder.u16(3);
  builder.u8(2);
  builder.u8(0);
  for (int index = 0; index < 23; ++index) {
    builder.u8(0);
  }
  builder.u8(1);
  for (int index = 0; index < 9; ++index) {
    builder.u8(0);
  }
  builder.u8(8);
  builder.u8(1);
  builder.u8(1);
  builder.u8(0x10);
  builder.u8(0x20);
  builder.u8(0x30);
  builder.u8(0xFF);
  return builder.body;
}

std::vector<std::byte> matrix_before_ratio_payload() {
  SwfBuilder builder;
  builder.u8(0x14);
  builder.u16(1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 1);
  builder.push_bits(0, 5);
  builder.align();
  builder.u16(5);
  return builder.body;
}

std::vector<std::byte> truncated_matrix_payload() {
  SwfBuilder builder;
  builder.u8(0x04);
  builder.u16(5);
  return builder.body;
}

}  // namespace

TEST(SwfPlacement, PlaceObject2DecodesDepthCharacterMatrixRatioAndName) {
  const auto bytes = movie_of({{26, place2_matrix_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& tags = result.file.tags;
  ASSERT_TRUE(tags[0].placement.has_value());
  const auto& place = *tags[0].placement;
  EXPECT_EQ(place.depth, 3);
  EXPECT_EQ(place.character_id, std::optional<std::uint16_t>(9));
  ASSERT_TRUE(place.matrix.has_value());
  EXPECT_EQ(place.matrix->scale_x, 0x20000);
  EXPECT_EQ(place.matrix->scale_y, 0x20000);
  EXPECT_EQ(place.matrix->translate_x, 100);
  EXPECT_EQ(place.matrix->translate_y, 200);
  EXPECT_EQ(place.ratio, std::optional<std::uint16_t>(7));
  EXPECT_EQ(place.name, std::optional<std::string>("Hat"));
  EXPECT_FALSE(place.clip_depth.has_value());
  EXPECT_FALSE(place.move);
}

TEST(SwfPlacement, PlaceObject2DecodesMoveClipDepthAndAlphaColorTransform) {
  const auto bytes = movie_of({{26, place2_color_clip_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_TRUE(place.move);
  EXPECT_EQ(place.depth, 4);
  EXPECT_EQ(place.character_id, std::optional<std::uint16_t>(2));
  EXPECT_EQ(place.clip_depth, std::optional<std::uint16_t>(6));
  ASSERT_TRUE(place.color_transform.has_value());
  EXPECT_TRUE(place.color_transform->has_alpha);
  EXPECT_EQ(place.color_transform->multiply, (std::array<std::int32_t, 4>{128, 256, 256, 256}));
  EXPECT_EQ(place.color_transform->add, (std::array<std::int32_t, 4>{10, -20, 0, 0}));
}

TEST(SwfPlacement, PlaceObject1DecodesMatrixAndRgbColorTransformWithoutAlpha) {
  const auto bytes = movie_of({{4, place1_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_EQ(place.character_id, std::optional<std::uint16_t>(11));
  EXPECT_EQ(place.depth, 2);
  ASSERT_TRUE(place.matrix.has_value());
  EXPECT_EQ(*place.matrix, SwfMatrix{});
  ASSERT_TRUE(place.color_transform.has_value());
  EXPECT_FALSE(place.color_transform->has_alpha);
  EXPECT_EQ(place.color_transform->multiply, (std::array<std::int32_t, 4>{64, 100, 127, 256}));
  EXPECT_EQ(place.color_transform->add, (std::array<std::int32_t, 4>{0, 0, 0, 0}));
}

TEST(SwfPlacement, PlaceObject3ImageWithCharacterHasNoClassName) {
  const auto bytes = movie_of({{70, place3_image_with_character_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_FALSE(place.class_name.has_value());
  EXPECT_EQ(place.character_id, std::optional<std::uint16_t>(1));
  EXPECT_EQ(place.depth, 1);
  EXPECT_TRUE(place.matrix.has_value());
}

TEST(SwfPlacement, PlaceObject3ImageWithoutCharacterReadsClassName) {
  const auto bytes = movie_of({{70, place3_image_without_character_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_EQ(place.class_name, std::optional<std::string>("BitmapData"));
  EXPECT_FALSE(place.character_id.has_value());
}

TEST(SwfPlacement, PlaceObject3HasClassNameReadsNameBeforeCharacter) {
  const auto bytes = movie_of({{70, place3_class_name_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_EQ(place.class_name, std::optional<std::string>("Hat"));
  EXPECT_EQ(place.character_id, std::optional<std::uint16_t>(12));
}

TEST(SwfPlacement, PlaceObject3SkipsEveryFilterKindByItsSpecSize) {
  // Regression: the blend mode after the filter list must pin the last filter's size.
  const auto bytes = movie_of({{70, place3_all_filter_kinds_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_EQ(place.filters.size(), 6u);
  EXPECT_EQ(place.blend_mode, std::optional<std::uint8_t>(9));
}

TEST(SwfPlacement, PlaceObject3SkipsFilterListAndReadsBlendCacheVisibleAndBackground) {
  const auto bytes = movie_of({{70, place3_filters_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_EQ(place.filters.size(), 2u);
  EXPECT_EQ(place.blend_mode, std::optional<std::uint8_t>(8));
  EXPECT_EQ(place.bitmap_cache, std::optional<std::uint8_t>(1));
  EXPECT_EQ(place.visible, std::optional<bool>(true));
  EXPECT_EQ(place.background_color, std::optional<std::uint32_t>(0xFF102030u));
  EXPECT_EQ(place.character_id, std::optional<std::uint16_t>(3));
}

TEST(SwfPlacement, PlaceObject3VisibleFlagAloneDoesNotConsumeABackgroundColor) {
  SwfBuilder builder;
  builder.u8(0x00);
  builder.u8(0x20);
  builder.u16(4);
  builder.u8(0);
  const auto bytes = movie_of({{70, builder.body}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  EXPECT_EQ(place.visible, std::optional<bool>(false));
  EXPECT_FALSE(place.background_color.has_value());
}

TEST(SwfPlacement, RemoveObjectAndRemoveObject2DecodeDepthWithAndWithoutCharacter) {
  SwfBuilder first;
  first.u16(7);
  first.u16(3);
  SwfBuilder second;
  second.u16(3);
  const auto bytes = movie_of({{5, first.body}, {28, second.body}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  ASSERT_TRUE(result.file.tags[0].removal.has_value());
  EXPECT_EQ(result.file.tags[0].removal->character_id, std::optional<std::uint16_t>(7));
  EXPECT_EQ(result.file.tags[0].removal->depth, 3);
  ASSERT_TRUE(result.file.tags[1].removal.has_value());
  EXPECT_FALSE(result.file.tags[1].removal->character_id.has_value());
  EXPECT_EQ(result.file.tags[1].removal->depth, 3);
}

TEST(SwfPlacement, MatrixWithoutScaleOrRotateEndsOnAByteBoundaryBeforeRatio) {
  const auto bytes = movie_of({{26, matrix_before_ratio_payload()}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto& place = *result.file.tags[0].placement;
  ASSERT_TRUE(place.matrix.has_value());
  EXPECT_EQ(*place.matrix, SwfMatrix{});
  EXPECT_EQ(place.ratio, std::optional<std::uint16_t>(5));
}

TEST(SwfPlacement, TruncatedPlacementKeepsRawPayloadAndTheNextTagStillParses) {
  const auto truncated = truncated_matrix_payload();
  const auto bytes = movie_of({{26, truncated}, {1, std::vector<std::byte>{}}});
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  ASSERT_EQ(result.file.tags.size(), 3u);
  EXPECT_FALSE(result.file.tags[0].placement.has_value());
  EXPECT_EQ(result.file.tags[0].payload, truncated);
  EXPECT_EQ(result.file.tags[1].code, fo4::swf::tag::kShowFrame);
  EXPECT_EQ(fo4::swf::write_swf(result.file), bytes);
}

namespace {

using fo4::swf::build_movie;
using fo4::swf::census;
using fo4::swf::inspect_swf;
using fo4::swf::SwfTimelineStatus;

std::vector<std::byte> matrix_translation(std::int32_t x, std::int32_t y) {
  SwfBuilder bits;
  bits.push_bits(0, 1);
  bits.push_bits(0, 1);
  bits.push_bits(11, 5);
  bits.push_bits(static_cast<std::uint32_t>(x) & 0x7FFu, 11);
  bits.push_bits(static_cast<std::uint32_t>(y) & 0x7FFu, 11);
  bits.align();
  return bits.body;
}

std::vector<std::byte> matrix_scale_translate(std::int32_t scale, std::int32_t x, std::int32_t y) {
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

std::vector<std::byte> movie_of_rect(std::int32_t x_min, std::int32_t x_max, std::int32_t y_min,
                                     std::int32_t y_max, unsigned nbits,
                                     const std::vector<TagPayload>& tags) {
  SwfBuilder builder;
  builder.rect(x_min, x_max, y_min, y_max, nbits);
  builder.u16(0x0C00);
  builder.u16(1);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, 10, false, false);
}

std::vector<std::byte> place2_tag(std::uint8_t flags, std::uint16_t depth,
                                  std::optional<std::uint16_t> character,
                                  const std::vector<std::byte>& matrix, const std::string& name) {
  SwfBuilder body;
  body.u8(flags);
  body.u16(depth);
  if (character) {
    body.u16(*character);
  }
  if (!matrix.empty()) {
    body.bytes(matrix);
  }
  if (!name.empty()) {
    body.text(name);
  }
  return body.body;
}

std::vector<std::byte> place3_class_only_tag(std::uint16_t depth, const std::string& name) {
  SwfBuilder body;
  body.u8(0x00);
  body.u8(0x08);
  body.u16(depth);
  body.text(name);
  return body.body;
}

std::vector<std::byte> edit_text_tag(std::uint16_t id, const std::string& text) {
  SwfBuilder body;
  body.u16(id);
  body.rect(0, 200, 0, 40, 11);
  body.u8(0x80);
  body.u8(0x00);
  body.u8(0x00);
  body.text(text);
  return body.body;
}

std::vector<std::byte> html_edit_text(std::uint16_t id, const std::string& text) {
  SwfBuilder body;
  body.u16(id);
  body.rect(0, 200, 0, 40, 11);
  body.u8(0x80);
  body.u8(0x02);
  body.u8(0x00);
  body.text(text);
  return body.body;
}

std::vector<std::byte> frame_label_tag(const std::string& label) {
  SwfBuilder body;
  body.text(label);
  return body.body;
}

std::vector<std::byte> symbol_class_tag(std::uint16_t id, const std::string& name) {
  SwfBuilder body;
  body.u16(1);
  body.u16(id);
  body.text(name);
  return body.body;
}

std::vector<std::byte> sprite_tag(std::uint16_t id, std::uint16_t frames,
                                  const std::vector<TagPayload>& children) {
  SwfBuilder body;
  body.u16(id);
  body.u16(frames);
  for (const auto& [code, payload] : children) {
    body.tag(code, payload, false);
  }
  body.tag(0);
  return body.body;
}

}  // namespace

TEST(SwfTimeline, CarriesDepthStateAcrossAReplacement) {
  const auto bytes = movie_of({
      {26, place2_tag(0x26, 1, 1, matrix_translation(240, -40), "Shifted")},
      {26, place2_tag(0x03, 1, 2, {}, "")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto movie = build_movie(result.file);
  ASSERT_EQ(movie.status, SwfTimelineStatus::ok) << movie.message;
  ASSERT_EQ(movie.root.placements.size(), 2u);
  EXPECT_EQ(movie.root.placements[1].character_id, 2);
  EXPECT_EQ(movie.root.placements[1].name, "Shifted");
  EXPECT_DOUBLE_EQ(movie.root.placements[1].transform.translate_x, 12.0);
  EXPECT_DOUBLE_EQ(movie.root.placements[1].transform.translate_y, -2.0);
  ASSERT_EQ(movie.root.frames.size(), 1u);
  ASSERT_EQ(movie.root.frames[0].objects.size(), 1u);
  EXPECT_EQ(movie.root.frames[0].objects[0].character_id, 2);
  EXPECT_EQ(movie.root.frames[0].objects[0].name, "Shifted");
}

TEST(SwfTimeline, MoveOnlyUpdateCarriesIntoALaterReplacement) {
  const auto bytes = movie_of({
      {26, place2_tag(0x26, 1, 1, matrix_translation(240, -40), "Shifted")},
      {26, place2_tag(0x05, 1, std::nullopt, matrix_translation(500, 80), "")},
      {26, place2_tag(0x03, 1, 2, {}, "")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto movie = build_movie(result.file);
  ASSERT_EQ(movie.status, SwfTimelineStatus::ok) << movie.message;
  ASSERT_EQ(movie.root.placements.size(), 3u);
  EXPECT_DOUBLE_EQ(movie.root.placements[1].transform.translate_x, 25.0);
  EXPECT_DOUBLE_EQ(movie.root.placements[1].transform.translate_y, 4.0);
  EXPECT_DOUBLE_EQ(movie.root.placements[2].transform.translate_x, 25.0);
  EXPECT_EQ(movie.root.placements[2].character_id, 2);
}

TEST(SwfTimeline, RemovalClearsTheInheritedDepthState) {
  const auto bytes = movie_of({
      {26, place2_tag(0x26, 1, 1, matrix_translation(240, -40), "Shifted")},
      {28, {std::byte{0x01}, std::byte{0x00}}},
      {26, place2_tag(0x22, 1, 2, {}, "Fresh")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto movie = build_movie(result.file);
  ASSERT_EQ(movie.status, SwfTimelineStatus::ok) << movie.message;
  ASSERT_EQ(movie.root.placements.size(), 2u);
  EXPECT_EQ(movie.root.placements[1].name, "Fresh");
  EXPECT_EQ(movie.root.placements[1].transform, fo4::swf::SwfTransform{});
  ASSERT_EQ(movie.root.frames[0].objects.size(), 1u);
  EXPECT_EQ(movie.root.frames[0].objects[0].character_id, 2);
}

TEST(SwfTimeline, RejectsMoveAtAnUndefinedDepthAndAClassOnlyPlacement) {
  const auto move = movie_of({{26, place2_tag(0x05, 1, std::nullopt, matrix_translation(20, 0), "")},
                             {1, {}}});
  const auto move_result = fo4::swf::read_swf(move);
  ASSERT_TRUE(move_result.ok()) << fo4::swf::status_name(move_result.status);
  EXPECT_EQ(build_movie(move_result.file).status, SwfTimelineStatus::malformed);

  const auto class_only = movie_of({{70, place3_class_only_tag(1, "OwnedImage")}, {1, {}}});
  const auto class_result = fo4::swf::read_swf(class_only);
  ASSERT_TRUE(class_result.ok()) << fo4::swf::status_name(class_result.status);
  EXPECT_EQ(build_movie(class_result.file).status, SwfTimelineStatus::malformed);
}

TEST(SwfTimeline, CountsDefinitionsLabelsAndInspectionRows) {
  const auto bytes = movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {37, edit_text_tag(2, "Hi")},
      {39, sprite_tag(3, 1,
                      {{26, place2_tag(0x22, 1, 5, {}, "Child")}, {43, frame_label_tag("idle")}, {1, {}}})},
      {43, frame_label_tag("open")},
      {76, symbol_class_tag(0, "OwnedFixture")},
      {26, place2_tag(0x22, 1, 3, {}, "Art")},
      {26, place2_tag(0x22, 2, 2, {}, "Title")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto counts = census(result.file);
  EXPECT_EQ(counts.shapes, 1u);
  EXPECT_EQ(counts.sprites, 1u);
  EXPECT_EQ(counts.text_fields, 1u);
  EXPECT_EQ(counts.symbols, 1u);
  EXPECT_EQ(counts.root_labels, 1u);
  EXPECT_EQ(counts.sprite_labels, 1u);
  EXPECT_EQ(counts.labelled_sprite_definitions, 1u);

  const std::string expected =
      "## owned.swf\n"
      "Stage 550 x 400 at 12 fps, 1 root frame(s).\n"
      "Document class: OwnedFixture\n"
      "Root frame labels: open@1\n"
      "Totals: 1 shapes, 1 sprites, 0 morph shapes, 0 images, 1 text fields.\n"
      "Frame-label census: 1 root labels, 1 sprite labels in 1 of 1 sprite definitions "
      "(including unnamed/unplaced).\n"
      "- Art: sprite #3, first frame 1, at (0, 0)\n"
      "    states: idle@1\n"
      "- Art/Child: shape #5, first frame 1, at (0, 0), size 100 x 50\n"
      "- Title: text field #2, first frame 1, at (0, 0), rect (0, 0, 10 x 2), text \"Hi\"\n";
  EXPECT_EQ(inspect_swf(result.file, "owned.swf"), expected);
}

TEST(SwfTimeline, AppliesTheRowTransformToATextFieldRect) {
  const auto bytes = movie_of({
      {37, edit_text_tag(2, "Hi")},
      {26, place2_tag(0x26, 1, 2, matrix_scale_translate(0x20000, 240, -40), "Title")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "scaled.swf");
  EXPECT_NE(report.find(", rect (12, -2, 20 x 4)"), std::string::npos) << report;
}

TEST(SwfTimeline, NamesButtonAndVideoCharactersFromTheirLeadingId) {
  SwfBuilder button;
  button.u16(7);
  button.u16(0);
  SwfBuilder video;
  video.u16(9);
  const auto bytes = movie_of({
      {7, button.body},
      {60, video.body},
      {26, place2_tag(0x22, 1, 7, {}, "Ok")},
      {26, place2_tag(0x22, 2, 9, {}, "Clip")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "kinds.swf");
  EXPECT_NE(report.find("- Ok: button #7, first frame 1, at (0, 0)"), std::string::npos) << report;
  EXPECT_NE(report.find("- Clip: video #9, first frame 1, at (0, 0)"), std::string::npos) << report;
}

TEST(SwfTimeline, RoundsHalfToEvenLikeTheEngineReport) {
  const auto bytes = movie_of_rect(0, 5, 0, 15, 11, {{1, {}}});
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "rounding.swf");
  EXPECT_NE(report.find("Stage 0.2 x 0.8 at 12 fps, 1 root frame(s)."), std::string::npos) << report;
}

TEST(SwfTimeline, FreshPlaceOnALiveDepthResetsTheInheritedState) {
  const auto bytes = movie_of({
      {26, place2_tag(0x26, 1, 1, matrix_translation(240, -40), "First")},
      {26, place2_tag(0x02, 1, 2, {}, "")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto movie = build_movie(result.file);
  ASSERT_EQ(movie.status, SwfTimelineStatus::ok) << movie.message;
  ASSERT_EQ(movie.root.placements.size(), 2u);
  EXPECT_EQ(movie.root.placements[1].character_id, 2);
  EXPECT_TRUE(movie.root.placements[1].name.empty());
  EXPECT_EQ(movie.root.placements[1].transform, fo4::swf::SwfTransform{});
}

TEST(SwfTimeline, KeepsACharacterLessPlacementWithoutARow) {
  const auto bytes = movie_of({
      {26, place2_tag(0x20, 5, std::nullopt, {}, "Slot")},
      {26, place2_tag(0x05, 5, std::nullopt, matrix_translation(40, 0), "")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto movie = build_movie(result.file);
  ASSERT_EQ(movie.status, SwfTimelineStatus::ok) << movie.message;
  EXPECT_TRUE(movie.root.placements.empty());
  ASSERT_EQ(movie.root.frames.size(), 1u);
  ASSERT_EQ(movie.root.frames[0].objects.size(), 1u);
  EXPECT_EQ(movie.root.frames[0].objects[0].character_id, 0u);
  EXPECT_EQ(movie.root.frames[0].objects[0].name, "Slot");
  EXPECT_DOUBLE_EQ(movie.root.frames[0].objects[0].transform.translate_x, 2.0);
}

TEST(SwfTimeline, RejectsATruncatedRemovalPayload) {
  const auto bytes = movie_of({{28, {}}, {1, {}}});
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, SwfStatus::malformed_tag);
  EXPECT_EQ(build_movie(result.file).status, SwfTimelineStatus::malformed);
}

TEST(SwfTimeline, TruncatesTheReportAtTheRowLimit) {
  std::vector<TagPayload> tags;
  for (std::uint16_t depth = 1; depth <= 401; ++depth) {
    tags.push_back({26, place2_tag(0x22, depth, depth, {}, "P" + std::to_string(depth))});
  }
  tags.push_back({1, {}});
  const auto result = fo4::swf::read_swf(movie_of(tags));
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "many.swf");
  EXPECT_NE(report.find("(list truncated at 400 placements)"), std::string::npos);
  EXPECT_EQ(report.find("- P401:"), std::string::npos);
}

TEST(SwfTimeline, StopsSpriteNestingAtTheDepthLimit) {
  const auto bytes = movie_of({
      {32, swf_art_test::define_shape_3_payload()},
      {39, sprite_tag(9, 1, {{26, place2_tag(0x22, 1, 5, {}, "L5")}, {1, {}}})},
      {39, sprite_tag(8, 1, {{26, place2_tag(0x22, 1, 9, {}, "L4")}, {1, {}}})},
      {39, sprite_tag(7, 1, {{26, place2_tag(0x22, 1, 8, {}, "L3")}, {1, {}}})},
      {39, sprite_tag(6, 1, {{26, place2_tag(0x22, 1, 7, {}, "L2")}, {1, {}}})},
      {39, sprite_tag(3, 1, {{26, place2_tag(0x22, 1, 6, {}, "L1")}, {1, {}}})},
      {26, place2_tag(0x22, 1, 3, {}, "L0")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "nested.swf");
  EXPECT_NE(report.find("- L0/L1/L2/L3/L4: sprite #9"), std::string::npos) << report;
  EXPECT_EQ(report.find("L5"), std::string::npos) << report;
}

TEST(SwfTimeline, StopsASelfReferencingSprite) {
  const auto bytes = movie_of({
      {39, sprite_tag(3, 1, {{26, place2_tag(0x22, 1, 3, {}, "Loop")}, {1, {}}})},
      {26, place2_tag(0x22, 1, 3, {}, "Loop")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "cycle.swf");
  EXPECT_NE(report.find("- Loop/Loop: sprite #3"), std::string::npos) << report;
  EXPECT_EQ(report.find("Loop/Loop/Loop"), std::string::npos) << report;
}

TEST(SwfTimeline, EscapesHtmlTextLikeTheJsonEncoder) {
  const auto bytes = movie_of({
      {37, html_edit_text(2, "<p>&'\xC3\xA9")},
      {26, place2_tag(0x22, 1, 2, {}, "Body")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "html.swf");
  EXPECT_NE(report.find(", text \"\\u003Cp\\u003E\\u0026\\u0027\\u00E9\", html"),
            std::string::npos)
      << report;
}

TEST(SwfTimeline, PrintsNegativeZeroAsZero) {
  const auto bytes = movie_of({
      {26, place2_tag(0x26, 1, 4, matrix_translation(0, -1), "Z")},
      {1, {}},
  });
  const auto result = fo4::swf::read_swf(bytes);
  ASSERT_TRUE(result.ok()) << fo4::swf::status_name(result.status);
  const auto report = inspect_swf(result.file, "zero.swf");
  EXPECT_NE(report.find("- Z: undefined #4, first frame 1, at (0, 0)"), std::string::npos) << report;
  EXPECT_EQ(report.find("-0"), std::string::npos) << report;
}
