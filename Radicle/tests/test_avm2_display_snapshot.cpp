#include <fo4/avm2/class.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/display_snapshot.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/text.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>
#include <fo4/swf/swf_art.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using fo4::avm2::Class;
using fo4::avm2::DisplayObject;
using fo4::avm2::DisplayObjectContainer;
using fo4::avm2::display_snapshot_hash;
using fo4::avm2::display_snapshot_text;
using fo4::avm2::DrawnArt;
using fo4::avm2::ExecutionResult;
using fo4::avm2::fnv1a64;
using fo4::avm2::FontRegistry;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::TextFieldObject;

struct StageTree {
  Class stage_class;
  Class sprite_class;
  Class text_class;
  FontRegistry registry;
  std::shared_ptr<DisplayObjectContainer> root;
  std::shared_ptr<DisplayObject> sprite;
  std::shared_ptr<TextFieldObject> field;
  std::shared_ptr<DisplayObjectContainer> holder;
  std::shared_ptr<DisplayObject> inner;

  StageTree() {
    stage_class.name = "flash.display::Stage";
    sprite_class.name = "flash.display::Sprite";
    text_class.name = "flash.text::TextField";
    root = std::make_shared<DisplayObjectContainer>(&stage_class);
    sprite = std::make_shared<DisplayObject>(&sprite_class);
    sprite->set_character_id(7);
    sprite->set_x(10.0);
    sprite->set_y(20.0);
    sprite->set_width(30.0);
    sprite->set_height(40.0);
    sprite->set_alpha(0.5);
    field = std::make_shared<TextFieldObject>(&text_class, &registry);
    field->set_character_id(9);
    field->set_x(5.0);
    field->set_visible(false);
    field->set_text("Hi there");
    holder = std::make_shared<DisplayObjectContainer>(&sprite_class);
    holder->set_scale_x(2.0);
    inner = std::make_shared<DisplayObject>(&sprite_class);
    inner->set_character_id(3);
    inner->set_x(3.0);
    holder->add_child(inner);
    root->add_child(sprite);
    root->add_child(field);
    root->add_child(holder);
  }
};

const char* const kExpectedText =
    "class=flash.display::Stage depth=0 id=0 bounds=0 0 0 0 matrix=10000 0 0 10000 0 0 "
    "visible=1 blend=0 ratio=none filters=0 alpha=10000 children=3\n"
    "  class=flash.display::Sprite depth=0 id=7 bounds=0 0 300000 400000 "
    "matrix=10000 0 0 10000 100000 200000 visible=1 blend=0 ratio=none filters=0 alpha=5000 "
    "children=0\n"
    "  class=flash.text::TextField depth=1 id=9 bounds=0 0 0 0 matrix=10000 0 0 10000 50000 0 "
    "visible=0 blend=0 ratio=none filters=0 alpha=10000 children=0 text=8:Hi there\n"
    "  class=flash.display::Sprite depth=2 id=0 bounds=0 0 0 0 matrix=20000 0 0 10000 0 0 "
    "visible=1 blend=0 ratio=none filters=0 alpha=10000 children=1\n"
    "    class=flash.display::Sprite depth=0 id=3 bounds=0 0 0 0 "
    "matrix=20000 0 0 10000 60000 0 visible=1 blend=0 ratio=none filters=0 alpha=10000 "
    "children=0\n";

TEST(Avm2DisplaySnapshot, TextListsEachNodeWithItsComposedMatrixAndTextFieldText) {
  StageTree tree;
  EXPECT_EQ(display_snapshot_text(*tree.root), kExpectedText);
}

TEST(Avm2DisplaySnapshot, PlacedFilterCountIsWhatTheDisplayTextReports) {
  // Detects: a placed filter list the display dump drops, so a pinned replay text would hide a filter change; the sprite line must report filters=2.
  StageTree tree;
  tree.sprite->set_placed_filters(std::vector<fo4::swf::SwfFilter>(2));
  EXPECT_NE(display_snapshot_text(*tree.root).find("filters=2 alpha=5000 children=0"), std::string::npos);
}

TEST(Avm2DisplaySnapshot, HashIsTheFnv1aOfTheText) {
  StageTree tree;
  EXPECT_EQ(display_snapshot_hash(*tree.root), fnv1a64(kExpectedText));
}

TEST(Avm2DisplaySnapshot, AChangedAlphaChangesTheHash) {
  StageTree tree;
  const std::uint64_t before = display_snapshot_hash(*tree.root);
  tree.sprite->set_alpha(1.0);
  EXPECT_NE(display_snapshot_hash(*tree.root), before);
}

TEST(Avm2DisplaySnapshot, AChangedBlendVisibleOrRatioChangesTheHash) {
  // Detects: a blend, visibility or ratio change that the display text omits; the hash must change with each one.
  StageTree tree;
  const std::uint64_t before = display_snapshot_hash(*tree.root);
  tree.sprite->set_blend_mode(3);
  const std::uint64_t blended = display_snapshot_hash(*tree.root);
  EXPECT_NE(blended, before);
  tree.sprite->set_visible(false);
  const std::uint64_t hidden = display_snapshot_hash(*tree.root);
  EXPECT_NE(hidden, blended);
  tree.sprite->set_ratio(std::uint16_t{100});
  EXPECT_NE(display_snapshot_hash(*tree.root), hidden);
  EXPECT_NE(display_snapshot_text(*tree.root).find(" blend=3 ratio=100 filters=0 "),
            std::string::npos);
}

TEST(Avm2DisplaySnapshot, NonFiniteAndOutOfRangeValuesPrintAsWords) {
  Class klass;
  klass.name = "flash.display::Sprite";
  const auto object = std::make_shared<DisplayObject>(&klass);
  object->set_width(1e300);
  object->set_height(std::numeric_limits<double>::quiet_NaN());
  object->set_alpha(-std::numeric_limits<double>::infinity());
  const std::string text = display_snapshot_text(*object);
  EXPECT_NE(text.find(" bounds=0 0 inf nan matrix="), std::string::npos) << text;
  EXPECT_NE(text.find(" alpha=-inf children=0"), std::string::npos) << text;
}

TEST(Fnv1a64, MatchesThePublishedVectors) {
  EXPECT_EQ(fnv1a64(""), 0xcbf29ce484222325ULL);
  EXPECT_EQ(fnv1a64("a"), 0xaf63dc4c8601ec8cULL);
}

namespace abc = fo4::abc;

struct Harness {
  Runtime runtime;
  Interpreter interpreter{runtime};

  ExecutionResult run(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                      std::uint32_t local_count) {
    const std::uint32_t method = script.method("init", code.bytes(), local_count);
    abc::AbcFile file = script.finish(method);
    runtime.add_file(file);
    if (!runtime.link().ok()) {
      ExecutionResult failure;
      failure.message = "runtime did not link";
      return failure;
    }
    return interpreter.run_script(0);
  }
};

void begin_shape(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.getlex(script.multiname("Shape", "flash.display"));
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(script.multiname("graphics"));
  code.setlocal(2);
}

void return_shape(avm2_test::CodeBuilder& code) {
  code.getlocal(1);
  code.returnvalue();
}

void draw_rect_fill(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.getlocal(2);
  code.pushint(script.add_int(0xFF0000));
  code.pushdouble(script.add_double(0.5));
  code.callproperty(script.multiname("beginFill"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(script.multiname("drawRect"), 4);
  code.pop();
  code.getlocal(2);
  code.callproperty(script.multiname("endFill"), 0);
  code.pop();
}

DrawnArt drawn_of(const ExecutionResult& result) {
  EXPECT_TRUE(result.value.is_object()) << result.message;
  if (!result.value.is_object()) {
    return {};
  }
  return fo4::avm2::drawn_art_of(static_cast<const DisplayObject&>(*result.value.as_object()));
}

bool has_point(const fo4::swf::ArtContour& contour, double x, double y) {
  for (const fo4::swf::ArtPoint& point : contour.points) {
    if (point.x == x && point.y == y) {
      return true;
    }
  }
  return false;
}

// Detects: a fill colour or alpha that the Graphics recorder drops or misplaces, so a drawn shape renders in the wrong colour; the drawn snapshot entry must keep its key, revision and shape.
TEST(Avm2GraphicsStyle, BeginFillCarriesColourAndAlphaIntoTheDrawnArt) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  begin_shape(script, code);
  draw_rect_fill(script, code);
  return_shape(code);
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  ASSERT_NE(drawn.shape, nullptr);
  EXPECT_NE(drawn.key, 0u);
  EXPECT_EQ(drawn.revision, 3u);
  ASSERT_EQ(drawn.shape->fills.size(), 1u);
  EXPECT_EQ(drawn.shape->fills[0].kind, std::uint8_t{0x00});
  EXPECT_EQ(drawn.shape->fills[0].color, 0x80FF0000u);
  ASSERT_EQ(drawn.shape->fills[0].contours.size(), 1u);
  const fo4::swf::ArtContour& contour = drawn.shape->fills[0].contours[0];
  EXPECT_TRUE(contour.closed);
  ASSERT_EQ(contour.points.size(), 4u);
  EXPECT_TRUE(has_point(contour, 0.0, 0.0));
  EXPECT_TRUE(has_point(contour, 10.0, 0.0));
  EXPECT_TRUE(has_point(contour, 10.0, 10.0));
  EXPECT_TRUE(has_point(contour, 0.0, 10.0));
  EXPECT_TRUE(drawn.shape->lines.empty());

  const DisplayObject& shape = static_cast<const DisplayObject&>(*result.value.as_object());
  const fo4::avm2::FrameSnapshot snapshot = fo4::avm2::build_frame_snapshot(shape);
  ASSERT_EQ(snapshot.entries.size(), 1u);
  EXPECT_EQ(snapshot.entries[0].drawn_key, drawn.key);
  EXPECT_EQ(snapshot.entries[0].drawn_revision, 3u);
  EXPECT_EQ(snapshot.entries[0].drawn_art, drawn.shape);
}

// Detects: a gradient fill whose stops, type, focal point, spread or interpolation are lost, so GE draws a flat or wrongly shaded fill; the recorder must keep each of them.
TEST(Avm2GraphicsStyle, GradientFillCarriesItsStopsFocalPointSpreadAndInterpolation) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  begin_shape(script, code);
  code.getlocal(2);
  code.pushstring(script.string_of("radial"));
  code.pushint(script.add_int(0xFF0000));
  code.pushint(script.add_int(0x0000FF));
  code.newarray(2);
  code.pushdouble(script.add_double(1.0));
  code.pushdouble(script.add_double(0.5));
  code.newarray(2);
  code.pushint(script.add_int(0));
  code.pushint(script.add_int(255));
  code.newarray(2);
  code.pushnull();
  code.pushstring(script.string_of("reflect"));
  code.pushstring(script.string_of("linearRGB"));
  code.pushdouble(script.add_double(0.5));
  code.callproperty(script.multiname("beginGradientFill"), 8);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(script.multiname("drawRect"), 4);
  code.pop();
  code.getlocal(2);
  code.callproperty(script.multiname("endFill"), 0);
  code.pop();
  return_shape(code);
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  ASSERT_NE(drawn.shape, nullptr);
  ASSERT_EQ(drawn.shape->fills.size(), 1u);
  const fo4::swf::ArtFill& fill = drawn.shape->fills[0];
  EXPECT_EQ(fill.kind, std::uint8_t{0x13});
  EXPECT_DOUBLE_EQ(fill.focal_point, 0.5);
  EXPECT_EQ(fill.spread, std::uint8_t{1});
  EXPECT_EQ(fill.interpolation, std::uint8_t{1});
  ASSERT_EQ(fill.stops.size(), 2u);
  EXPECT_EQ(fill.stops[0].ratio, std::uint8_t{0});
  EXPECT_EQ(fill.stops[0].color, 0xFFFF0000u);
  EXPECT_EQ(fill.stops[1].ratio, std::uint8_t{255});
  EXPECT_EQ(fill.stops[1].color, 0x800000FFu);
  EXPECT_DOUBLE_EQ(fill.matrix[0], 1.0);
  EXPECT_DOUBLE_EQ(fill.matrix[3], 1.0);
  EXPECT_DOUBLE_EQ(fill.matrix[4], 0.0);
}

// Detects: a line style whose width, colour, cap, join or miter limit is lost, so GE strokes with the wrong weight or ends; the recorder must keep each of them.
TEST(Avm2GraphicsStyle, LineStyleCarriesWidthColourCapsJointsAndMiterLimit) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  begin_shape(script, code);
  code.getlocal(2);
  code.pushbyte(2);
  code.pushint(script.add_int(0x00FF00));
  code.pushdouble(script.add_double(1.0));
  code.pushfalse();
  code.pushstring(script.string_of("normal"));
  code.pushstring(script.string_of("square"));
  code.pushstring(script.string_of("miter"));
  code.pushbyte(3);
  code.callproperty(script.multiname("lineStyle"), 8);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(script.multiname("moveTo"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(0);
  code.callproperty(script.multiname("lineTo"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(script.multiname("lineTo"), 2);
  code.pop();
  return_shape(code);
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  ASSERT_NE(drawn.shape, nullptr);
  EXPECT_TRUE(drawn.shape->fills.empty());
  ASSERT_EQ(drawn.shape->lines.size(), 1u);
  const fo4::swf::ArtLine& line = drawn.shape->lines[0];
  EXPECT_DOUBLE_EQ(line.width, 2.0);
  EXPECT_EQ(line.color, 0xFF00FF00u);
  EXPECT_EQ(line.caps, std::uint8_t{2});
  EXPECT_EQ(line.joins, std::uint8_t{2});
  EXPECT_DOUBLE_EQ(line.miter_limit, 3.0);
  ASSERT_EQ(line.contours.size(), 1u);
  EXPECT_FALSE(line.contours[0].closed);
  ASSERT_EQ(line.contours[0].points.size(), 3u);
  EXPECT_TRUE(has_point(line.contours[0], 10.0, 10.0));
}

// Detects: a bare lineStyle() call that starts stroking, which would draw every later edge as a visible outline; a bare call must draw no stroke.
TEST(Avm2GraphicsStyle, BareLineStyleCallDrawsNoStroke) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  begin_shape(script, code);
  code.getlocal(2);
  code.callproperty(script.multiname("lineStyle"), 0);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(script.multiname("moveTo"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(0);
  code.callproperty(script.multiname("lineTo"), 2);
  code.pop();
  return_shape(code);
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  ASSERT_NE(drawn.shape, nullptr);
  EXPECT_TRUE(drawn.shape->lines.empty());
}

// Detects: a closing edge of an open fill that is stroked under the active lineStyle, so a filled triangle draws an extra outline; the stroke must stay the open polyline and the fill must stay closed.
TEST(Avm2GraphicsStyle, ClosingEdgeOfAnOpenFillIsNotStroked) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  begin_shape(script, code);
  code.getlocal(2);
  code.pushbyte(2);
  code.pushint(script.add_int(0x00FF00));
  code.pushdouble(script.add_double(1.0));
  code.callproperty(script.multiname("lineStyle"), 3);
  code.pop();
  code.getlocal(2);
  code.pushint(script.add_int(0xFF0000));
  code.pushdouble(script.add_double(1.0));
  code.callproperty(script.multiname("beginFill"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(script.multiname("moveTo"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(0);
  code.callproperty(script.multiname("lineTo"), 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(script.multiname("lineTo"), 2);
  code.pop();
  code.getlocal(2);
  code.callproperty(script.multiname("endFill"), 0);
  code.pop();
  return_shape(code);
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  ASSERT_NE(drawn.shape, nullptr);
  ASSERT_EQ(drawn.shape->fills.size(), 1u);
  ASSERT_EQ(drawn.shape->fills[0].contours.size(), 1u);
  EXPECT_TRUE(drawn.shape->fills[0].contours[0].closed);
  EXPECT_EQ(drawn.shape->fills[0].contours[0].points.size(), 3u);
  ASSERT_EQ(drawn.shape->lines.size(), 1u);
  ASSERT_EQ(drawn.shape->lines[0].contours.size(), 1u);
  EXPECT_FALSE(drawn.shape->lines[0].contours[0].closed);
  EXPECT_EQ(drawn.shape->lines[0].contours[0].points.size(), 3u);
}

// Detects: a clear or redraw that keeps the old revision, so GE never re-uploads, or a new key per draw, so GE re-uploads everything; clear must advance the revision and keep the key.
TEST(Avm2GraphicsStyle, ClearAndRedrawAdvanceTheRevisionAndKeepTheKey) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  begin_shape(script, code);
  draw_rect_fill(script, code);
  code.getlocal(2);
  code.callproperty(script.multiname("clear"), 0);
  code.pop();
  draw_rect_fill(script, code);
  return_shape(code);
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  ASSERT_NE(drawn.shape, nullptr);
  EXPECT_EQ(drawn.revision, 7u);
  EXPECT_NE(drawn.key, 0u);
  ASSERT_EQ(drawn.shape->fills.size(), 1u);
  EXPECT_EQ(drawn.shape->fills[0].contours.size(), 1u);
}

// Detects: a plain display object that reports a drawn shape or a nonzero key, so GE would upload art for objects that never drew; such objects must report none.
TEST(Avm2GraphicsStyle, ObjectsThatNeverDrewHaveNoDrawnArt) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getlex(script.multiname("Shape", "flash.display"));
  code.construct(0);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  const DrawnArt drawn = drawn_of(result);
  EXPECT_EQ(drawn.key, 0u);
  EXPECT_EQ(drawn.revision, 0u);
  EXPECT_EQ(drawn.shape, nullptr);
}

// Detects: a Graphics key taken from a process-wide counter, so the same drawing in a fresh runtime gets a later key and a replay stops being reproducible; the key must count from one in each runtime.
TEST(Avm2GraphicsStyle, GraphicsKeysStartAtOneInEachRuntime) {
  for (int round = 0; round < 2; ++round) {
    Harness harness;
    avm2_test::ScriptBuilder script;
    avm2_test::CodeBuilder code;
    begin_shape(script, code);
    return_shape(code);
    const ExecutionResult result = harness.run(script, code, 3);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(drawn_of(result).key, 1u);
  }
}

}  // namespace
