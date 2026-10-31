#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/snapshot.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;

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

fo4::avm2::Value property(const fo4::avm2::Value& object, const char* name) {
  if (!object.is_object()) return fo4::avm2::Value::undefined();
  fo4::avm2::Multiname multiname;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Value out;
  if (!object.as_object()->read_value(multiname, out)) return fo4::avm2::Value::undefined();
  return out;
}

void call_on(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code, std::uint32_t local,
             const char* name, const std::vector<std::int32_t>& args, bool keep = false) {
  code.getlocal(local);
  for (const std::int32_t arg : args) {
    code.pushbyte(arg);
  }
  code.callproperty(script.multiname(name), static_cast<std::uint32_t>(args.size()));
  if (!keep) {
    code.pop();
  }
}

TEST(Avm2DisplayApi, GraphicsRecordsDrawCommandsWithoutFailing) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t line_style = script.multiname("lineStyle");
  const std::uint32_t line_to = script.multiname("lineTo");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(1);
  code.pushbyte(0);
  code.pushbyte(1);
  code.callproperty(line_style, 3);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.callproperty(end_fill, 0);
  code.pop();
  code.getlocal(1);
  code.pushnull();
  code.callproperty(script.multiname("getBounds"), 1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_TRUE(property(result.value, "width").is_number_like());
}

TEST(Avm2DisplayApi, GraphicsDrawRectProducesFillGeometry) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t draw_rect = script.multiname("drawRect");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(draw_rect, 4);
  code.pop();
  code.getlocal(2);
  code.callproperty(end_fill, 0);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& shape_object =
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object());
  const fo4::avm2::ShapeGeometry drawn = *fo4::avm2::drawn_geometry_of(shape_object);
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 5.0, 5.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(drawn, 50.0, 50.0));
  EXPECT_TRUE(fo4::avm2::hit_test_point(shape_object, 5.0, 5.0, true));
  EXPECT_FALSE(fo4::avm2::hit_test_point(shape_object, 50.0, 50.0, true));
  const fo4::avm2::FrameSnapshot snapshot = fo4::avm2::build_frame_snapshot(shape_object);
  ASSERT_EQ(snapshot.entries.size(), 1u);
  EXPECT_TRUE(fo4::avm2::shape_contains(*snapshot.entries[0].geometry, 5.0, 5.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(*snapshot.entries[0].geometry, 50.0, 50.0));
}

fo4::avm2::ShapeGeometry draw_open_path(double x0) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t line_to = script.multiname("lineTo");
  const std::uint32_t start = script.add_double(x0);
  const std::uint32_t zero = script.add_double(0.0);
  const std::uint32_t ten = script.add_double(10.0);
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushdouble(start);
  code.pushdouble(zero);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushdouble(ten);
  code.pushdouble(zero);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushdouble(ten);
  code.pushdouble(ten);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  EXPECT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_object());
  return *fo4::avm2::drawn_geometry_of(
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object()));
}

TEST(Avm2DisplayApi, ExtremeDrawingCoordinatesClampDeterministically) {
  // NaN must draw at zero on every platform, so it matches a literal zero start.
  EXPECT_EQ(draw_open_path(std::numeric_limits<double>::quiet_NaN()), draw_open_path(0.0));
  // Huge and infinite starts must clamp to the same records, keeping every point finite.
  const fo4::avm2::ShapeGeometry infinite =
      draw_open_path(std::numeric_limits<double>::infinity());
  EXPECT_EQ(infinite, draw_open_path(1e30));
  for (const fo4::avm2::ShapeEdge& edge : infinite.edges) {
    for (const fo4::avm2::ShapePoint& point : edge.points) {
      EXPECT_TRUE(std::isfinite(point.x));
      EXPECT_TRUE(std::isfinite(point.y));
    }
  }
}

TEST(Avm2DisplayApi, SubPixelDrawingCoordinatesSurviveInTwips) {
  const fo4::avm2::ShapeGeometry geometry = draw_open_path(0.05);
  ASSERT_FALSE(geometry.edges.empty());
  ASSERT_FALSE(geometry.edges[0].points.empty());
  // One twip must read above zero pixels, so a pixel-rounding reader fails this upper bound.
  EXPECT_GT(geometry.edges[0].points[0].x, 0.0);
  EXPECT_LT(geometry.edges[0].points[0].x, 0.1);
}

TEST(Avm2DisplayApi, StrokeEdgesDoNotShiftLaterFillEdges) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t line_style = script.multiname("lineStyle");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t line_to = script.multiname("lineTo");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(1);
  code.callproperty(line_style, 1);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(0);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(10);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.callproperty(end_fill, 0);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& shape_object =
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object());
  const fo4::avm2::ShapeGeometry drawn = *fo4::avm2::drawn_geometry_of(shape_object);
  // A stroke-only lineTo moves the pen, so the fill edges after it must still start where it ended.
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 5.0, 5.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(drawn, 50.0, 50.0));
}

TEST(Avm2DisplayApi, OpenFilledPathClosesWithALineAtEndFill) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t line_to = script.multiname("lineTo");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(0);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.callproperty(end_fill, 0);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& shape_object =
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object());
  const fo4::avm2::ShapeGeometry drawn = *fo4::avm2::drawn_geometry_of(shape_object);
  // An open fill must close at endFill, so the missing third side must not leak hits.
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 8.0, 2.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(drawn, 2.0, 9.0));
}

TEST(Avm2DisplayApi, OverlappingSeparateFillsUnionInsteadOfCancelling) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t draw_rect = script.multiname("drawRect");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  for (int rect = 0; rect < 2; ++rect) {
    code.getlocal(2);
    code.pushbyte(255);
    code.pushbyte(1);
    code.callproperty(begin_fill, 2);
    code.pop();
    code.getlocal(2);
    code.pushbyte(rect == 0 ? 0 : 50);
    code.pushbyte(rect == 0 ? 0 : 50);
    code.pushbyte(100);
    code.pushbyte(100);
    code.callproperty(draw_rect, 4);
    code.pop();
    code.getlocal(2);
    code.callproperty(end_fill, 0);
    code.pop();
  }
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& shape_object =
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object());
  const fo4::avm2::ShapeGeometry drawn = *fo4::avm2::drawn_geometry_of(shape_object);
  // Two separate fills overlap at (75,75); the player unions them, so the overlap must hit.
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 25.0, 25.0));
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 75.0, 75.0));
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 125.0, 125.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(drawn, 175.0, 175.0));
}

TEST(Avm2DisplayApi, OpenFillIsClosedAtTheEndOfTheCommandList) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t line_to = script.multiname("lineTo");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(100);
  code.pushbyte(0);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(100);
  code.pushbyte(100);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& shape_object =
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object());
  const fo4::avm2::ShapeGeometry drawn = *fo4::avm2::drawn_geometry_of(shape_object);
  // A fill that never reaches endFill must still close, so the missing side must not leak hits.
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 80.0, 20.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(drawn, 20.0, 80.0));
}

TEST(Avm2DisplayApi, FillClosesToThePenWhereItBegan) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t move_to = script.multiname("moveTo");
  const std::uint32_t line_to = script.multiname("lineTo");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.callproperty(move_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(50);
  code.pushbyte(50);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(100);
  code.pushbyte(50);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(100);
  code.pushbyte(100);
  code.callproperty(line_to, 2);
  code.pop();
  code.getlocal(2);
  code.callproperty(end_fill, 0);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const auto& shape_object =
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object());
  const fo4::avm2::ShapeGeometry drawn = *fo4::avm2::drawn_geometry_of(shape_object);
  // The fill starts where beginFill ran, (50,50), so the closing side misses (5,10).
  EXPECT_TRUE(fo4::avm2::shape_contains(drawn, 80.0, 60.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(drawn, 5.0, 10.0));
}

TEST(Avm2DisplayApi, SnapshotHashIsPinnedForAKnownDrawing) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t shape = script.multiname("Shape", "flash.display");
  const std::uint32_t graphics = script.multiname("graphics");
  const std::uint32_t begin_fill = script.multiname("beginFill");
  const std::uint32_t draw_rect = script.multiname("drawRect");
  const std::uint32_t end_fill = script.multiname("endFill");
  code.getlex(shape);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(graphics);
  code.setlocal(2);
  code.getlocal(2);
  code.pushbyte(255);
  code.pushbyte(1);
  code.callproperty(begin_fill, 2);
  code.pop();
  code.getlocal(2);
  code.pushbyte(0);
  code.pushbyte(0);
  code.pushbyte(10);
  code.pushbyte(10);
  code.callproperty(draw_rect, 4);
  code.pop();
  code.getlocal(2);
  code.callproperty(end_fill, 0);
  code.pop();
  code.getlocal(1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  const fo4::avm2::FrameSnapshot snapshot = fo4::avm2::build_frame_snapshot(
      static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object()));
  // Pins the snapshot hash of a 10 px drawn square, so any hash input change fails.
  EXPECT_EQ(fo4::avm2::snapshot_hash(snapshot), 0xc290c7e83afe01ddULL);
}

TEST(Avm2DisplayApi, DrawnGeometryIsRebuiltAfterDrawingAndAfterClear) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.getlex(script.multiname("Shape", "flash.display"));
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(script.multiname("graphics"));
  code.setlocal(2);
  call_on(script, code, 2, "beginFill", {255, 1});
  call_on(script, code, 2, "drawRect", {0, 0, 40, 40});
  call_on(script, code, 2, "endFill", {});
  call_on(script, code, 1, "hitTestPoint", {10, 10, 1});
  call_on(script, code, 2, "beginFill", {255, 1});
  call_on(script, code, 2, "drawRect", {60, 60, 40, 40});
  call_on(script, code, 2, "endFill", {});
  call_on(script, code, 1, "hitTestPoint", {70, 70, 1}, true);
  call_on(script, code, 2, "clear", {});
  call_on(script, code, 1, "hitTestPoint", {10, 10, 1}, true);
  code.subtract();
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_DOUBLE_EQ(result.value.as_number(), 1.0);
}

TEST(Avm2DisplayApi, SnapshotHashCoversDrawnGeometry) {
  auto run_shape = [](bool draw) {
    Harness harness;
    avm2_test::ScriptBuilder script;
    avm2_test::CodeBuilder code;
    const std::uint32_t shape = script.multiname("Shape", "flash.display");
    code.getlex(shape);
    code.construct(0);
    code.setlocal(1);
    if (draw) {
      const std::uint32_t graphics = script.multiname("graphics");
      const std::uint32_t begin_fill = script.multiname("beginFill");
      const std::uint32_t draw_rect = script.multiname("drawRect");
      code.getlocal(1);
      code.getproperty(graphics);
      code.setlocal(2);
      code.getlocal(2);
      code.pushbyte(255);
      code.pushbyte(1);
      code.callproperty(begin_fill, 2);
      code.pop();
      code.getlocal(2);
      code.pushbyte(0);
      code.pushbyte(0);
      code.pushbyte(10);
      code.pushbyte(10);
      code.callproperty(draw_rect, 4);
      code.pop();
    }
    code.getlocal(1);
    code.returnvalue();
    const ExecutionResult result = harness.run(script, code, 3);
    EXPECT_TRUE(result.ok()) << result.message;
    if (!result.value.is_object()) {
      return fo4::avm2::FrameSnapshot{};
    }
    return fo4::avm2::build_frame_snapshot(
        static_cast<const fo4::avm2::DisplayObject&>(*result.value.as_object()));
  };
  const fo4::avm2::FrameSnapshot drawn = run_shape(true);
  const fo4::avm2::FrameSnapshot plain = run_shape(false);
  ASSERT_EQ(drawn.entries.size(), 1u);
  ASSERT_EQ(plain.entries.size(), 1u);
  EXPECT_NE(fo4::avm2::snapshot_hash(drawn), fo4::avm2::snapshot_hash(plain));
}

TEST(Avm2DisplayApi, HitTestPointHonorsShapeFlagThroughScript) {
  auto run_hit = [](int x, int y) {
    Harness harness;
    avm2_test::ScriptBuilder script;
    avm2_test::CodeBuilder code;
    const std::uint32_t shape = script.multiname("Shape", "flash.display");
    const std::uint32_t graphics = script.multiname("graphics");
    const std::uint32_t begin_fill = script.multiname("beginFill");
    const std::uint32_t draw_rect = script.multiname("drawRect");
    const std::uint32_t hit_test_point = script.multiname("hitTestPoint");
    code.getlex(shape);
    code.construct(0);
    code.setlocal(1);
    code.getlocal(1);
    code.getproperty(graphics);
    code.setlocal(2);
    code.getlocal(2);
    code.pushbyte(255);
    code.pushbyte(1);
    code.callproperty(begin_fill, 2);
    code.pop();
    code.getlocal(2);
    code.pushbyte(0);
    code.pushbyte(0);
    code.pushbyte(10);
    code.pushbyte(10);
    code.callproperty(draw_rect, 4);
    code.pop();
    code.getlocal(1);
    code.pushbyte(x);
    code.pushbyte(y);
    code.pushtrue();
    code.callproperty(hit_test_point, 3);
    code.returnvalue();
    const ExecutionResult result = harness.run(script, code, 3);
    EXPECT_TRUE(result.ok()) << result.message;
    return result.value;
  };
  EXPECT_TRUE(run_hit(5, 5).as_boolean());
  EXPECT_FALSE(run_hit(50, 50).as_boolean());
}

TEST(Avm2DisplayApi, ReportsATransformObjectWithColourTransform) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t transform = script.multiname("transform");
  const std::uint32_t color_transform = script.multiname("colorTransform");
  const std::uint32_t red_multiplier = script.multiname("redMultiplier");
  code.getlex(movie_clip);
  code.construct(0);
  code.getproperty(transform);
  code.getproperty(color_transform);
  code.getproperty(red_multiplier);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 1.0);
}

TEST(Avm2DisplayApi, EventFormatToStringUsesTheEventFields) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t event_class = script.multiname("Event", "flash.events");
  const std::uint32_t format = script.multiname("formatToString");
  code.getlex(event_class);
  code.pushstring(script.string_of("timer"));
  code.pushtrue();
  code.pushfalse();
  code.construct(3);
  code.pushstring(script.string_of("Event"));
  code.callproperty(format, 1);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_NE(result.value.as_string().find("type=\"timer\""), std::string::npos);
  EXPECT_NE(result.value.as_string().find("bubbles=true"), std::string::npos);
}

TEST(Avm2DisplayApi, LoaderExposesALoaderInfoWithAnApplicationDomain) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t loader = script.multiname("Loader", "flash.display");
  const std::uint32_t info = script.multiname("contentLoaderInfo");
  const std::uint32_t domain = script.multiname("applicationDomain");
  code.getlex(loader);
  code.construct(0);
  code.getproperty(info);
  code.getproperty(domain);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->instance_class()->name, "flash.system::ApplicationDomain");
}

TEST(Avm2DisplayApi, ContentLoaderInfoIsTheSameObjectOnEveryRead) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t loader = script.multiname("Loader", "flash.display");
  const std::uint32_t info = script.multiname("contentLoaderInfo");
  code.getlex(loader);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(info);
  code.getlocal(1);
  code.getproperty(info);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

}  // namespace
