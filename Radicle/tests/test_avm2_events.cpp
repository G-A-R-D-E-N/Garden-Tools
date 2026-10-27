#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
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

}  // namespace
