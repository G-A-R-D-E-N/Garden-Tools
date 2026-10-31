#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/hit_test.hpp>
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
  upper->set_x(50.0);
  upper->set_y(50.0);
  upper->set_width(100.0);
  upper->set_height(100.0);
  ASSERT_TRUE(container->add_child(lower));
  ASSERT_TRUE(container->add_child(upper));
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 60.0, 60.0).get(), upper.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 10.0, 10.0).get(), lower.get());
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 200.0, 200.0).get(), nullptr);
  upper->set_mouse_enabled(false);
  EXPECT_EQ(fo4::avm2::mouse_target_at(root, 60.0, 60.0).get(), lower.get());
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

TEST(Avm2Events, EventConstantsCarryTheAs3EventTypeStrings) {
  Fixture fixture;
  fixture.finish();
  auto event_class = class_for(fixture.runtime, "flash.events::Event");
  ASSERT_NE(event_class, nullptr);
  const ExecutionResult result =
      fixture.interpreter->read_property(*event_class->class_object, public_name("ENTER_FRAME"), 0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "enterFrame");
}

TEST(Avm2Events, AddFrameScriptRunsItsFunctionEachTimeThePlayheadLandsOnItsFrame) {
  Fixture fixture;
  const std::uint32_t body = fixture.append_body("f");
  fixture.finish();
  auto clip_class = class_for(fixture.runtime, "flash.display::MovieClip");
  ASSERT_NE(clip_class, nullptr);
  auto clip = std::dynamic_pointer_cast<MovieClip>(
      fo4::avm2::make_display_object(fixture.runtime, *clip_class));
  ASSERT_NE(clip, nullptr);
  auto timeline = std::make_shared<fo4::avm2::TimelineDefinition>();
  timeline->frame_count = 3;
  clip->set_timeline(timeline);
  const ExecutionResult add =
      fixture.interpreter->read_property(*clip, public_name("addFrameScript"), 0);
  ASSERT_TRUE(add.ok()) << add.message;
  ASSERT_TRUE(add.value.is_object());
  const ExecutionResult added = fixture.interpreter->call_function(
      static_cast<const FunctionObject&>(*add.value.as_object()), Value::undefined(),
      {Value::int32(1), Value::object(fixture.function(body))}, 0);
  ASSERT_TRUE(added.ok()) << added.message;
  for (int step = 0; step < 4; ++step) {
    ASSERT_TRUE(clip->advance_frame(*fixture.interpreter).ok());
  }
  EXPECT_EQ(read_log(fixture.runtime), "ff");
}

TEST(Avm2Events, AddedFrameScriptWinsOverAFrameNTrait) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t trait_flag = script.multiname("traitRan");
  const std::uint32_t added_flag = script.multiname("addedRan");
  const std::uint32_t framed = script.multiname("Framed");
  const std::uint32_t movie_clip = script.multiname("MovieClip", "flash.display");
  const std::uint32_t frame2 = script.multiname("frame2");
  const auto flag_body = [&script](std::uint32_t flag) {
    avm2_test::CodeBuilder body;
    body.pushtrue();
    body.getglobalscope();
    body.swap();
    body.setproperty(flag);
    body.returnvoid();
    return body.bytes();
  };
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  const std::uint32_t trait_method = script.method("frame2", flag_body(trait_flag), 1);
  const std::uint32_t added_method = script.method("added", flag_body(added_flag), 1);
  avm2_test::CodeBuilder init_code;
  init_code.returnvoid();
  const std::uint32_t init = script.method("init", init_code.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcTrait frame_trait;
  frame_trait.name = frame2;
  frame_trait.kind = abc::AbcTraitKind::Method;
  frame_trait.method_index = trait_method;
  abc::AbcInstanceInfo instance;
  instance.name = framed;
  instance.super_index = movie_clip;
  instance.iinit = constructor;
  instance.traits.push_back(frame_trait);
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);

  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  auto clip = std::dynamic_pointer_cast<MovieClip>(fo4::avm2::make_display_object(
      runtime, *const_cast<fo4::avm2::Class*>(runtime.find_class("Framed"))));
  ASSERT_NE(clip, nullptr);
  auto timeline = std::make_shared<fo4::avm2::TimelineDefinition>();
  timeline->frame_count = 3;
  clip->set_timeline(timeline);
  clip->set_frame_script(2, std::make_shared<FunctionObject>("added", &file, added_method));
  ASSERT_TRUE(clip->advance_frame(interpreter).ok());
  Value added;
  EXPECT_TRUE(runtime.global_object()->read_value(public_name("addedRan"), added));
  Value traited;
  EXPECT_FALSE(runtime.global_object()->read_value(public_name("traitRan"), traited));
}

TEST(Avm2Events, BoundMethodListenerRunsWithItsReceiverAndRemovesByAFreshRead) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t holder = script.multiname("Holder");
  const std::uint32_t object = script.multiname("Object");
  const std::uint32_t tick = script.multiname("tick");
  const std::uint32_t ticked_on = script.multiname("tickedOn");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder tick_code;
  tick_code.getlocal0();
  tick_code.getglobalscope();
  tick_code.swap();
  tick_code.setproperty(ticked_on);
  tick_code.returnvoid();
  const std::uint32_t tick_method = script.method("tick", tick_code.bytes(), 1);
  avm2_test::CodeBuilder init_code;
  init_code.returnvoid();
  const std::uint32_t init = script.method("init", init_code.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  abc::AbcTrait tick_trait;
  tick_trait.name = tick;
  tick_trait.kind = abc::AbcTraitKind::Method;
  tick_trait.method_index = tick_method;
  abc::AbcInstanceInfo instance;
  instance.name = holder;
  instance.super_index = object;
  instance.iinit = constructor;
  instance.traits.push_back(tick_trait);
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);

  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  auto receiver = std::make_shared<fo4::avm2::Object>();
  receiver->set_instance_class(const_cast<fo4::avm2::Class*>(runtime.find_class("Holder")));
  auto dispatcher = make_sprite(runtime);
  ASSERT_NE(dispatcher, nullptr);

  const ExecutionResult first = interpreter.read_property(*receiver, public_name("tick"), 0);
  ASSERT_TRUE(first.ok()) << first.message;
  ASSERT_TRUE(fo4::avm2::add_event_listener(*dispatcher, "ping", first.value, false, 0));
  auto event = fo4::avm2::make_event(runtime, "ping", false, false);
  ASSERT_NE(event, nullptr);
  ASSERT_TRUE(fo4::avm2::dispatch_event(interpreter, dispatcher, event, 0).ok());
  Value ticked;
  ASSERT_TRUE(runtime.global_object()->read_value(public_name("tickedOn"), ticked));
  ASSERT_TRUE(ticked.is_object());
  EXPECT_EQ(ticked.as_object().get(), receiver.get());

  const ExecutionResult again = interpreter.read_property(*receiver, public_name("tick"), 0);
  ASSERT_TRUE(again.ok()) << again.message;
  EXPECT_TRUE(fo4::avm2::remove_event_listener(*dispatcher, "ping", again.value, false));
}

}  // namespace
