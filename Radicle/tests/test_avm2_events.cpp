#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/shape_geometry.hpp>
#include <fo4/avm2/swf_runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/swf/swf.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::DisplayObject;
using fo4::avm2::EventObject;
using fo4::avm2::ExecutionResult;
using fo4::avm2::FunctionObject;
using fo4::avm2::Interpreter;
using fo4::avm2::MovieClip;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::Property;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::TimerObject;
using fo4::avm2::Value;

Multiname public_name(const char* name) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

void reset_log(Runtime& runtime) {
  Property& property =
      runtime.global_object()->define_own_property(fo4::avm2::primary_key(public_name("log")));
  property.kind = fo4::avm2::PropertyKind::Value;
  property.value = Value::string(std::string{});
}

std::string read_log(Runtime& runtime) {
  Value value;
  if (!runtime.global_object()->read_value(public_name("log"), value) || !value.is_string()) {
    return {};
  }
  return value.as_string();
}

struct Fixture {
  avm2_test::ScriptBuilder script;
  std::vector<std::uint32_t> methods;
  abc::AbcFile file;
  Runtime runtime;
  std::unique_ptr<Interpreter> interpreter;

  std::uint32_t append_body(const std::string& tag) {
    avm2_test::CodeBuilder body;
    const std::uint32_t log_name = log_multiname();
    body.findpropstrict(log_name);
    body.dup();
    body.getproperty(log_name);
    body.pushstring(script.string_of(tag));
    body.add();
    body.initproperty(log_name);
    body.returnvoid();
    return script.method("append_" + tag, body.bytes(), 2);
  }

  std::uint32_t stop_body(const std::string& tag) {
    avm2_test::CodeBuilder body;
    const std::uint32_t log_name = log_multiname();
    const std::uint32_t stop_name = script.multiname("stopPropagation");
    body.getlocal(1);
    body.callproperty(stop_name, 0);
    body.pop();
    body.findpropstrict(log_name);
    body.dup();
    body.getproperty(log_name);
    body.pushstring(script.string_of(tag));
    body.add();
    body.initproperty(log_name);
    body.returnvoid();
    return script.method("stop_" + tag, body.bytes(), 2);
  }

  std::uint32_t log_multiname() { return script.multiname("log"); }

  void finish() {
    const std::uint32_t init = script.method("init", std::vector<std::uint8_t>{0x47}, 1);
    file = script.finish(init);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime);
    reset_log(runtime);
  }

  std::shared_ptr<FunctionObject> function(std::uint32_t index) {
    return std::make_shared<FunctionObject>("listener", &file, index);
  }
};

fo4::avm2::Class* class_for(Runtime& runtime, const char* name) {
  return const_cast<fo4::avm2::Class*>(runtime.find_class(name));
}

std::shared_ptr<DisplayObject> make_sprite(Runtime& runtime) {
  auto klass = class_for(runtime, "flash.display::Sprite");
  if (klass == nullptr) {
    return nullptr;
  }
  return std::static_pointer_cast<DisplayObject>(
      fo4::avm2::make_display_object(runtime, *klass));
}

TEST(Avm2Events, DispatchesCaptureTargetAndBubbleInOrder) {
  Fixture fixture;
  const std::uint32_t capture_body = fixture.append_body("c");
  const std::uint32_t target_body = fixture.append_body("t");
  const std::uint32_t bubble_body = fixture.append_body("b");
  fixture.finish();

  auto parent = make_sprite(fixture.runtime);
  auto child = make_sprite(fixture.runtime);
  ASSERT_NE(parent, nullptr);
  ASSERT_NE(child, nullptr);
  ASSERT_TRUE(parent->is_container());
  auto* container = static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get());
  ASSERT_TRUE(container->add_child(child));

  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(capture_body)), true, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "ping",
                                            Value::object(fixture.function(target_body)), false,
                                            0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(bubble_body)), false, 0));

  auto event = fo4::avm2::make_event(fixture.runtime, "ping", true, false);
  ASSERT_NE(event, nullptr);
  const ExecutionResult result =
      fo4::avm2::dispatch_event(*fixture.interpreter, child, event, 0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(read_log(fixture.runtime), "ctb");
  EXPECT_EQ(event->event_phase(), 3);
}

TEST(Avm2Events, NonBubblingEventStopsAfterTheTargetPhase) {
  Fixture fixture;
  const std::uint32_t capture_body = fixture.append_body("c");
  const std::uint32_t target_body = fixture.append_body("t");
  const std::uint32_t bubble_body = fixture.append_body("b");
  fixture.finish();

  auto parent = make_sprite(fixture.runtime);
  auto child = make_sprite(fixture.runtime);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get())->add_child(child));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(capture_body)), true, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "ping",
                                            Value::object(fixture.function(target_body)), false,
                                            0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(bubble_body)), false, 0));

  auto event = fo4::avm2::make_event(fixture.runtime, "ping", false, false);
  ASSERT_NE(event, nullptr);
  const ExecutionResult result =
      fo4::avm2::dispatch_event(*fixture.interpreter, child, event, 0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(read_log(fixture.runtime), "ct");
}

TEST(Avm2Events, StopPropagationOnTheTargetSkipsTheBubblePhase) {
  Fixture fixture;
  const std::uint32_t capture_body = fixture.append_body("c");
  const std::uint32_t stop_body = fixture.stop_body("s");
  const std::uint32_t bubble_body = fixture.append_body("b");
  fixture.finish();

  auto parent = make_sprite(fixture.runtime);
  auto child = make_sprite(fixture.runtime);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get())->add_child(child));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(capture_body)), true, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "ping",
                                            Value::object(fixture.function(stop_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(bubble_body)), false, 0));

  auto event = fo4::avm2::make_event(fixture.runtime, "ping", true, false);
  ASSERT_NE(event, nullptr);
  const ExecutionResult result =
      fo4::avm2::dispatch_event(*fixture.interpreter, child, event, 0);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(read_log(fixture.runtime), "cs");
  EXPECT_TRUE(event->propagation_stopped());
}

TEST(Avm2Events, MovieClipFrameLifecycleDispatchesEnterThenExit) {
  Fixture fixture;
  const std::uint32_t enter_body = fixture.append_body("e");
  const std::uint32_t exit_body = fixture.append_body("x");
  fixture.finish();

  auto clip_class = class_for(fixture.runtime, "flash.display::MovieClip");
  ASSERT_NE(clip_class, nullptr);
  auto object = fo4::avm2::make_display_object(fixture.runtime, *clip_class);
  auto clip = std::dynamic_pointer_cast<MovieClip>(object);
  ASSERT_NE(clip, nullptr);
  auto timeline = std::make_shared<fo4::avm2::TimelineDefinition>();
  timeline->frame_count = 3;
  clip->set_timeline(timeline);
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*clip, "enterFrame", Value::object(fixture.function(enter_body)),
                                    false, 0));
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*clip, "exitFrame", Value::object(fixture.function(exit_body)),
                                    false, 0));

  const ExecutionResult result = clip->advance_frame(*fixture.interpreter);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(read_log(fixture.runtime), "ex");
}

TEST(Avm2Events, TimerTicksFromHostTimeAndCompletes) {
  Fixture fixture;
  const std::uint32_t tick_body = fixture.append_body("t");
  fixture.finish();

  auto timer_class = class_for(fixture.runtime, "flash.utils::Timer");
  ASSERT_NE(timer_class, nullptr);
  auto timer = std::make_shared<TimerObject>(timer_class, 100.0, 2);
  timer->set_running(true);
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*timer, "timer", Value::object(fixture.function(tick_body)),
                                    false, 0));
  fixture.interpreter->host_clock().register_timer(timer);

  fixture.interpreter->advance_host_time(250.0);
  EXPECT_EQ(read_log(fixture.runtime), "tt");
  EXPECT_EQ(timer->current_count(), 2);
  EXPECT_FALSE(timer->running());
  EXPECT_DOUBLE_EQ(fixture.interpreter->host_time_ms(), 250.0);
}

TEST(Avm2Events, KeyboardClassExposesKeyConstants) {
  Fixture fixture;
  fixture.finish();
  auto keyboard_class = class_for(fixture.runtime, "flash.ui::Keyboard");
  ASSERT_NE(keyboard_class, nullptr);
  ASSERT_NE(keyboard_class->class_object, nullptr);
  const ExecutionResult result =
      fixture.interpreter->read_property(*keyboard_class->class_object, public_name("A"), 0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 65.0);
}

fo4::swf::SwfFile minimal_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 1;
  file.header.frame_size.x_max = 8000;
  file.header.frame_size.y_max = 6000;
  return file;
}

TEST(Avm2Events, SwfRuntimeDispatchesHostKeyInputToTheRoot) {
  Fixture fixture;
  const std::uint32_t key_body = fixture.append_body("k");
  fixture.finish();
  const fo4::swf::SwfFile movie_file = minimal_movie();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, movie_file);
  ASSERT_NE(movie, nullptr);
  ASSERT_NE(movie->root(), nullptr);
  ASSERT_TRUE(fo4::avm2::add_event_listener(*movie->root(), "keyDown",
                                            Value::object(fixture.function(key_body)), false, 0));
  movie->key_event(*fixture.interpreter, 65, true);
  EXPECT_EQ(read_log(fixture.runtime), "k");
  EXPECT_DOUBLE_EQ(fixture.interpreter->host_time_ms(), 0.0);
  movie->advance(*fixture.interpreter, 1.0 / 24.0);
  EXPECT_GT(fixture.interpreter->host_time_ms(), 0.0);
}

TEST(Avm2Events, MouseTargetIsTheTopmostHitChild) {
  Fixture fixture;
  fixture.finish();
  auto root = make_sprite(fixture.runtime);
  auto lower = make_sprite(fixture.runtime);
  auto upper = make_sprite(fixture.runtime);
  ASSERT_NE(root, nullptr);
  ASSERT_NE(lower, nullptr);
  ASSERT_NE(upper, nullptr);
  auto* container = static_cast<fo4::avm2::DisplayObjectContainer*>(root.get());
  lower->set_width(100.0);
  lower->set_height(100.0);
  avm2_test::set_box_hit_area(*lower);
  upper->set_x(50.0);
  upper->set_y(50.0);
  upper->set_width(100.0);
  upper->set_height(100.0);
  avm2_test::set_box_hit_area(*upper);
  ASSERT_TRUE(container->add_child(lower));
  ASSERT_TRUE(container->add_child(upper));
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 60.0, 60.0).get(), upper.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 10.0, 10.0).get(), lower.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 200.0, 200.0).get(), nullptr);
  upper->set_mouse_enabled(false);
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 60.0, 60.0).get(), lower.get());
}

TEST(Avm2Events, MouseDisabledChildLetsItsParentTakeTheHit) {
  Fixture fixture;
  fixture.finish();
  auto parent = make_sprite(fixture.runtime);
  ASSERT_NE(parent, nullptr);
  fo4::avm2::Class sprite_class;
  sprite_class.name = "flash.display::Sprite";
  auto child = std::make_shared<DisplayObject>(&sprite_class);
  child->set_width(50.0);
  child->set_height(50.0);
  avm2_test::set_box_hit_area(*child);
  child->set_mouse_enabled(false);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get())->add_child(child));
  EXPECT_EQ(fo4::avm2::mouse_target_at(parent, 10.0, 10.0).get(), parent.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(parent, 80.0, 80.0).get(), nullptr);
}

TEST(Avm2Events, MouseChildrenOffMakesTheContainerTheTarget) {
  Fixture fixture;
  fixture.finish();
  auto parent = make_sprite(fixture.runtime);
  ASSERT_NE(parent, nullptr);
  fo4::avm2::Class sprite_class;
  sprite_class.name = "flash.display::Sprite";
  auto child = std::make_shared<DisplayObject>(&sprite_class);
  child->set_width(50.0);
  child->set_height(50.0);
  avm2_test::set_box_hit_area(*child);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get())->add_child(child));
  parent->set_mouse_children(false);
  EXPECT_EQ(fo4::avm2::mouse_target_at(parent, 10.0, 10.0).get(), parent.get());
  parent->set_mouse_children(true);
  EXPECT_EQ(fo4::avm2::mouse_target_at(parent, 10.0, 10.0).get(), child.get());
}

TEST(Avm2Events, MouseChildrenOffHidesItsChildrenFromAnAncestor) {
  Fixture fixture;
  fixture.finish();
  auto root = make_sprite(fixture.runtime);
  auto middle = make_sprite(fixture.runtime);
  auto inner = make_sprite(fixture.runtime);
  ASSERT_NE(middle, nullptr);
  fo4::avm2::Class sprite_class;
  sprite_class.name = "flash.display::Sprite";
  auto leaf = std::make_shared<DisplayObject>(&sprite_class);
  leaf->set_width(50.0);
  leaf->set_height(50.0);
  avm2_test::set_box_hit_area(*leaf);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(inner.get())->add_child(leaf));
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(middle.get())->add_child(inner));
  middle->set_mouse_children(false);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(root.get())->add_child(middle));
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 10.0, 10.0).get(), middle.get());
}

TEST(Avm2Events, HitTestFollowsTheParentTransform) {
  Fixture fixture;
  fixture.finish();
  auto root = make_sprite(fixture.runtime);
  auto child = make_sprite(fixture.runtime);
  auto other = make_sprite(fixture.runtime);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(root.get())->add_child(child));
  child->set_x(10.0);
  child->set_scale_x(2.0);
  child->set_scale_y(2.0);
  child->set_width(10.0);
  child->set_height(10.0);
  other->set_x(25.0);
  other->set_y(15.0);
  other->set_width(10.0);
  other->set_height(10.0);
  EXPECT_TRUE(fo4::avm2::hit_test_point(*child, 25.0, 15.0));
  EXPECT_FALSE(fo4::avm2::hit_test_point(*child, 5.0, 5.0));
  EXPECT_TRUE(fo4::avm2::hit_test_object(*child, *other));
  other->set_x(40.0);
  other->set_y(40.0);
  EXPECT_FALSE(fo4::avm2::hit_test_object(*child, *other));
  root->set_visible(false);
  EXPECT_FALSE(fo4::avm2::hit_test_point(*child, 25.0, 15.0));
}

fo4::avm2::ShapeEdge square_edge(double x0, double y0, double x1, double y1,
                                  std::uint32_t style) {
  fo4::avm2::ShapeEdge edge;
  edge.fill1 = style;
  edge.points = {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}};
  return edge;
}

TEST(Avm2Events, ShapeGeometryUsesTheEvenOddRule) {
  fo4::avm2::ShapeGeometry geometry;
  geometry.edges.push_back(square_edge(0.0, 0.0, 100.0, 100.0, 1));
  geometry.edges.push_back(square_edge(25.0, 25.0, 75.0, 75.0, 1));
  EXPECT_TRUE(fo4::avm2::shape_contains(geometry, 10.0, 10.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(geometry, 50.0, 50.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(geometry, 150.0, 50.0));
}

TEST(Avm2Events, ShapeContainsCountsCrossingsPerFillStyle) {
  fo4::avm2::ShapeGeometry geometry;
  geometry.edges.push_back(square_edge(0.0, 0.0, 100.0, 100.0, 1));
  geometry.edges.push_back(square_edge(60.0, 60.0, 150.0, 150.0, 1));
  geometry.edges.push_back(square_edge(25.0, 25.0, 75.0, 75.0, 2));
  fo4::avm2::ShapeEdge shared;
  shared.fill0 = 1;
  shared.fill1 = 1;
  shared.points = {{{30.0, -10.0}, {30.0, 200.0}}};
  geometry.edges.push_back(shared);
  // An edge with one style on both sides must not count, so the shared border keeps the hit.
  EXPECT_TRUE(fo4::avm2::shape_contains(geometry, 10.0, 10.0));
  // Overlapping fills of one style must cancel, so the overlap misses.
  EXPECT_FALSE(fo4::avm2::shape_contains(geometry, 75.0, 75.0));
  EXPECT_TRUE(fo4::avm2::shape_contains(geometry, 125.0, 125.0));
  // A second style over the cancelled overlap must still hit.
  EXPECT_TRUE(fo4::avm2::shape_contains(geometry, 50.0, 50.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(geometry, 200.0, 200.0));
}

TEST(Avm2Events, ShapeRecordsConvertFromTwipsToPixels) {
  using Kind = fo4::swf::SwfShapeRecordKind;
  auto style = [](std::uint16_t fill0, std::uint16_t fill1) {
    fo4::swf::SwfShapeRecord record;
    record.kind = Kind::style_change;
    record.has_fill0 = true;
    record.has_fill1 = true;
    record.fill0 = fill0;
    record.fill1 = fill1;
    return record;
  };
  auto move = [](std::int32_t x, std::int32_t y) {
    fo4::swf::SwfShapeRecord record;
    record.kind = Kind::move;
    record.x = x;
    record.y = y;
    return record;
  };
  auto edge = [](std::int32_t dx, std::int32_t dy) {
    fo4::swf::SwfShapeRecord record;
    record.kind = Kind::straight_edge;
    record.x = dx;
    record.y = dy;
    return record;
  };
  // Requires pen deltas with the fill on the style record: (0,0), (200,0), (200,200) and back.
  const std::vector<fo4::swf::SwfShapeRecord> records = {
      move(0, 0), style(0, 1), edge(200, 0), edge(0, 200), edge(-200, -200),
  };
  const fo4::avm2::ShapeGeometry geometry =
      fo4::avm2::geometry_from_shape_records(records);
  ASSERT_EQ(geometry.edges.size(), 3u);
  EXPECT_TRUE(fo4::avm2::shape_contains(geometry, 8.0, 2.0));
  EXPECT_FALSE(fo4::avm2::shape_contains(geometry, 2.0, 9.0));
  const std::vector<fo4::swf::SwfShapeRecord> strokes = {
      move(0, 0),
      edge(200, 0),
  };
  EXPECT_TRUE(fo4::avm2::geometry_from_shape_records(strokes).edges.empty());
}

TEST(Avm2Events, ShapeFlagHitTestUsesFillGeometry) {
  Fixture fixture;
  fixture.finish();
  auto root = make_sprite(fixture.runtime);
  auto target = make_sprite(fixture.runtime);
  ASSERT_NE(root, nullptr);
  ASSERT_NE(target, nullptr);
  ASSERT_TRUE(
      static_cast<fo4::avm2::DisplayObjectContainer*>(root.get())->add_child(target));
  target->set_width(100.0);
  target->set_height(100.0);
  fo4::avm2::ShapeGeometry geometry;
  fo4::avm2::ShapeEdge edge;
  edge.fill1 = 1;
  edge.points = {{{0.0, 0.0}, {100.0, 0.0}, {0.0, 100.0}}};
  geometry.edges.push_back(edge);
  target->set_shape_geometry(std::make_shared<const fo4::avm2::ShapeGeometry>(geometry));
  EXPECT_TRUE(fo4::avm2::hit_test_point(*target, 90.0, 90.0));
  EXPECT_FALSE(fo4::avm2::hit_test_point(*target, 90.0, 90.0, true));
  EXPECT_TRUE(fo4::avm2::hit_test_point(*target, 10.0, 10.0, true));
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 10.0, 10.0).get(), target.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 90.0, 90.0).get(), nullptr);
}

TEST(Avm2Events, ShapeFlagHitTestFindsChildFillOutsideTheParentBox) {
  fo4::avm2::Class sprite_class;
  sprite_class.name = "flash.display::Sprite";
  auto root = std::make_shared<fo4::avm2::DisplayObjectContainer>(&sprite_class);
  root->set_width(100.0);
  root->set_height(100.0);
  auto child = std::make_shared<DisplayObject>(&sprite_class);
  child->set_x(200.0);
  fo4::avm2::ShapeGeometry geometry;
  fo4::avm2::ShapeEdge edge;
  edge.fill1 = 1;
  edge.points = {{{0.0, 0.0}, {100.0, 0.0}, {0.0, 100.0}}};
  geometry.edges.push_back(edge);
  child->set_shape_geometry(std::make_shared<const fo4::avm2::ShapeGeometry>(geometry));
  ASSERT_TRUE(root->add_child(child));
  // The point misses the parent box but lands on the child fill, so only the flag must find it.
  EXPECT_FALSE(fo4::avm2::hit_test_point(*root, 250.0, 10.0));
  EXPECT_TRUE(fo4::avm2::hit_test_point(*root, 250.0, 10.0, true));
  EXPECT_FALSE(fo4::avm2::hit_test_point(*root, 250.0, 150.0, true));
}

TEST(Avm2Events, ShapeFlagHitTestMissesAnEmptyContainerBox) {
  fo4::avm2::Class sprite_class;
  sprite_class.name = "flash.display::Sprite";
  auto root = std::make_shared<fo4::avm2::DisplayObjectContainer>(&sprite_class);
  root->set_width(100.0);
  root->set_height(100.0);
  // An empty container must claim nothing with the flag, but its box still hits without it.
  EXPECT_FALSE(fo4::avm2::hit_test_point(*root, 50.0, 50.0, true));
  EXPECT_TRUE(fo4::avm2::hit_test_point(*root, 50.0, 50.0));
}

TEST(Avm2Events, FocusChangeDispatchesFocusOutThenFocusIn) {
  Fixture fixture;
  const std::uint32_t in_body = fixture.append_body("i");
  const std::uint32_t out_body = fixture.append_body("o");
  fixture.finish();
  auto stage = make_sprite(fixture.runtime);
  auto first = make_sprite(fixture.runtime);
  auto second = make_sprite(fixture.runtime);
  ASSERT_TRUE(fo4::avm2::add_event_listener(*first, "focusOut",
                                            Value::object(fixture.function(out_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*second, "focusIn",
                                            Value::object(fixture.function(in_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::set_display_focus(*fixture.interpreter, *stage, first, 0).ok());
  EXPECT_EQ(stage->focus().get(), first.get());
  ASSERT_TRUE(fo4::avm2::set_display_focus(*fixture.interpreter, *stage, second, 0).ok());
  EXPECT_EQ(stage->focus().get(), second.get());
  EXPECT_EQ(read_log(fixture.runtime), "oi");
}

TEST(Avm2Events, WillTriggerSeesDisplayAncestors) {
  Fixture fixture;
  const std::uint32_t body = fixture.append_body("p");
  fixture.finish();
  auto parent = make_sprite(fixture.runtime);
  auto child = make_sprite(fixture.runtime);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get())->add_child(child));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*parent, "ping",
                                            Value::object(fixture.function(body)), false, 0));
  EXPECT_TRUE(fo4::avm2::will_trigger(child, "ping"));
  EXPECT_TRUE(fo4::avm2::will_trigger(parent, "ping"));
  EXPECT_FALSE(fo4::avm2::will_trigger(child, "other"));
}

TEST(Avm2Events, SwfRuntimeRoutesMouseInputToTheHitObject) {
  Fixture fixture;
  const std::uint32_t down_body = fixture.append_body("d");
  const std::uint32_t bubble_body = fixture.append_body("r");
  const std::uint32_t click_body = fixture.append_body("c");
  const std::uint32_t over_body = fixture.append_body("v");
  const std::uint32_t out_body = fixture.append_body("u");
  fixture.finish();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr);
  auto target = make_sprite(fixture.runtime);
  ASSERT_NE(target, nullptr);
  target->set_width(100.0);
  target->set_height(100.0);
  avm2_test::set_box_hit_area(*target);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(movie->root().get())
                  ->add_child(target));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*target, "mouseDown",
                                            Value::object(fixture.function(down_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*movie->root(), "mouseDown",
                                            Value::object(fixture.function(bubble_body)), false,
                                            0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*target, "click",
                                            Value::object(fixture.function(click_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*target, "mouseOver",
                                            Value::object(fixture.function(over_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*target, "mouseOut",
                                            Value::object(fixture.function(out_body)), false, 0));
  movie->mouse_event(*fixture.interpreter, 50.0, 50.0, 0, true);
  EXPECT_EQ(read_log(fixture.runtime), "vdr");
  movie->mouse_event(*fixture.interpreter, 50.0, 50.0, 0, false);
  EXPECT_EQ(read_log(fixture.runtime), "vdrc");
  movie->mouse_move(*fixture.interpreter, 60.0, 60.0);
  EXPECT_EQ(read_log(fixture.runtime), "vdrc");
  movie->mouse_move(*fixture.interpreter, 500.0, 500.0);
  EXPECT_EQ(read_log(fixture.runtime), "vdrcu");
}

TEST(Avm2Events, SwfRuntimeRoutesKeysToTheStageFocus) {
  Fixture fixture;
  const std::uint32_t key_body = fixture.append_body("k");
  const std::uint32_t root_body = fixture.append_body("r");
  fixture.finish();
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(fixture.runtime, *fixture.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr);
  auto target = make_sprite(fixture.runtime);
  ASSERT_NE(target, nullptr);
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(movie->root().get())
                  ->add_child(target));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*target, "keyDown",
                                            Value::object(fixture.function(key_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*movie->root(), "keyDown",
                                            Value::object(fixture.function(root_body)), false, 0));
  ASSERT_TRUE(fo4::avm2::set_display_focus(*fixture.interpreter, *movie->stage(), target, 0).ok());
  movie->key_event(*fixture.interpreter, 65, true);
  EXPECT_EQ(read_log(fixture.runtime), "kr");
}

}  // namespace
