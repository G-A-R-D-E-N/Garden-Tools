#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_bitmap_edit.hpp"
#include "fo4/swf/swf_bitmap_png.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_edit.hpp"
#include "swf_art_fixtures.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfDisplayList;
using fo4::swf::SwfFile;
using fo4::swf::SwfInstanceEditTarget;
using fo4::swf::SwfInstanceTransformEdit;
using fo4::swf::SwfMatrix;
using fo4::swf::SwfTransform;
using fo4::swf::SwfTransformEditResult;
using fo4::swf::SwfBitmapEditStatus;
using fo4::swf::replace_bitmap_lossless;
using swf_art_test::define_shape_3_payload;
using swf_art_test::lossless_raw_payload;
using swf_art_test::minimal_jpeg;
using swf_art_test::png_chunks;
using swf_test::SwfBuilder;
using swf_test::wrap_body;

using TagPayload = std::pair<std::uint16_t, std::vector<std::byte>>;

// Raw 16.16 / twips fields: a fixture must author the encoding the decoder sees.
struct Matrix {
  std::int32_t scale_x{65536};
  std::int32_t scale_y{65536};
  std::int32_t skew0{0};
  std::int32_t skew1{0};
  std::int32_t translate_x{0};
  std::int32_t translate_y{0};
};

void push_matrix(SwfBuilder& builder, const Matrix& matrix) {
  const bool has_scale = matrix.scale_x != 65536 || matrix.scale_y != 65536;
  builder.push_bits(has_scale ? 1 : 0, 1);
  if (has_scale) {
    builder.push_bits(19, 5);
    builder.push_bits(static_cast<std::uint32_t>(matrix.scale_x), 19);
    builder.push_bits(static_cast<std::uint32_t>(matrix.scale_y), 19);
  }
  const bool has_rotate = matrix.skew0 != 0 || matrix.skew1 != 0;
  builder.push_bits(has_rotate ? 1 : 0, 1);
  if (has_rotate) {
    builder.push_bits(19, 5);
    builder.push_bits(static_cast<std::uint32_t>(matrix.skew0), 19);
    builder.push_bits(static_cast<std::uint32_t>(matrix.skew1), 19);
  }
  builder.push_bits(16, 5);
  builder.push_bits(static_cast<std::uint32_t>(matrix.translate_x), 16);
  builder.push_bits(static_cast<std::uint32_t>(matrix.translate_y), 16);
  builder.align();
}

// HasCharacter|HasMatrix|HasName; a clip depth requires HasClipDepth.
std::vector<std::byte> place2(std::uint16_t depth, std::uint16_t character, std::string_view name,
                              const Matrix& matrix, std::optional<std::uint16_t> clip_depth) {
  SwfBuilder builder;
  builder.u8(clip_depth ? 0x66 : 0x26);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.text(name);
  if (clip_depth) {
    builder.u16(*clip_depth);
  }
  return builder.body;
}

// Constraint: a v3 body has two flag bytes and its blend mode follows the name.
std::vector<std::byte> place3(std::uint16_t depth, std::uint16_t character, std::string_view name,
                              const Matrix& matrix, std::uint8_t blend) {
  SwfBuilder builder;
  builder.u8(0x26);  // Constraint: HasCharacter|HasMatrix|HasName.
  builder.u8(0x02);  // Constraint: HasBlendMode lives in the second flag byte.
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.text(name);
  builder.u8(blend);
  return builder.body;
}

// Move|HasMatrix without a character: it must re-author the MATRIX of a live depth.
std::vector<std::byte> move2_matrix(std::uint16_t depth, const Matrix& matrix) {
  SwfBuilder builder;
  builder.u8(0x05);
  builder.u16(depth);
  push_matrix(builder, matrix);
  return builder.body;
}

// Constraint: a v1 body has no flag byte, so its MATRIX offset is four and a CXFORM may follow.
std::vector<std::byte> place1(std::uint16_t character, std::uint16_t depth, const Matrix& matrix) {
  SwfBuilder builder;
  builder.u16(character);
  builder.u16(depth);
  push_matrix(builder, matrix);
  builder.push_bits(0, 1);  // Constraint: no additive term.
  builder.push_bits(1, 1);  // Constraint: multiplicative term present.
  builder.push_bits(8, 4);  // Constraint: eight bits per channel.
  builder.push_bits(64, 8);
  builder.push_bits(64, 8);
  builder.push_bits(64, 8);
  builder.align();
  return builder.body;
}

// HasCharacter only: no MATRIX, so a target in this lifetime must be refused.
std::vector<std::byte> place2_without_matrix(std::uint16_t depth, std::uint16_t character) {
  SwfBuilder builder;
  builder.u8(0x02);
  builder.u16(depth);
  builder.u16(character);
  return builder.body;
}

std::vector<std::byte> remove2(std::uint16_t depth) {
  SwfBuilder builder;
  builder.u16(depth);
  return builder.body;
}

std::vector<std::byte> sprite_payload(std::uint16_t sprite_id, std::uint16_t frame_count,
                                      const std::vector<TagPayload>& children) {
  SwfBuilder builder;
  builder.u16(sprite_id);
  builder.u16(frame_count);
  for (const auto& [code, payload] : children) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return builder.body;
}

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

SwfFile open(const std::vector<std::byte>& bytes) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, fo4::swf::SwfStatus::ok) << result.message;
  return result.file;
}

std::vector<std::vector<std::byte>> payloads_of(const SwfFile& file) {
  std::vector<std::vector<std::byte>> out;
  for (const auto& tag : file.tags) {
    out.push_back(tag.payload);
  }
  return out;
}

const SwfInstanceEditTarget* find_target(const std::vector<SwfInstanceEditTarget>& targets,
                                         std::string_view path) {
  const auto found = std::find_if(targets.begin(), targets.end(),
                                  [&](const SwfInstanceEditTarget& target) {
                                    return target.path == path;
                                  });
  return found == targets.end() ? nullptr : &*found;
}

SwfTransform transform_of(std::int32_t scale_x, std::int32_t scale_y, std::int32_t skew0,
                          std::int32_t skew1, std::int32_t translate_x, std::int32_t translate_y) {
  SwfTransform transform;
  transform.scale_x = static_cast<double>(scale_x) / 65536.0;
  transform.scale_y = static_cast<double>(scale_y) / 65536.0;
  transform.rotate_skew0 = static_cast<double>(skew0) / 65536.0;
  transform.rotate_skew1 = static_cast<double>(skew1) / 65536.0;
  transform.translate_x = static_cast<double>(translate_x) / 20.0;
  transform.translate_y = static_cast<double>(translate_y) / 20.0;
  return transform;
}

std::vector<std::uint8_t> rgba_2x2() {
  return {10, 20, 30, 255, 200, 100, 1, 128, 0, 0, 0, 0, 255, 128, 0, 128};
}

std::vector<std::uint8_t> zeros(std::size_t count) {
  return std::vector<std::uint8_t>(count, 0);
}

std::vector<std::uint8_t> head_of(const std::vector<std::byte>& payload, std::size_t count) {
  std::vector<std::uint8_t> out;
  for (std::size_t at = 0; at < count && at < payload.size(); ++at) {
    out.push_back(static_cast<std::uint8_t>(payload[at]));
  }
  return out;
}

std::vector<std::uint8_t> inflated(std::span<const std::byte> packed, std::size_t size) {
  std::vector<std::uint8_t> raw(size);
  uLongf length = static_cast<uLongf>(size);
  EXPECT_EQ(uncompress(raw.data(), &length, reinterpret_cast<const Bytef*>(packed.data()),
                       static_cast<uLong>(packed.size())),
            Z_OK);
  raw.resize(length);
  return raw;
}

}  // namespace

TEST(SwfEditTransform, EncodesTheMinimumBitWidthTheValuesNeed) {
  // Identity: no scale, no rotate, zero-bit translate, so the field must be one byte.
  EXPECT_EQ(fo4::swf::encode_matrix(SwfTransform{}), (std::vector<std::byte>{std::byte{0x00}}));

  // Constraint: 20 / -20 twips must use six signed bits per axis after the 0,0 flags.
  const auto translated =
      fo4::swf::encode_matrix(transform_of(65536, 65536, 0, 0, 20, -20));
  EXPECT_EQ(translated,
            (std::vector<std::byte>{std::byte{0x0C}, std::byte{0xA5}, std::byte{0x80}}));
  // Constraint: a 2.0 scale must use nineteen signed bits and set the leading flag.
  const auto scaled = fo4::swf::encode_matrix(transform_of(131072, 65536, 0, 0, 20, -20));
  ASSERT_EQ(scaled.size(), 8u);
  EXPECT_EQ(std::to_integer<unsigned>(scaled.front()) & 0x80u, 0x80u);
  // Constraint: rotate and negative scale take a 17-bit field each, so 85 bits pad to eleven bytes.
  const auto rotated = fo4::swf::encode_matrix(transform_of(-65536, 32768, 32768, -32768, 0, 0));
  EXPECT_EQ(rotated,
            (std::vector<std::byte>{std::byte{0xC6}, std::byte{0x00}, std::byte{0x00},
                                    std::byte{0x80}, std::byte{0x00}, std::byte{0xC5},
                                    std::byte{0x00}, std::byte{0x01}, std::byte{0x80},
                                    std::byte{0x00}, std::byte{0x00}}));
  // Constraint: a 2^15 scale does not fit 31 signed bits, so encoding must refuse it.
  SwfTransform huge;
  huge.scale_x = 32768.0;
  EXPECT_TRUE(fo4::swf::encode_matrix(huge).empty());
}

TEST(SwfEditTransform, RejectsATransformThatNeedsMoreThanTheMatrixFields) {
  const auto source = movie_of({{26, place2(3, 9, "Hat", Matrix{}, std::nullopt)}, {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hat = find_target(targets, "Hat");
  ASSERT_NE(hat, nullptr);

  SwfTransform overflow;
  overflow.scale_x = -16385.0;
  SwfTransformEditResult result;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{hat->instance_id, overflow}}, result));
  EXPECT_NE(result.message.find("does not fit"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfEditTransform, SeparatesAnEmptyFrameFromAnOutOfRangeFrame) {
  const auto source = movie_of({{1, {}}, {1, {}}});
  auto file = open(source);
  const SwfInstanceTransformEdit edit{1u, SwfTransform{}};

  SwfTransformEditResult empty_frame;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(file, 0, {edit}, empty_frame));
  EXPECT_NE(empty_frame.message.find("no instances"), std::string::npos) << empty_frame.message;

  SwfTransformEditResult out_of_range;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(file, 9, {edit}, out_of_range));
  EXPECT_NE(out_of_range.message.find("out of range"), std::string::npos) << out_of_range.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfEditTransform, RewritesTheMatrixAndLeavesEveryOtherPayloadByteIdentical) {
  const auto source = movie_of({{26, place2(3, 9, "Hat", Matrix{}, 7)}, {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hat = find_target(targets, "Hat");
  ASSERT_NE(hat, nullptr);
  EXPECT_TRUE(hat->has_authored_matrix);
  EXPECT_FALSE(hat->inside_sprite);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{hat->instance_id, transform_of(131072, 65536, 0, 0, 40, -60)}},
      result))
      << result.message;

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_EQ(reread.tags.size(), file.tags.size());
  ASSERT_TRUE(reread.tags[0].placement.has_value());
  const auto& placement = *reread.tags[0].placement;
  EXPECT_EQ(placement.depth, 3);
  ASSERT_TRUE(placement.character_id.has_value());
  EXPECT_EQ(*placement.character_id, 9);
  ASSERT_TRUE(placement.name.has_value());
  EXPECT_EQ(*placement.name, "Hat");
  ASSERT_TRUE(placement.clip_depth.has_value());
  EXPECT_EQ(*placement.clip_depth, 7);
  ASSERT_TRUE(placement.matrix.has_value());
  EXPECT_EQ(placement.matrix->scale_x, 131072);
  EXPECT_EQ(placement.matrix->scale_y, 65536);
  EXPECT_EQ(placement.matrix->translate_x, 40);
  EXPECT_EQ(placement.matrix->translate_y, -60);

  // Constraint: every tag but the edited placement must keep its bytes, including End.
  const auto before = payloads_of(open(source));
  const auto after = payloads_of(reread);
  ASSERT_EQ(before.size(), after.size());
  for (std::size_t index = 1; index < before.size(); ++index) {
    EXPECT_EQ(before[index], after[index]) << "tag " << index;
  }
}

TEST(SwfEditTransform, KeepsAnUnchangedTransformByteExact) {
  const auto source = movie_of({{26, place2(3, 9, "Hat", Matrix{}, std::nullopt)}, {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hat = find_target(targets, "Hat");
  ASSERT_NE(hat, nullptr);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{hat->instance_id, SwfTransform{}}}, result))
      << result.message;
  // An identity request on an identity placement must not normalise the authored encoding.
  EXPECT_TRUE(result.replaced_tag_indices.empty());
  EXPECT_EQ(fo4::swf::write_swf(file), source);
  EXPECT_EQ(file.tags[0].payload, open(source).tags[0].payload);
}

TEST(SwfEditTransform, RejectsAnInstanceWithNoAuthoredMatrix) {
  // Frame 1 replaces the object without a MATRIX, so this lifetime must refuse an edit.
  const auto source = movie_of({{26, place2(3, 9, "Hat", Matrix{}, std::nullopt)},
                                {1, {}},
                                {28, remove2(3)},
                                {26, place2_without_matrix(3, 9)},
                                {1, {}}});
  auto file = open(source);

  const auto later = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* fresh = find_target(later, "#9");
  ASSERT_NE(fresh, nullptr);
  EXPECT_FALSE(fresh->has_authored_matrix);

  SwfTransformEditResult result;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(
      file, 1, {SwfInstanceTransformEdit{fresh->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)}},
      result));
  EXPECT_NE(result.message.find("editable explicit matrix"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);

  // Frame 0 still has an authored matrix, so that target must stay editable.
  const auto earlier = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* authored = find_target(earlier, "Hat");
  ASSERT_NE(authored, nullptr);
  EXPECT_TRUE(authored->has_authored_matrix);
  EXPECT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{authored->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)}},
      result))
      << result.message;
}

TEST(SwfEditTransform, EditsTwoInstancesOfOneSharedPlacementInOnePlan) {
  // One sprite child is visible twice, so both ids must name the same authored bytes.
  const auto source = movie_of({{39, sprite_payload(42, 1, {{26, place2(3, 9, "child", Matrix{}, std::nullopt)}, {1, {}}})},
                                {26, place2(5, 42, "left", Matrix{}, std::nullopt)},
                                {26, place2(6, 42, "right", Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* left = find_target(targets, "left/child");
  const auto* right = find_target(targets, "right/child");
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_TRUE(left->inside_sprite);
  EXPECT_EQ(left->sprite_id, 42);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0,
      {SwfInstanceTransformEdit{left->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)},
       SwfInstanceTransformEdit{right->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)}},
      result))
      << result.message;
  ASSERT_EQ(result.replaced_tag_indices.size(), 1u);
  // Constraint: both ids address one authored placement, so one write must move that placement once.
  EXPECT_EQ(left->sprite_tag_index, right->sprite_tag_index);
  EXPECT_EQ(left->transform_tag_index, right->transform_tag_index);
  const auto written = fo4::swf::write_swf(file);
  EXPECT_NE(written, source);
  const auto reread = open(written);
  ASSERT_EQ(reread.tags.size(), 5u);
  ASSERT_EQ(reread.tags[0].children.size(), 3u);
  const auto& rewritten = reread.tags[0].children[0];
  ASSERT_TRUE(rewritten.placement.has_value());
  ASSERT_TRUE(rewritten.placement->matrix.has_value());
  EXPECT_EQ(rewritten.placement->matrix->translate_x, 20);
  EXPECT_EQ(rewritten.placement->matrix->translate_y, 0);
  // Constraint: the two referencing placements carry no authored transform, so their bytes stay.
  EXPECT_EQ(reread.tags[1].payload, open(source).tags[1].payload);
  EXPECT_EQ(reread.tags[2].payload, open(source).tags[2].payload);

  // Constraint: two different transforms for one shared placement must conflict.
  auto fresh = open(source);
  SwfTransformEditResult conflicted;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(
      fresh, 0,
      {SwfInstanceTransformEdit{left->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)},
       SwfInstanceTransformEdit{right->instance_id, transform_of(65536, 65536, 0, 0, 40, 0)}},
      conflicted));
  EXPECT_NE(conflicted.message.find("same shared authored placement"), std::string::npos)
      << conflicted.message;
  EXPECT_EQ(fo4::swf::write_swf(fresh), source);
}

TEST(SwfEditTransform, RewritesASpriteChildPlacementInsideItsDefineSprite) {
  const auto source = movie_of({{39, sprite_payload(42, 1, {{26, place2(3, 9, "child", Matrix{}, 4)}, {1, {}}})},
                                {26, place2(5, 42, "hero", Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* child = find_target(targets, "hero/child");
  ASSERT_NE(child, nullptr);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{child->instance_id, transform_of(65536, 65536, 0, 0, 60, 20)}},
      result))
      << result.message;
  ASSERT_EQ(result.replaced_tag_indices.size(), 1u);
  EXPECT_EQ(result.replaced_tag_indices[0], 0u);  // the DefineSprite tag, not the child

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_EQ(reread.tags.size(), 4u);
  ASSERT_TRUE(reread.tags[0].sprite_id.has_value());
  EXPECT_EQ(*reread.tags[0].sprite_id, 42);
  ASSERT_TRUE(reread.tags[0].sprite_frame_count.has_value());
  EXPECT_EQ(*reread.tags[0].sprite_frame_count, 1);
  // Constraint: the rebuild must keep the child, the ShowFrame and the End tag.
  ASSERT_EQ(reread.tags[0].children.size(), 3u);
  const auto& rewritten = reread.tags[0].children[0];
  ASSERT_TRUE(rewritten.placement.has_value());
  ASSERT_TRUE(rewritten.placement->matrix.has_value());
  EXPECT_EQ(rewritten.placement->matrix->translate_x, 60);
  EXPECT_EQ(rewritten.placement->matrix->translate_y, 20);
  EXPECT_EQ(rewritten.placement->depth, 3);
  ASSERT_TRUE(rewritten.placement->clip_depth.has_value());
  EXPECT_EQ(*rewritten.placement->clip_depth, 4);
  ASSERT_TRUE(rewritten.placement->name.has_value());
  EXPECT_EQ(*rewritten.placement->name, "child");
}

TEST(SwfEditTransform, WritesAFreshMatrixAndReparsesTheWrittenFile) {
  const auto source = movie_of({{26, place2(3, 9, "Hat", Matrix{98304, 32768, 16384, -8192, 250, -60}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hat = find_target(targets, "Hat");
  ASSERT_NE(hat, nullptr);

  const auto requested = transform_of(131072, 65536, 0, 0, 40, -60);
  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{hat->instance_id, requested}}, result))
      << result.message;

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_TRUE(reread.tags[0].placement.has_value());
  ASSERT_TRUE(reread.tags[0].placement->matrix.has_value());
  const auto& matrix = *reread.tags[0].placement->matrix;
  EXPECT_EQ(matrix.scale_x, 131072);
  EXPECT_EQ(matrix.scale_y, 65536);
  EXPECT_EQ(matrix.rotate_skew0, 0);
  EXPECT_EQ(matrix.rotate_skew1, 0);
  EXPECT_EQ(matrix.translate_x, 40);
  EXPECT_EQ(matrix.translate_y, -60);
  ASSERT_TRUE(reread.tags[0].placement->name.has_value());
  EXPECT_EQ(*reread.tags[0].placement->name, "Hat");
}

TEST(SwfEditTransform, EditsTheEarlierPlacementThatAuthoredTheVisibleMatrix) {
  // Frame 1 re-authors the matrix on a Move, so the edit must rewrite that Move, not the creation.
  const auto source = movie_of({{26, place2(3, 9, "Hat", Matrix{}, std::nullopt)},
                                {1, {}},
                                {26, move2_matrix(3, Matrix{65536, 65536, 0, 0, 100, 0})},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* hat = find_target(targets, "Hat");
  ASSERT_NE(hat, nullptr);
  EXPECT_TRUE(hat->has_authored_matrix);
  EXPECT_EQ(hat->transform_tag_index, 2u);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 1, {SwfInstanceTransformEdit{hat->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)}},
      result))
      << result.message;
  ASSERT_EQ(result.replaced_tag_indices.size(), 1u);
  EXPECT_EQ(result.replaced_tag_indices[0], 2u);

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_TRUE(reread.tags[2].placement.has_value());
  EXPECT_TRUE(reread.tags[2].placement->move);
  EXPECT_FALSE(reread.tags[2].placement->character_id.has_value());
  ASSERT_TRUE(reread.tags[2].placement->matrix.has_value());
  EXPECT_EQ(reread.tags[2].placement->matrix->translate_x, 20);
  // Constraint: the creating placement must keep its bytes and its name.
  EXPECT_EQ(reread.tags[0].payload, open(source).tags[0].payload);
  ASSERT_TRUE(reread.tags[0].placement->name.has_value());
  EXPECT_EQ(*reread.tags[0].placement->name, "Hat");

  const auto settled = fo4::swf::write_swf(file);
  SwfTransformEditResult unknown;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(
      file, 1, {SwfInstanceTransformEdit{999999u, SwfTransform{}}}, unknown));
  EXPECT_NE(unknown.message.find("not found"), std::string::npos) << unknown.message;
  EXPECT_EQ(fo4::swf::write_swf(file), settled);
}

TEST(SwfEditTransform, RewritesAVersionOnePlacementMatrix) {
  // Constraint: a v1 body has no flag byte and a CXFORM after the MATRIX, so the splice must keep it.
  const auto source = movie_of({{4, place1(9, 3, Matrix{})}, {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hat = find_target(targets, "#9");
  ASSERT_NE(hat, nullptr);
  EXPECT_TRUE(hat->has_authored_matrix);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{hat->instance_id, transform_of(65536, 65536, 0, 0, 40, -20)}},
      result))
      << result.message;

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_TRUE(reread.tags[0].placement.has_value());
  ASSERT_TRUE(reread.tags[0].placement->matrix.has_value());
  EXPECT_EQ(reread.tags[0].placement->matrix->translate_x, 40);
  EXPECT_EQ(reread.tags[0].placement->matrix->translate_y, -20);
  ASSERT_TRUE(reread.tags[0].placement->color_transform.has_value());
  EXPECT_EQ(reread.tags[0].placement->color_transform->multiply[0], 64);
}

TEST(SwfEditTransform, RefusesASpriteBodyTheChildListDoesNotExplain) {
  // Constraint: bytes after the sprite's End cannot be re-emitted, so the edit must refuse.
  SwfBuilder trailing;
  trailing.u16(42);
  trailing.u16(1);
  trailing.tag(26, place2(3, 9, "child", Matrix{}, std::nullopt), false);
  trailing.tag(1);
  trailing.tag(0);
  trailing.u8(0xAB);
  trailing.u8(0xCD);
  const auto source = movie_of({{39, trailing.body},
                                {26, place2(5, 42, "hero", Matrix{}, std::nullopt)},
                                {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* child = find_target(targets, "hero/child");
  ASSERT_NE(child, nullptr);

  SwfTransformEditResult result;
  EXPECT_FALSE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{child->instance_id, transform_of(65536, 65536, 0, 0, 20, 0)}},
      result));
  EXPECT_NE(result.message.find("could not be rebuilt"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfEditTransform, RewritesAPlaceObjectThreeMatrixAndKeepsTheBlendMode) {
  // Constraint: the splice must not read the second flag byte or the blend mode as MATRIX bytes.
  const auto source = movie_of({{70, place3(3, 9, "Hat", Matrix{}, 7)}, {1, {}}});
  auto file = open(source);

  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* hat = find_target(targets, "Hat");
  ASSERT_NE(hat, nullptr);
  EXPECT_TRUE(hat->has_authored_matrix);

  SwfTransformEditResult result;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 0, {SwfInstanceTransformEdit{hat->instance_id, transform_of(65536, 65536, 0, 0, -40, 60)}},
      result))
      << result.message;

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_TRUE(reread.tags[0].placement.has_value());
  const auto& placement = *reread.tags[0].placement;
  ASSERT_TRUE(placement.matrix.has_value());
  EXPECT_EQ(placement.matrix->translate_x, -40);
  EXPECT_EQ(placement.matrix->translate_y, 60);
  ASSERT_TRUE(placement.blend_mode.has_value());
  EXPECT_EQ(*placement.blend_mode, 7);
  ASSERT_TRUE(placement.name.has_value());
  EXPECT_EQ(*placement.name, "Hat");
}

TEST(SwfEditBitmap, ReplacesALosslessCharacterAndItsPngKeepsThePremultiplyRounding) {
  auto file = open(movie_of({{20, lossless_raw_payload(5, 2, 2, 0, zeros(16))}, {1, {}}}));
  const auto pixels = rgba_2x2();
  ASSERT_EQ(replace_bitmap_lossless(file, 3, 2, 2, pixels, true), SwfBitmapEditStatus::ok);

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_EQ(reread.tags.size(), 3u);
  ASSERT_TRUE(reread.tags[0].bitmap.has_value());
  EXPECT_EQ(reread.tags[0].code, 36);
  EXPECT_TRUE(reread.tags[0].bitmap->has_alpha);
  EXPECT_EQ(head_of(reread.tags[0].payload, 7), (std::vector<std::uint8_t>{3, 0, 5, 2, 0, 2, 0}));
  EXPECT_EQ(inflated(reread.tags[0].bitmap->packed, 16),
            (std::vector<std::uint8_t>{255, 10, 20, 30, 128, 100, 50, 1, 0, 0, 0, 0, 128, 128, 64, 0}));

  const auto png = fo4::swf::bitmap_to_png(*reread.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  const auto chunks = png_chunks(*png);
  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_EQ(inflated(std::as_bytes(chunks[1].data), 18),
            (std::vector<std::uint8_t>{0, 10, 20, 30, 255, 199, 100, 2, 128, 0, 0, 0, 0, 0, 255, 128, 0, 128}));
}

TEST(SwfEditBitmap, SwitchesToTagTwentyAndDropsTheAlphaChannelWhenHasAlphaIsFalse) {
  auto file = open(movie_of({{36, lossless_raw_payload(5, 2, 2, 0, zeros(16))}, {1, {}}}));
  const auto pixels = rgba_2x2();
  ASSERT_EQ(replace_bitmap_lossless(file, 3, 2, 2, pixels, false), SwfBitmapEditStatus::ok);

  const auto reread = open(fo4::swf::write_swf(file));
  ASSERT_EQ(reread.tags.size(), 3u);
  ASSERT_TRUE(reread.tags[0].bitmap.has_value());
  EXPECT_EQ(reread.tags[0].code, 20);
  EXPECT_FALSE(reread.tags[0].bitmap->has_alpha);
  EXPECT_EQ(head_of(reread.tags[0].payload, 7), (std::vector<std::uint8_t>{3, 0, 5, 2, 0, 2, 0}));
  EXPECT_EQ(inflated(reread.tags[0].bitmap->packed, 16),
            (std::vector<std::uint8_t>{0, 10, 20, 30, 0, 200, 100, 1, 0, 0, 0, 0, 0, 255, 128, 0}));

  const auto png = fo4::swf::bitmap_to_png(*reread.tags[0].bitmap);
  ASSERT_TRUE(png.has_value());
  const auto chunks = png_chunks(*png);
  ASSERT_EQ(chunks.size(), 3u);
  EXPECT_EQ(inflated(std::as_bytes(chunks[1].data), 18),
            (std::vector<std::uint8_t>{0, 10, 20, 30, 255, 200, 100, 1, 255, 0, 0, 0, 0, 255, 255, 128, 0, 255}));
}

TEST(SwfEditBitmap, KeepsTheCharacterIdAndEveryOtherTagUnchanged) {
  auto second_shape = define_shape_3_payload();
  second_shape[0] = std::byte{6};
  const auto source = movie_of({{32, define_shape_3_payload()},
                                {20, lossless_raw_payload(5, 2, 2, 0, zeros(16))},
                                {32, second_shape},
                                {1, {}}});
  auto file = open(source);
  const auto pixels = rgba_2x2();
  ASSERT_EQ(replace_bitmap_lossless(file, 3, 2, 2, pixels, true), SwfBitmapEditStatus::ok);

  const auto before = open(source);
  const auto after = open(fo4::swf::write_swf(file));
  ASSERT_EQ(after.tags.size(), before.tags.size());
  for (std::size_t index = 0; index < before.tags.size(); ++index) {
    if (index == 1) {
      EXPECT_EQ(after.tags[index].code, 36);
      ASSERT_TRUE(after.tags[index].bitmap.has_value());
      EXPECT_EQ(after.tags[index].bitmap->character_id, 3);
      continue;
    }
    EXPECT_EQ(after.tags[index].code, before.tags[index].code) << "tag " << index;
    EXPECT_EQ(after.tags[index].payload, before.tags[index].payload) << "tag " << index;
  }
}

TEST(SwfEditBitmap, RefusesAnUnknownOrNonLosslessCharacterAndLeavesTheFileUntouched) {
  SwfBuilder jpeg;
  jpeg.u16(8);
  jpeg.bytes(minimal_jpeg(2, 2));
  auto file = open(movie_of({{32, define_shape_3_payload()},
                             {21, jpeg.body},
                             {20, lossless_raw_payload(5, 2, 2, 0, zeros(16))},
                             {1, {}}}));
  const auto before = fo4::swf::write_swf(file);
  const auto pixels = rgba_2x2();
  EXPECT_EQ(replace_bitmap_lossless(file, 9, 2, 2, pixels, true),
            SwfBitmapEditStatus::unknown_character);
  EXPECT_EQ(replace_bitmap_lossless(file, 5, 2, 2, pixels, true),
            SwfBitmapEditStatus::not_a_lossless_bitmap);
  EXPECT_EQ(replace_bitmap_lossless(file, 8, 2, 2, pixels, true),
            SwfBitmapEditStatus::not_a_lossless_bitmap);
  EXPECT_EQ(fo4::swf::write_swf(file), before);
}

TEST(SwfEditBitmap, RefusesZeroOrOversizedDimensionsAndAPixelBufferOfTheWrongSize) {
  auto file = open(movie_of({{20, lossless_raw_payload(5, 2, 2, 0, zeros(16))}, {1, {}}}));
  const auto before = fo4::swf::write_swf(file);
  const std::vector<std::uint8_t> none;
  EXPECT_EQ(replace_bitmap_lossless(file, 3, 0, 2, none, true),
            SwfBitmapEditStatus::bad_dimensions);
  EXPECT_EQ(replace_bitmap_lossless(file, 3, 2, 0, none, true),
            SwfBitmapEditStatus::bad_dimensions);
  EXPECT_EQ(replace_bitmap_lossless(file, 3, 4097, 4096, none, true),
            SwfBitmapEditStatus::bad_dimensions);
  EXPECT_EQ(replace_bitmap_lossless(file, 3, 2, 2, zeros(15), true),
            SwfBitmapEditStatus::pixel_size_mismatch);
  EXPECT_EQ(replace_bitmap_lossless(file, 3, 2, 2, zeros(17), false),
            SwfBitmapEditStatus::pixel_size_mismatch);
  EXPECT_EQ(fo4::swf::write_swf(file), before);
}
