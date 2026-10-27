#include <fo4/avm2/display.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/text.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using fo4::avm2::Class;
using fo4::avm2::DisplayObject;
using fo4::avm2::FrameSnapshot;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::SnapshotColorTransform;
using fo4::avm2::SnapshotEntryKind;
using fo4::avm2::SnapshotSoundEvent;
using fo4::avm2::SwfRuntime;
using fo4::avm2::TextFieldObject;

fo4::swf::SwfTag make_tag(std::uint16_t code) {
  fo4::swf::SwfTag tag;
  tag.code = code;
  return tag;
}

fo4::swf::SwfTag place_tag(std::uint16_t depth, std::uint16_t character, double x, double y,
                           const char* name = nullptr) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kPlaceObject2);
  fo4::swf::SwfPlacement placement;
  placement.depth = depth;
  placement.character_id = character;
  fo4::swf::SwfMatrix matrix;
  matrix.translate_x = static_cast<std::int32_t>(x * 20.0);
  matrix.translate_y = static_cast<std::int32_t>(y * 20.0);
  placement.matrix = matrix;
  if (name != nullptr) {
    placement.name = std::string{name};
  }
  tag.placement = placement;
  return tag;
}

fo4::swf::SwfTag symbol_class_tag(std::uint16_t character, const char* name) {
  fo4::swf::SwfTag tag = make_tag(fo4::swf::tag::kSymbolClass);
  const std::uint16_t count = 1;
  tag.payload.push_back(static_cast<std::byte>(count & 0xFFu));
  tag.payload.push_back(std::byte{0});
  tag.payload.push_back(static_cast<std::byte>(character & 0xFFu));
  tag.payload.push_back(static_cast<std::byte>((character >> 8u) & 0xFFu));
  for (const char letter : std::string{name}) {
    tag.payload.push_back(static_cast<std::byte>(letter));
  }
  tag.payload.push_back(std::byte{0});
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

fo4::swf::SwfFile build_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 2;
  file.header.frame_size.x_max = 8000;
  file.header.frame_size.y_max = 6000;

  std::vector<fo4::swf::SwfTag> sprite_children;
  sprite_children.push_back(place_tag(1, 100, 1.0, 2.0, "shape"));
  sprite_children.push_back(make_tag(fo4::swf::tag::kShowFrame));
  sprite_children.push_back(make_tag(fo4::swf::tag::kShowFrame));

  std::vector<fo4::swf::SwfTag> root;
  root.push_back(symbol_class_tag(5, "flash.text::TextField"));
  root.push_back(sprite_tag(10, 2, std::move(sprite_children)));
  root.push_back(place_tag(1, 10, 0.0, 0.0, "loader"));
  root.push_back(make_tag(fo4::swf::tag::kShowFrame));
  root.push_back(place_tag(2, 5, 10.0, 20.0, "label"));
  root.push_back(make_tag(fo4::swf::tag::kShowFrame));
  file.tags = std::move(root);
  return file;
}

struct Harness {
  Runtime runtime;
  Interpreter interpreter{runtime};

  Harness() { runtime.link(); }
};

const DisplayObject* child_at(const DisplayObject& parent, std::size_t index) {
  const auto& container = static_cast<const fo4::avm2::DisplayObjectContainer&>(parent);
  return index < container.children().size() ? container.children()[index].get() : nullptr;
}

TEST(Avm2Snapshot, FlattensTheDisplayTreeWithComposedTransforms) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  const FrameSnapshot snapshot = movie->take_snapshot();
  ASSERT_EQ(snapshot.entries.size(), 3u);
  EXPECT_EQ(snapshot.entries[0].path, "");
  EXPECT_EQ(snapshot.entries[1].path, "/0");
  EXPECT_EQ(snapshot.entries[2].path, "/0/0");
  EXPECT_EQ(snapshot.entries[1].name, "loader");
  EXPECT_EQ(snapshot.entries[2].name, "shape");
  EXPECT_EQ(snapshot.entries[1].character_id, 10u);
  EXPECT_EQ(snapshot.entries[2].character_id, 100u);
  EXPECT_EQ(snapshot.entries[2].kind, SnapshotEntryKind::Shape);
  EXPECT_DOUBLE_EQ(snapshot.entries[2].matrix.tx, 1.0);
  EXPECT_DOUBLE_EQ(snapshot.entries[2].matrix.ty, 2.0);
}

TEST(Avm2Snapshot, IncludesPlacedTextFieldRuns) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  const DisplayObject* placed = child_at(*movie->root(), 1);
  ASSERT_NE(placed, nullptr);
  ASSERT_TRUE(placed->is_text_field());
  auto* text_field = static_cast<TextFieldObject*>(const_cast<DisplayObject*>(placed));
  text_field->set_text("AB");
  const FrameSnapshot snapshot = movie->snapshot();
  const fo4::avm2::SnapshotEntry* text_entry = nullptr;
  for (const auto& entry : snapshot.entries) {
    if (entry.kind == SnapshotEntryKind::Text) {
      text_entry = &entry;
    }
  }
  ASSERT_NE(text_entry, nullptr);
  ASSERT_EQ(text_entry->runs.size(), 1u);
  ASSERT_EQ(text_entry->runs[0].glyphs.size(), 2u);
  EXPECT_DOUBLE_EQ(text_entry->runs[0].glyphs[1].x, 6.0);
  EXPECT_DOUBLE_EQ(text_entry->runs[0].glyphs[1].advance, 6.0);
}

TEST(Avm2Snapshot, SnapshotHashIsStableForTheSameTree) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  movie->advance(harness.interpreter, 1.0 / 24.0);
  const FrameSnapshot first = movie->snapshot();
  const FrameSnapshot second = movie->snapshot();
  EXPECT_EQ(first, second);
  EXPECT_EQ(fo4::avm2::snapshot_hash(first), fo4::avm2::snapshot_hash(second));
}

TEST(Avm2Snapshot, IdenticalRunsProduceIdenticalSnapshotHashes) {
  const fo4::swf::SwfFile file = build_movie();
  const auto run_once = [&file]() {
    std::vector<std::uint64_t> hashes;
    Harness harness;
    const std::shared_ptr<SwfRuntime> movie =
        SwfRuntime::create(harness.runtime, harness.interpreter, file);
    if (movie == nullptr) {
      return hashes;
    }
    hashes.push_back(fo4::avm2::snapshot_hash(movie->take_snapshot()));
    for (const double dt : {1.0 / 48.0, 1.0 / 48.0, 1.0 / 24.0, 1.0 / 24.0}) {
      movie->advance(harness.interpreter, dt);
      hashes.push_back(fo4::avm2::snapshot_hash(movie->take_snapshot()));
    }
    return hashes;
  };
  const std::vector<std::uint64_t> first = run_once();
  const std::vector<std::uint64_t> second = run_once();
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(first, second);
}

TEST(Avm2Snapshot, SoundQueueIsDrainedByTheSnapshot) {
  Harness harness;
  const fo4::swf::SwfFile file = build_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(harness.runtime, harness.interpreter, file);
  ASSERT_NE(movie, nullptr);
  SnapshotSoundEvent event;
  event.kind = SnapshotSoundEvent::Kind::Play;
  event.sound_id = 7;
  event.volume = 0.5;
  movie->sound_events().push(event);
  const FrameSnapshot first = movie->take_snapshot();
  ASSERT_EQ(first.sounds.size(), 1u);
  EXPECT_EQ(first.sounds[0], event);
  EXPECT_TRUE(movie->sound_events().empty());
  const FrameSnapshot second = movie->take_snapshot();
  EXPECT_TRUE(second.sounds.empty());
}

TEST(Avm2Snapshot, ColorOffsetsOutsideTheIntRangeClampToIt) {
  Class klass;
  klass.name = "flash.display::Sprite";
  const auto object = std::make_shared<DisplayObject>(&klass);
  const double add[4] = {1e300, -1e300, std::numeric_limits<double>::quiet_NaN(), 12.0};
  object->set_color_add(add);
  const FrameSnapshot snapshot = fo4::avm2::build_frame_snapshot(*object);
  ASSERT_EQ(snapshot.entries.size(), 1u);
  const SnapshotColorTransform& color = snapshot.entries[0].color;
  EXPECT_EQ(color.red_offset, std::numeric_limits<std::int32_t>::max());
  EXPECT_EQ(color.green_offset, std::numeric_limits<std::int32_t>::min());
  EXPECT_EQ(color.blue_offset, 0);
  EXPECT_EQ(color.alpha_offset, 12);
}

}  // namespace
