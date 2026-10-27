#include <fo4/avm2/display.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
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

}  // namespace
