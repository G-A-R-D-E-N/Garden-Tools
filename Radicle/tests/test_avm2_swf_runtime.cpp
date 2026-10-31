#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/text.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using fo4::avm2::DisplayObject;
using fo4::avm2::DisplayObjectContainer;
using fo4::avm2::Interpreter;
using fo4::avm2::MovieClip;
using fo4::avm2::Runtime;
using fo4::avm2::SwfMovieData;
using fo4::avm2::SwfRuntime;
using fo4::avm2::SwfRuntimeOptions;

fo4::swf::SwfTag make_tag(std::uint16_t code) {
  fo4::swf::SwfTag tag;
  tag.code = code;
  return tag;
}

fo4::swf::SwfTag place_tag(std::uint16_t depth, std::uint16_t character, double x, double y) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kPlaceObject2);
  fo4::swf::SwfPlacement placement;
  placement.depth = depth;
  placement.character_id = character;
  fo4::swf::SwfMatrix matrix;
  matrix.translate_x = static_cast<std::int32_t>(x * 20.0);
  matrix.translate_y = static_cast<std::int32_t>(y * 20.0);
  placement.matrix = matrix;
  tag.placement = placement;
  return tag;
}

fo4::swf::SwfTag place_tag_named(std::uint16_t depth, std::uint16_t character, const char* name) {
  fo4::swf::SwfTag tag = place_tag(depth, character, 0.0, 0.0);
  tag.placement->name = std::string{name};
  return tag;
}

fo4::swf::SwfTag remove_tag(std::uint16_t depth) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kRemoveObject2);
  fo4::swf::SwfRemoval removal;
  removal.depth = depth;
  tag.removal = removal;
  return tag;
}

fo4::swf::SwfTag move_tag(std::uint16_t depth, std::optional<fo4::swf::SwfMatrix> matrix) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kPlaceObject2);
  fo4::swf::SwfPlacement placement;
  placement.depth = depth;
  placement.move = true;
  placement.matrix = matrix;
  tag.placement = placement;
  return tag;
}

fo4::swf::SwfMatrix translated(double x, double y) {
  fo4::swf::SwfMatrix matrix;
  matrix.translate_x = static_cast<std::int32_t>(x * 20.0);
  matrix.translate_y = static_cast<std::int32_t>(y * 20.0);
  return matrix;
}

fo4::swf::SwfMatrix rotated_45() {
  fo4::swf::SwfMatrix matrix;
  matrix.scale_x = 46341;
  matrix.scale_y = 46341;
  matrix.rotate_skew0 = 46341;
  matrix.rotate_skew1 = -46341;
  return matrix;
}

fo4::swf::SwfFile build_frames_movie(std::vector<std::vector<fo4::swf::SwfTag>> frames) {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = static_cast<std::uint16_t>(frames.size());
  for (std::vector<fo4::swf::SwfTag>& frame : frames) {
    for (fo4::swf::SwfTag& tag : frame) {
      file.tags.push_back(std::move(tag));
    }
    file.tags.push_back(make_tag(fo4::swf::tag::kShowFrame));
  }
  return file;
}

fo4::swf::SwfTag label_tag(const char* name) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kFrameLabel);
  const std::string text{name};
  for (const char character : text) {
    tag.payload.push_back(static_cast<std::byte>(character));
  }
  tag.payload.push_back(std::byte{0});
  return tag;
}

fo4::swf::SwfTag symbol_class_tag(const std::vector<std::pair<std::uint16_t, std::string>>& symbols) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kSymbolClass);
  const std::uint16_t count = static_cast<std::uint16_t>(symbols.size());
  tag.payload.push_back(static_cast<std::byte>(count & 0xFFu));
  tag.payload.push_back(static_cast<std::byte>((count >> 8u) & 0xFFu));
  for (const auto& [character, name] : symbols) {
    tag.payload.push_back(static_cast<std::byte>(character & 0xFFu));
    tag.payload.push_back(static_cast<std::byte>((character >> 8u) & 0xFFu));
    for (const char letter : name) {
      tag.payload.push_back(static_cast<std::byte>(letter));
    }
    tag.payload.push_back(std::byte{0});
  }
  return tag;
}

fo4::swf::SwfTag sprite_tag(std::uint16_t sprite_id, std::uint16_t frames,
                            std::vector<fo4::swf::SwfTag> children) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kDefineSprite);
  tag.sprite_id = sprite_id;
  tag.sprite_frame_count = frames;
  tag.children = std::move(children);
  return tag;
}

fo4::swf::SwfFile build_movie(bool with_symbol_class) {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 3;
  file.header.frame_size.x_min = 0;
  file.header.frame_size.x_max = 16000;
  file.header.frame_size.y_min = 0;
  file.header.frame_size.y_max = 12000;

  std::vector<fo4::swf::SwfTag> sprite_children;
  sprite_children.push_back(place_tag(1, 100, 1.0, 2.0));
  sprite_children.push_back(make_tag(fo4::swf::tag::kShowFrame));
  sprite_children.push_back(place_tag(2, 101, 3.0, 4.0));
  sprite_children.push_back(make_tag(fo4::swf::tag::kShowFrame));

  std::vector<fo4::swf::SwfTag> root;
  root.push_back(label_tag("start"));
  root.push_back(sprite_tag(10, 2, std::move(sprite_children)));
  root.push_back(place_tag_named(1, 10, "loader"));
  root.push_back(make_tag(fo4::swf::tag::kShowFrame));
  root.push_back(place_tag(2, 5, 10.0, 20.0));
  root.push_back(make_tag(fo4::swf::tag::kShowFrame));
  root.push_back(remove_tag(1));
  root.push_back(make_tag(fo4::swf::tag::kShowFrame));
  if (with_symbol_class) {
    root.insert(root.begin() + 1,
                symbol_class_tag({{5, "flash.text::TextField"}, {0, "flash.display::Sprite"}}));
  }
  file.tags = std::move(root);
  return file;
}

struct Harness {
  Runtime runtime;
  Interpreter interpreter{runtime};

  Harness() { runtime.link(); }
};

TEST(Avm2SwfRuntime, ReadsTimelineRowsLabelsAndSprites) {
  const fo4::swf::SwfFile file = build_movie(false);
  const std::shared_ptr<SwfMovieData> data = fo4::avm2::read_swf_movie_data(file);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->root.frame_count, 3u);
  EXPECT_EQ(data->root.frames.size(), 3u);
  ASSERT_EQ(data->root.labels.size(), 1u);
  EXPECT_EQ(data->root.labels[0].name, "start");
  EXPECT_EQ(data->root.labels[0].frame, 1u);
  ASSERT_EQ(data->root.frames[0].size(), 1u);
  EXPECT_EQ(data->root.frames[0][0].depth, 1u);
  EXPECT_EQ(data->root.frames[2][0].kind, fo4::avm2::SwfPlacementRow::Kind::Remove);
  ASSERT_TRUE(data->sprites.contains(10));
  EXPECT_EQ(data->sprites[10].frame_count, 2u);
  EXPECT_NEAR(data->stage_width, 800.0, 1e-9);
  EXPECT_NEAR(data->stage_height, 600.0, 1e-9);
}

TEST(Avm2SwfRuntime, BuildsTheStageAndAppliesTheFirstFrame) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie(false);
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_NE(movie->stage(), nullptr);
  ASSERT_NE(movie->root(), nullptr);
  EXPECT_EQ(movie->stage()->children().size(), 1u);
  EXPECT_EQ(movie->root()->parent(), movie->stage().get());
  EXPECT_EQ(movie->root()->current_frame(), 1u);
  EXPECT_EQ(movie->root()->total_frames(), 3u);
  EXPECT_NEAR(movie->stage()->stage_width(), 800.0, 1e-9);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(movie->root()->children()[0]->name(), "loader");
  EXPECT_NEAR(movie->root()->children()[0]->x(), 0.0, 1e-9);

  const auto* child = dynamic_cast<MovieClip*>(movie->root()->children()[0].get());
  ASSERT_NE(child, nullptr);
  EXPECT_EQ(child->total_frames(), 2u);
  ASSERT_EQ(child->children().size(), 1u);
  EXPECT_NEAR(child->children()[0]->x(), 1.0, 1e-9);
}

TEST(Avm2SwfRuntime, AdvanceUsesADeterministicFrameAccumulator) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie(false);
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  movie->advance(harness.interpreter, 1.0 / 48.0);
  EXPECT_EQ(movie->root()->current_frame(), 1u);
  movie->advance(harness.interpreter, 1.0 / 48.0);
  EXPECT_EQ(movie->root()->current_frame(), 2u);
  EXPECT_EQ(movie->root()->children().size(), 2u);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  EXPECT_EQ(movie->root()->current_frame(), 3u);
  EXPECT_EQ(movie->root()->children().size(), 1u);
}

TEST(Avm2SwfRuntime, ChildSpriteTimelineAdvancesWithTheParent) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie(false);
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  auto* sprite = dynamic_cast<MovieClip*>(movie->root()->children()[0].get());
  ASSERT_NE(sprite, nullptr);
  EXPECT_EQ(sprite->current_frame(), 1u);
  EXPECT_EQ(sprite->children().size(), 1u);

  movie->advance(harness.interpreter, 1.0 / 24.0);
  EXPECT_EQ(sprite->current_frame(), 2u);
  EXPECT_EQ(sprite->children().size(), 2u);
  EXPECT_NEAR(sprite->children()[1]->x(), 3.0, 1e-9);
}

TEST(Avm2SwfRuntime, SymbolClassChoosesThePlacedCharacterClass) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie(true);
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 2u);
  const DisplayObject* placed = movie->root()->children()[1].get();
  ASSERT_NE(placed, nullptr);
  ASSERT_NE(placed->display_class(), nullptr);
  EXPECT_EQ(placed->display_class()->name, "flash.text::TextField");
}

TEST(Avm2SwfRuntime, TwoIdenticalRunsProduceTheSameFrameSequence) {
  const fo4::swf::SwfFile file = build_movie(true);
  auto run_once = [&file]() {
    std::vector<std::string> trace;
    Harness harness;
    const std::shared_ptr<SwfRuntime> movie =
        SwfRuntime::create(harness.runtime, harness.interpreter, file);
    for (int step = 0; step < 4; ++step) {
      trace.push_back("frame " + std::to_string(movie->root()->current_frame()) + " children " +
                      std::to_string(movie->root()->children().size()));
      movie->advance(harness.interpreter, 1.0 / 24.0);
    }
    return trace;
  };
  EXPECT_EQ(run_once(), run_once());
}

fo4::swf::SwfTag shape_tag(std::uint16_t shape_id, std::int32_t x_min, std::int32_t x_max,
                           std::int32_t y_min, std::int32_t y_max) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kDefineShape);
  fo4::swf::SwfShape shape;
  shape.shape_id = shape_id;
  shape.bounds.x_min = x_min;
  shape.bounds.x_max = x_max;
  shape.bounds.y_min = y_min;
  shape.bounds.y_max = y_max;
  tag.shape = shape;
  return tag;
}

fo4::swf::SwfTag font_tag(std::uint16_t font_id) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kDefineFont3);
  fo4::swf::SwfFont font;
  font.font_id = font_id;
  font.glyph_count = 2;
  font.glyph_codes = {std::uint16_t{'A'}, std::uint16_t{' '}};
  font.advances = {0.5, 0.25};
  tag.font = font;
  return tag;
}

fo4::swf::SwfTag edit_text_tag(std::uint16_t character_id, const char* text) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kDefineEditText);
  fo4::swf::SwfEditText edit;
  edit.character_id = character_id;
  edit.bounds.x_min = 0;
  edit.bounds.x_max = 500;
  edit.bounds.y_min = 0;
  edit.bounds.y_max = 2000;
  edit.flags1 = static_cast<std::uint8_t>(0x80u | 0x40u | 0x04u | 0x01u);
  edit.font_id = 3;
  edit.font_height_twips = 400;
  edit.color = 0xFF112233u;
  edit.text = text;
  tag.edit_text = edit;
  return tag;
}

fo4::swf::SwfFile build_placed_bounds_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 1;
  file.header.frame_size.x_min = 0;
  file.header.frame_size.x_max = 16000;
  file.header.frame_size.y_min = 0;
  file.header.frame_size.y_max = 12000;
  file.tags.push_back(shape_tag(20, -400, 800, -200, 400));
  file.tags.push_back(font_tag(3));
  file.tags.push_back(edit_text_tag(7, "AA AA"));
  file.tags.push_back(place_tag(1, 20, 100.0, 50.0));
  file.tags.push_back(place_tag(2, 7, 10.0, 20.0));
  file.tags.push_back(make_tag(fo4::swf::tag::kShowFrame));
  return file;
}

TEST(Avm2SwfRuntime, PlacedCharacterBoundsDriveHitTesting) {
  Harness harness;
  const fo4::swf::SwfFile file = build_placed_bounds_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 2u);
  const DisplayObject& shape = *movie->root()->children()[0];
  EXPECT_DOUBLE_EQ(shape.origin_x(), -20.0);
  EXPECT_DOUBLE_EQ(shape.origin_y(), -10.0);
  EXPECT_DOUBLE_EQ(shape.width(), 60.0);
  EXPECT_DOUBLE_EQ(shape.height(), 30.0);
  const std::shared_ptr<DisplayObject> shape_object = movie->root()->children()[0];
  const std::shared_ptr<DisplayObject> field_object = movie->root()->children()[1];
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 80.0, 40.0).get(), shape_object.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 140.0, 70.0).get(), shape_object.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 15.0, 30.0).get(), field_object.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 79.0, 60.0).get(), nullptr);
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 141.0, 60.0).get(), nullptr);
  EXPECT_EQ(fo4::avm2::mouse_target_at(movie->root(), 120.0, 71.0).get(), nullptr);
}

TEST(Avm2SwfRuntime, PlacedEditTextBecomesAWrappedTextField) {
  Harness harness;
  const fo4::swf::SwfFile file = build_placed_bounds_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 2u);
  const auto* field =
      dynamic_cast<fo4::avm2::TextFieldObject*>(movie->root()->children()[1].get());
  ASSERT_NE(field, nullptr);
  EXPECT_DOUBLE_EQ(field->width(), 25.0);
  EXPECT_DOUBLE_EQ(field->height(), 100.0);
  EXPECT_EQ(field->text(), "AA AA");
  EXPECT_EQ(field->format().font_id, std::uint16_t{3});
  EXPECT_EQ(field->text_color(), 0x112233u);
  EXPECT_TRUE(field->word_wrap());
  ASSERT_EQ(field->runs().size(), 2u);
  EXPECT_EQ(field->line_text(0), "AA ");
  EXPECT_EQ(field->line_text(1), "AA");
  EXPECT_DOUBLE_EQ(field->text_width(), 25.0);
  EXPECT_DOUBLE_EQ(field->text_height(), 48.0);
}

TEST(Avm2SwfRuntime, MoveOnlyPlacementKeepsTheTimelineObject) {
  Harness harness;
  const fo4::swf::SwfFile file = build_frames_movie(
      {{place_tag(1, 20, 100.0, 50.0)}, {move_tag(1, std::nullopt)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(movie->root()->children()[0].get(), placed.get());
  EXPECT_NEAR(placed->x(), 100.0, 1e-9);
  EXPECT_NEAR(placed->y(), 50.0, 1e-9);
}

TEST(Avm2SwfRuntime, MoveWithMatrixUpdatesOnlyTheMatrix) {
  Harness harness;
  const fo4::swf::SwfFile file = build_frames_movie(
      {{place_tag_named(1, 20, "tween")}, {move_tag(1, translated(30.0, 40.0))}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(movie->root()->children()[0].get(), placed.get());
  EXPECT_EQ(placed->name(), "tween");
  EXPECT_EQ(placed->character_id(), 20u);
  EXPECT_NEAR(placed->x(), 30.0, 1e-9);
  EXPECT_NEAR(placed->y(), 40.0, 1e-9);
}

TEST(Avm2SwfRuntime, MoveWithANewCharacterReplacesTheObject) {
  Harness harness;
  fo4::swf::SwfTag replacement = move_tag(1, translated(30.0, 40.0));
  replacement.placement->character_id = std::uint16_t{21};
  const fo4::swf::SwfFile file =
      build_frames_movie({{place_tag(1, 20, 100.0, 50.0)}, {replacement}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> original = movie->root()->children()[0];
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_NE(movie->root()->children()[0].get(), original.get());
  EXPECT_EQ(movie->root()->children()[0]->character_id(), 21u);
  EXPECT_NEAR(movie->root()->children()[0]->x(), 30.0, 1e-9);
}

TEST(Avm2SwfRuntime, RotationFromMatrixSurvivesAMoveOnlyPlacement) {
  Harness harness;
  fo4::swf::SwfTag rotated = place_tag(1, 20, 0.0, 0.0);
  rotated.placement->matrix = rotated_45();
  const fo4::swf::SwfFile file =
      build_frames_movie({{rotated}, {move_tag(1, std::nullopt)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  EXPECT_NEAR(placed->rotation(), 45.0, 1e-3);
  EXPECT_NEAR(placed->scale_x(), 1.0, 1e-3);
  EXPECT_NEAR(placed->scale_y(), 1.0, 1e-3);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(movie->root()->children()[0].get(), placed.get());
  EXPECT_NEAR(placed->rotation(), 45.0, 1e-3);
}

TEST(Avm2SwfRuntime, MoveOnlyPlacementKeepsTheClipDepth) {
  Harness harness;
  fo4::swf::SwfTag masked = place_tag(1, 20, 0.0, 0.0);
  masked.placement->clip_depth = std::uint16_t{4};
  const fo4::swf::SwfFile file = build_frames_movie({{masked}, {move_tag(1, std::nullopt)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  EXPECT_EQ(placed->clip_depth(), 4);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(placed->clip_depth(), 4);
}

TEST(Avm2SwfRuntime, MoveWithoutAnObjectCreatesNothing) {
  Harness harness;
  const fo4::swf::SwfFile file =
      build_frames_movie({{}, {move_tag(1, std::nullopt)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  EXPECT_TRUE(movie->root()->children().empty());
  movie->advance(harness.interpreter, 1.0 / 24.0);
  EXPECT_TRUE(movie->root()->children().empty());
}

fo4::swf::SwfTag styled_place_tag(std::uint16_t depth, std::uint16_t character) {
  fo4::swf::SwfTag tag = place_tag(depth, character, 0.0, 0.0);
  tag.placement->ratio = std::uint16_t{3};
  tag.placement->blend_mode = std::uint8_t{8};
  tag.placement->visible = false;
  tag.placement->bitmap_cache = std::uint8_t{1};
  return tag;
}

fo4::swf::SwfTag flagged_move_tag(std::uint16_t depth) {
  fo4::swf::SwfTag tag = move_tag(depth, std::nullopt);
  tag.placement->ratio = std::uint16_t{9};
  tag.placement->blend_mode = std::uint8_t{2};
  tag.placement->visible = true;
  tag.placement->bitmap_cache = std::uint8_t{0};
  return tag;
}

TEST(Avm2SwfRuntime, PlacementCarriesRatioBlendVisibleAndCache) {
  // Detects: a PlaceObject3 ratio, blend, visible or cache flag that never reaches the placed object; the decoded values must apply.
  Harness harness;
  const fo4::swf::SwfFile file = build_frames_movie({{styled_place_tag(1, 20)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  EXPECT_EQ(placed->ratio(), std::optional<std::uint16_t>{3});
  EXPECT_EQ(placed->blend_mode(), std::uint8_t{8});
  EXPECT_FALSE(placed->visible());
  EXPECT_TRUE(placed->cache_as_bitmap());
}

TEST(Avm2SwfRuntime, MoveWithoutStateFlagsKeepsRatioBlendVisibleAndCache) {
  // Detects: a modify-only row that resets ratio, blend, visibility or cache when its flags are absent; the prior values must persist.
  Harness harness;
  const fo4::swf::SwfFile file =
      build_frames_movie({{styled_place_tag(1, 20)}, {move_tag(1, std::nullopt)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(movie->root()->children()[0].get(), placed.get());
  EXPECT_EQ(placed->ratio(), std::optional<std::uint16_t>{3});
  EXPECT_EQ(placed->blend_mode(), std::uint8_t{8});
  EXPECT_FALSE(placed->visible());
  EXPECT_TRUE(placed->cache_as_bitmap());
}

TEST(Avm2SwfRuntime, MoveWithStateFlagsOverwritesRatioBlendVisibleAndCache) {
  // Detects: a flagged modify row that keeps the prior ratio, blend, visibility or cache; the new values must replace them.
  Harness harness;
  const fo4::swf::SwfFile file =
      build_frames_movie({{styled_place_tag(1, 20)}, {flagged_move_tag(1)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_EQ(movie->root()->children()[0].get(), placed.get());
  EXPECT_EQ(placed->ratio(), std::optional<std::uint16_t>{9});
  EXPECT_EQ(placed->blend_mode(), std::uint8_t{2});
  EXPECT_TRUE(placed->visible());
  EXPECT_FALSE(placed->cache_as_bitmap());
}

TEST(Avm2SwfRuntime, NonMovePlacementOverAnObjectResetsItsState) {
  // Detects: a non-move placement over an existing object that inherits its old ratio, blend or visibility; the defaults must apply.
  Harness harness;
  const fo4::swf::SwfFile file =
      build_frames_movie({{styled_place_tag(1, 20)}, {place_tag(1, 20, 0.0, 0.0)}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  EXPECT_FALSE(placed->ratio().has_value());
  EXPECT_EQ(placed->blend_mode(), std::uint8_t{0});
  EXPECT_TRUE(placed->visible());
  EXPECT_FALSE(placed->cache_as_bitmap());
}

TEST(Avm2SwfRuntime, PlacedFilterListFollowsItsFlagOnModify) {
  // Detects: a modify-only row that drops the placed filters when their flag is absent, or keeps them after a flagged empty list; the list must follow the flag.
  Harness harness;
  fo4::swf::SwfFilter shadow;
  shadow.blur_x = 2.0;
  fo4::swf::SwfTag filtered = place_tag(1, 20, 0.0, 0.0);
  filtered.placement->has_filter_list = true;
  filtered.placement->filters = {shadow};
  fo4::swf::SwfTag cleared = move_tag(1, std::nullopt);
  cleared.placement->has_filter_list = true;
  const fo4::swf::SwfFile file =
      build_frames_movie({{filtered}, {move_tag(1, std::nullopt)}, {cleared}});
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const std::shared_ptr<DisplayObject> placed = movie->root()->children()[0];
  ASSERT_EQ(placed->placed_filters().size(), 1u);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  ASSERT_EQ(placed->placed_filters().size(), 1u);
  EXPECT_DOUBLE_EQ(placed->placed_filters()[0].blur_x, 2.0);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  EXPECT_TRUE(placed->placed_filters().empty());
}

TEST(Avm2SwfRuntime, BlendModeAndVisibleReadAndWriteThroughTheDisplayState) {
  // Detects: an AS3 blendMode or visible write that misses the display state, or a read that loses it; each value must round trip.
  Harness harness;
  const fo4::swf::SwfFile file = build_movie(false);
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  ASSERT_EQ(movie->root()->children().size(), 1u);
  DisplayObject& placed = *movie->root()->children()[0];
  fo4::avm2::Multiname blend_name;
  blend_name.name = "blendMode";
  fo4::avm2::Value out;
  ASSERT_TRUE(fo4::avm2::read_display_property(harness.interpreter, placed, blend_name, out));
  EXPECT_EQ(out.as_string(), "normal");
  ASSERT_TRUE(fo4::avm2::write_display_property(placed, blend_name,
                                                fo4::avm2::Value::string("hardlight")));
  EXPECT_EQ(placed.blend_mode(), std::uint8_t{14});
  ASSERT_TRUE(fo4::avm2::read_display_property(harness.interpreter, placed, blend_name, out));
  EXPECT_EQ(out.as_string(), "hardlight");
  ASSERT_TRUE(fo4::avm2::write_display_property(placed, blend_name,
                                                fo4::avm2::Value::string("bogus")));
  EXPECT_EQ(placed.blend_mode(), std::uint8_t{0});

  fo4::avm2::Multiname visible_name;
  visible_name.name = "visible";
  ASSERT_TRUE(fo4::avm2::write_display_property(placed, visible_name,
                                                fo4::avm2::Value::boolean(false)));
  EXPECT_FALSE(placed.visible());
  ASSERT_TRUE(fo4::avm2::read_display_property(harness.interpreter, placed, visible_name, out));
  ASSERT_TRUE(out.is_boolean());
  EXPECT_FALSE(out.as_boolean());
}

TEST(Avm2SwfRuntime, EnterFrameReachesTheStageAndItsSpriteChildren) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t entered = script.multiname("entered");
  avm2_test::CodeBuilder listener_code;
  listener_code.pushtrue();
  listener_code.getglobalscope();
  listener_code.swap();
  listener_code.setproperty(entered);
  listener_code.returnvoid();
  const std::uint32_t listener = script.method("listener", listener_code.bytes(), 2);
  avm2_test::CodeBuilder init_code;
  init_code.returnvoid();
  const std::uint32_t init = script.method("init", init_code.bytes(), 1);
  const fo4::abc::AbcFile file = script.finish(init);

  Harness harness;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, build_movie(false));
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  auto sprite = fo4::avm2::make_display_object(
      harness.runtime,
      *const_cast<fo4::avm2::Class*>(harness.runtime.find_class("flash.display::Sprite")));
  ASSERT_NE(sprite, nullptr);
  ASSERT_TRUE(movie->stage()->add_child(sprite));
  const auto function = std::make_shared<fo4::avm2::FunctionObject>("listener", &file, listener);
  ASSERT_TRUE(fo4::avm2::add_event_listener(*sprite, "enterFrame",
                                            fo4::avm2::Value::object(function), false, 0));
  movie->advance(harness.interpreter, 1.0 / 24.0);
  fo4::avm2::Multiname multiname;
  multiname.name = "entered";
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value out;
  ASSERT_TRUE(harness.runtime.global_object()->read_value(multiname, out));
  EXPECT_TRUE(out.as_boolean());
}

}  // namespace
