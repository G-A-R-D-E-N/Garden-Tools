#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_display.hpp"
#include "fo4/swf/swf_edit.hpp"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::swf::SwfAuthoredFrame;
using fo4::swf::SwfAuthoredFrameObject;
using fo4::swf::SwfDisplayFrame;
using fo4::swf::SwfFile;
using fo4::swf::SwfInstanceEditTarget;
using fo4::swf::SwfInstanceTransformEdit;
using fo4::swf::SwfMatrix;
using fo4::swf::SwfKeyframeInsertResult;
using fo4::swf::SwfTransform;
using fo4::swf::SwfTransformEditResult;
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

// Constraint: a full authored source must carry the character, the MATRIX and the name.
std::vector<std::byte> place2(std::uint16_t depth, std::uint16_t character, std::string_view name,
                              const Matrix& matrix) {
  SwfBuilder builder;
  builder.u8(0x26);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.text(name);
  return builder.body;
}

// Constraint: no MATRIX, so this object must start with no explicit transform source.
std::vector<std::byte> place2_without_matrix(std::uint16_t depth, std::uint16_t character,
                                             std::string_view name) {
  SwfBuilder builder;
  builder.u8(0x22);
  builder.u16(depth);
  builder.u16(character);
  builder.text(name);
  return builder.body;
}

// Constraint: HasCharacter|HasMatrix|HasRatio|HasName|HasClipDepth, ratio before clip depth and name.
std::vector<std::byte> place2_ratio_clip(std::uint16_t depth, std::uint16_t character,
                                         std::string_view name, const Matrix& matrix,
                                         std::uint16_t ratio, std::uint16_t clip_depth) {
  SwfBuilder builder;
  builder.u8(0x76);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.u16(ratio);
  builder.text(name);
  builder.u16(clip_depth);
  return builder.body;
}

// Constraint: PlaceObject3 authors a blur filter and a blend mode after the placement fields.
std::vector<std::byte> place3_blend_filter(std::uint16_t depth, std::uint16_t character,
                                           std::string_view name, const Matrix& matrix,
                                           std::uint8_t blend) {
  SwfBuilder builder;
  builder.u8(0x26);
  builder.u8(0x03);
  builder.u16(depth);
  builder.u16(character);
  push_matrix(builder, matrix);
  builder.text(name);
  builder.u8(1);
  builder.u8(1);
  for (int index = 0; index < 9; ++index) {
    builder.u8(0);
  }
  builder.u8(blend);
  return builder.body;
}

// Move|HasMatrix without a character: the shape a keyframe must reproduce.
std::vector<std::byte> move2_matrix(std::uint16_t depth, const Matrix& matrix) {
  SwfBuilder builder;
  builder.u8(0x05);
  builder.u16(depth);
  push_matrix(builder, matrix);
  return builder.body;
}

// Constraint: a name-only Move must change nothing but the name of a live depth.
std::vector<std::byte> move2_name(std::uint16_t depth, std::string_view name) {
  SwfBuilder builder;
  builder.u8(0x21);
  builder.u16(depth);
  builder.text(name);
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

std::vector<std::byte> movie_of(const std::vector<TagPayload>& tags, std::uint8_t version = 10) {
  SwfBuilder builder;
  builder.rect(0, 11000, 0, 8000, 15);
  builder.u16(0x0C00);
  builder.u16(1);
  for (const auto& [code, payload] : tags) {
    builder.tag(code, payload, false);
  }
  builder.tag(0);
  return wrap_body(builder.body, version, false, false);
}

SwfFile open(const std::vector<std::byte>& bytes) {
  const auto result = fo4::swf::read_swf(bytes);
  EXPECT_EQ(result.status, fo4::swf::SwfStatus::ok) << result.message;
  return result.file;
}

const SwfInstanceEditTarget* find_target(const std::vector<SwfInstanceEditTarget>& targets,
                                         std::string_view path) {
  const auto found = std::find_if(targets.begin(), targets.end(),
                                  [&](const SwfInstanceEditTarget& target) {
                                    return target.path == path;
                                  });
  return found == targets.end() ? nullptr : &*found;
}

const SwfInstanceEditTarget* find_child(const std::vector<SwfInstanceEditTarget>& targets) {
  const auto found = std::find_if(targets.begin(), targets.end(),
                                  [&](const SwfInstanceEditTarget& target) {
                                    return target.inside_sprite;
                                  });
  return found == targets.end() ? nullptr : &*found;
}

std::vector<SwfDisplayFrame> root_display_frames(const SwfFile& file) {
  const auto display = fo4::swf::build_display_list(file);
  if (display.timelines.empty()) {
    return {};
  }
  return display.timelines.front().frames;
}

// Constraint: the authored object is identified by its creating tag, so a later Move cannot shadow it.
const SwfAuthoredFrameObject* authored_at(const SwfAuthoredFrame& frame, std::size_t create_tag_index) {
  const auto found = std::find_if(frame.objects.begin(), frame.objects.end(),
                                  [&](const SwfAuthoredFrameObject& object) {
                                    return object.create_tag_index == create_tag_index;
                                  });
  return found == frame.objects.end() ? nullptr : &*found;
}

SwfTransform transform_of(std::int32_t translate_x, std::int32_t translate_y) {
  SwfTransform transform;
  transform.translate_x = static_cast<double>(translate_x) / 20.0;
  transform.translate_y = static_cast<double>(translate_y) / 20.0;
  return transform;
}

}  // namespace

TEST(SwfKeyframe, InsertsARenderNeutralMoveBeforeTheFramesShowFrame) {
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{65536, 65536, 0, 0, 100, 0})},
                                {1, {}},
                                {1, {}},
                                {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  ASSERT_EQ(before.size(), 3u);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result))
      << result.message;
  EXPECT_EQ(result.root_frame, 1u);
  EXPECT_EQ(result.depth, 5);
  EXPECT_EQ(result.inserted_tag_index, 2u);
  ASSERT_EQ(file.tags.size(), 6u);

  // Constraint: the inserted tag must be a matrix-only Move with no character, name or clip depth.
  const auto& inserted = file.tags[2];
  ASSERT_EQ(inserted.code, 26u);
  ASSERT_TRUE(inserted.placement.has_value());
  EXPECT_TRUE(inserted.placement->move);
  EXPECT_FALSE(inserted.placement->character_id.has_value());
  EXPECT_FALSE(inserted.placement->name.has_value());
  ASSERT_TRUE(inserted.placement->matrix.has_value());
  EXPECT_EQ(*inserted.placement->matrix, (SwfMatrix{65536, 65536, 0, 0, 100, 0}));

  // Constraint: every tag outside the insertion point must keep its bytes.
  const auto pristine = open(source);
  EXPECT_EQ(file.tags[0].payload, pristine.tags[0].payload);
  EXPECT_EQ(file.tags[1].payload, pristine.tags[1].payload);
  for (std::size_t index = 2; index < pristine.tags.size(); ++index) {
    EXPECT_EQ(file.tags[index + 1].payload, pristine.tags[index].payload);
  }

  // Constraint: a neutral keyframe must leave every rendered frame unchanged.
  EXPECT_EQ(root_display_frames(file), before);
  const auto reparsed = open(fo4::swf::write_swf(file));
  EXPECT_EQ(root_display_frames(reparsed), before);
  const auto reread = fo4::swf::build_instance_edit_targets(reparsed, 1);
  EXPECT_NE(find_target(reread, "widget"), nullptr);
}

TEST(SwfKeyframe, MaterializesIdentityWhenTheObjectAuthoredNoMatrix) {
  const auto source =
      movie_of({{26, place2_without_matrix(5, 7, "widget")}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);
  EXPECT_FALSE(widget->has_authored_matrix);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result))
      << result.message;

  SwfAuthoredFrame frame;
  std::string message;
  ASSERT_TRUE(fo4::swf::build_authored_frame(file.tags, 1, frame, message)) << message;
  const auto* object = authored_at(frame, widget->create_tag_index);
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(object->matrix.has_value());
  EXPECT_EQ(*object->matrix, SwfMatrix{});

  const auto after = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* refreshed = find_target(after, "widget");
  ASSERT_NE(refreshed, nullptr);
  EXPECT_TRUE(refreshed->has_authored_matrix);
  EXPECT_EQ(refreshed->transform_tag_index, result.inserted_tag_index);
  EXPECT_EQ(root_display_frames(file), before);
}

TEST(SwfKeyframe, TheInsertedPlacementBecomesTheEditableTransformSource) {
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})}, {1, {}}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult inserted;
  ASSERT_TRUE(
      fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, inserted))
      << inserted.message;

  const auto refreshed = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* edit_target = find_target(refreshed, "widget");
  ASSERT_NE(edit_target, nullptr);
  EXPECT_EQ(edit_target->transform_tag_index, inserted.inserted_tag_index);

  SwfTransformEditResult edit;
  ASSERT_TRUE(fo4::swf::apply_instance_transforms(
      file, 1, {SwfInstanceTransformEdit{edit_target->instance_id, transform_of(400, 0)}}, edit))
      << edit.message;

  // Constraint: the keyframe must own the transform from its frame forward and nothing before it.
  const auto frames = root_display_frames(file);
  ASSERT_EQ(frames.size(), 3u);
  ASSERT_EQ(frames[0].instances.size(), 1u);
  EXPECT_EQ(frames[0].instances[0].transform.translate_x, 0.0);
  EXPECT_EQ(frames[1].instances[0].transform.translate_x, 20.0);
  EXPECT_EQ(frames[2].instances[0].transform.translate_x, 20.0);
}

TEST(SwfKeyframe, RejectsASecondKeyframeOnTheSameFrame) {
  const auto source =
      movie_of({{26, place2_without_matrix(5, 7, "widget")}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult first;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, first))
      << first.message;
  const auto after_first = fo4::swf::write_swf(file);

  fo4::swf::SwfKeyframeInsertResult second;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, second));
  EXPECT_NE(second.message.find("already has an explicit transform source"), std::string::npos)
      << second.message;
  EXPECT_EQ(fo4::swf::write_swf(file), after_first);
}

TEST(SwfKeyframe, RejectsAFrameThatAlreadyAuthoredTheMatrixInForce) {
  // Constraint: the value already in force on that frame must leave the file byte-identical.
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})},
                                {1, {}},
                                {26, move2_matrix(5, Matrix{})},
                                {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);
  ASSERT_EQ(widget->transform_tag_index, 2u);

  fo4::swf::SwfKeyframeInsertResult result;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result));
  EXPECT_NE(result.message.find("already has an explicit transform source"), std::string::npos)
      << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);

  fo4::swf::SwfKeyframeInsertResult requested;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id,
                                                            transform_of(400, 0), requested));
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, RejectsANestedSpriteInstance) {
  const auto source =
      movie_of({{39, sprite_payload(42, 1, {{26, place2(1, 7, "child", Matrix{})}, {1, {}}})},
                {26, place2(5, 42, "hero", Matrix{})},
                {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 0);
  const auto* child = find_child(targets);
  ASSERT_NE(child, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 0, child->instance_id, result));
  EXPECT_NE(result.message.find("root timeline"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, RejectsAPreSwf3Movie) {
  const auto source =
      movie_of({{26, place2_without_matrix(5, 7, "widget")}, {1, {}}, {1, {}}}, 2);
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result));
  EXPECT_NE(result.message.find("version 3"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, RefusesWhenTheReadSideCannotReproduceTheMovie) {
  // Constraint: a move to a depth that never exists must make the whole movie unplaceable.
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})},
                                {1, {}},
                                {26, move2_matrix(9, Matrix{})},
                                {1, {}}});
  auto file = open(source);
  fo4::swf::SwfKeyframeInsertResult result;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 0, 1, result));
  EXPECT_FALSE(result.message.empty());
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, WritesTheRequestedTransformFromTheTargetFrameForward) {
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})}, {1, {}}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);
  const auto before = root_display_frames(file);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(
      file, 1, widget->instance_id, transform_of(400, -80), result))
      << result.message;

  const auto frames = root_display_frames(file);
  ASSERT_EQ(frames.size(), 3u);
  EXPECT_EQ(frames[0].instances, before[0].instances);
  EXPECT_EQ(frames[1].instances[0].transform.translate_x, 20.0);
  EXPECT_EQ(frames[1].instances[0].transform.translate_y, -4.0);
  EXPECT_EQ(frames[2].instances[0].transform.translate_x, 20.0);

  const auto reparsed = open(fo4::swf::write_swf(file));
  const auto reread = root_display_frames(reparsed);
  ASSERT_EQ(reread.size(), 3u);
  EXPECT_EQ(reread[1].instances[0].transform.translate_x, 20.0);

  // Constraint: the inserted tag must stay the canonical matrix-only Move.
  const auto& inserted = file.tags[result.inserted_tag_index];
  ASSERT_EQ(inserted.code, 26u);
  ASSERT_TRUE(inserted.placement.has_value());
  EXPECT_TRUE(inserted.placement->move);
  EXPECT_FALSE(inserted.placement->character_id.has_value());
}

TEST(SwfKeyframe, ALaterAuthoredMatrixRegainsOwnership) {
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})},
                                {1, {}},
                                {1, {}},
                                {26, move2_matrix(5, Matrix{65536, 65536, 0, 0, 200, 0})},
                                {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(
      file, 1, widget->instance_id, transform_of(400, 0), result))
      << result.message;

  const auto frames = root_display_frames(file);
  ASSERT_EQ(frames.size(), 3u);
  EXPECT_EQ(frames[1].instances[0].transform.translate_x, 20.0);
  EXPECT_EQ(frames[2].instances[0].transform.translate_x, 10.0);
}

TEST(SwfKeyframe, KeepsASameFrameNameMoveAndMakesTheKeyframeTheSource) {
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})},
                                {1, {}},
                                {26, move2_name(5, "renamed")},
                                {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* renamed = find_target(targets, "renamed");
  ASSERT_NE(renamed, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, renamed->instance_id, result))
      << result.message;

  const auto after = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* refreshed = find_target(after, "renamed");
  ASSERT_NE(refreshed, nullptr);
  EXPECT_EQ(refreshed->transform_tag_index, result.inserted_tag_index);
  // Constraint: a render-neutral keyframe must not disturb the name the same frame authored.
  EXPECT_EQ(root_display_frames(file), before);
}

TEST(SwfKeyframe, PreservesRatioAndClipDepthOnTheTargetFrame) {
  const auto source =
      movie_of({{26, place2_ratio_clip(5, 7, "widget", Matrix{}, 128, 9)}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result))
      << result.message;

  SwfAuthoredFrame frame;
  std::string message;
  ASSERT_TRUE(fo4::swf::build_authored_frame(file.tags, 1, frame, message)) << message;
  const auto* object = authored_at(frame, widget->create_tag_index);
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(object->ratio.has_value());
  EXPECT_EQ(*object->ratio, 128);
  ASSERT_TRUE(object->clip_depth.has_value());
  EXPECT_EQ(*object->clip_depth, 9);
  EXPECT_EQ(root_display_frames(file), before);
}

TEST(SwfKeyframe, PreservesPlaceObjectThreeFiltersAndBlendMode) {
  const auto source =
      movie_of({{70, place3_blend_filter(5, 7, "widget", Matrix{}, 5)}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result))
      << result.message;

  SwfAuthoredFrame frame;
  std::string message;
  ASSERT_TRUE(fo4::swf::build_authored_frame(file.tags, 1, frame, message)) << message;
  const auto* object = authored_at(frame, widget->create_tag_index);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->filter_count, 1);
  ASSERT_TRUE(object->blend_mode.has_value());
  EXPECT_EQ(*object->blend_mode, 5);
  EXPECT_EQ(root_display_frames(file), before);
}

TEST(SwfKeyframe, KeepsMaskMembershipOnTheTargetFrame) {
  // Constraint: the mask at depth three covers four to six, so the widget must stay masked by it.
  const auto source = movie_of({{26, place2(3, 9, "mask", Matrix{})},
                                {26, place2(5, 7, "widget", Matrix{})},
                                {1, {}},
                                {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result))
      << result.message;

  SwfAuthoredFrame frame;
  std::string message;
  ASSERT_TRUE(fo4::swf::build_authored_frame(file.tags, 1, frame, message)) << message;
  const auto* object = authored_at(frame, widget->create_tag_index);
  ASSERT_NE(object, nullptr);
  ASSERT_TRUE(object->masked_by_depth.has_value());
  EXPECT_EQ(*object->masked_by_depth, 3);
  EXPECT_EQ(root_display_frames(file), before);
}

TEST(SwfKeyframe, RejectsATransformTheMatrixFieldsCannotHold) {
  const auto source =
      movie_of({{26, place2(5, 7, "widget", Matrix{})}, {1, {}}, {1, {}}});
  auto file = open(source);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  SwfTransform nan = transform_of(400, 0);
  nan.scale_x = std::numeric_limits<double>::quiet_NaN();
  fo4::swf::SwfKeyframeInsertResult not_a_number;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id,
                                                            nan, not_a_number));
  EXPECT_EQ(fo4::swf::write_swf(file), source);

  SwfTransform overflowing;
  overflowing.scale_x = 32768.0;
  fo4::swf::SwfKeyframeInsertResult too_wide;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id,
                                                            overflowing, too_wide));
  EXPECT_NE(too_wide.message.find("fit the MATRIX fields"), std::string::npos)
      << too_wide.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, RejectsAnOutOfRangeRootFrame) {
  const auto source =
      movie_of({{26, place2(5, 7, "widget", Matrix{})}, {1, {}}, {1, {}}});
  auto file = open(source);

  fo4::swf::SwfKeyframeInsertResult result;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 9, 1, result));
  EXPECT_NE(result.message.find("no renderable root frame"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, RejectsAnUnknownInstanceId) {
  const auto source =
      movie_of({{26, place2(5, 7, "widget", Matrix{})}, {1, {}}, {1, {}}});
  auto file = open(source);

  fo4::swf::SwfKeyframeInsertResult result;
  EXPECT_FALSE(fo4::swf::insert_instance_transform_keyframe(file, 1, 999999u, result));
  EXPECT_NE(result.message.find("instance id not found"), std::string::npos) << result.message;
  EXPECT_EQ(fo4::swf::write_swf(file), source);
}

TEST(SwfKeyframe, InsertsAfterADoActionTag) {
  // Constraint: the inserted Move lands at the ShowFrame, so a DoAction earlier in the frame stays first.
  const auto source = movie_of({{26, place2(5, 7, "widget", Matrix{})},
                                {1, {}},
                                {12, {std::byte{0x00}}},
                                {1, {}}});
  auto file = open(source);
  const auto before = root_display_frames(file);
  const auto targets = fo4::swf::build_instance_edit_targets(file, 1);
  const auto* widget = find_target(targets, "widget");
  ASSERT_NE(widget, nullptr);

  fo4::swf::SwfKeyframeInsertResult result;
  ASSERT_TRUE(fo4::swf::insert_instance_transform_keyframe(file, 1, widget->instance_id, result))
      << result.message;
  ASSERT_LT(result.inserted_tag_index, file.tags.size());
  EXPECT_EQ(file.tags[result.inserted_tag_index].code, 26u);
  ASSERT_GT(result.inserted_tag_index, 0u);
  EXPECT_EQ(file.tags[result.inserted_tag_index - 1].code, 12u);
  EXPECT_EQ(root_display_frames(file), before);
}
TEST(SwfKeyframe, SweepsEveryRootInstanceAndFrameForRenderNeutrality) {

  const std::vector<std::vector<TagPayload>> corpus{
      {{26, place2(5, 7, "a", Matrix{})}, {1, {}}, {1, {}}, {1, {}}},
      {{26, place2(5, 7, "a", Matrix{})}, {1, {}}, {26, move2_name(5, "renamed")}, {1, {}}, {1, {}}},
      {{26, place2(5, 7, "a", Matrix{})},
       {26, place2(9, 8, "b", Matrix{})},
       {1, {}},
       {26, move2_name(5, "renamed-a")},
       {1, {}},
       {28, remove2(9)},
       {1, {}},
       {1, {}}},
      {{26, place2_without_matrix(5, 7, "a")},
       {1, {}},
       {1, {}},
       {26, move2_matrix(5, Matrix{})},
       {1, {}},
       {1, {}}},
  };

  std::size_t successes = 0;
  std::size_t refusals = 0;
  for (const auto& tags : corpus) {
    const auto source = movie_of(tags);
    const auto pristine = open(source);
    const auto before = root_display_frames(pristine);
    ASSERT_FALSE(before.empty());

    // Constraint: every frame and every root instance is swept, so a frame-local source is also visited.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> candidates;
    for (std::uint32_t frame = 0; frame < before.size(); ++frame) {
      for (const auto& target : fo4::swf::build_instance_edit_targets(pristine, frame)) {
        if (!target.inside_sprite) {
          candidates.emplace_back(frame, target.instance_id);
        }
      }
    }

    for (const auto& candidate : candidates) {
      auto post = open(source);
      fo4::swf::SwfKeyframeInsertResult result;
      if (fo4::swf::insert_instance_transform_keyframe(post, candidate.first, candidate.second,
                                                       result)) {
        ++successes;
        EXPECT_EQ(result.root_frame, candidate.first);
        // Constraint: insertion must be render-neutral on every frame, and survive a write and reparse.
        EXPECT_EQ(root_display_frames(post), before) << "frame " << candidate.first;
        const auto reparsed = open(fo4::swf::write_swf(post));
        EXPECT_EQ(root_display_frames(reparsed), before);
        const auto reread = fo4::swf::build_instance_edit_targets(reparsed, candidate.first);
        const auto found = std::find_if(reread.begin(), reread.end(),
                                        [&](const SwfInstanceEditTarget& target) {
                                          return target.instance_id == candidate.second;
                                        });
        EXPECT_NE(found, reread.end());
      } else {
        ++refusals;
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(fo4::swf::write_swf(post), source);
      }
    }
  }

  EXPECT_GT(successes, 0u);
  EXPECT_GT(refusals, 0u);
}
