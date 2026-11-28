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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::Class;
using fo4::avm2::ExecutionResult;
using fo4::avm2::FunctionObject;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::TimerObject;
using fo4::avm2::Value;

std::uint32_t thrower(avm2_test::ScriptBuilder& script, std::int32_t value) {
  avm2_test::CodeBuilder body;
  body.pushbyte(value);
  body.throw_op();
  body.returnvoid();
  return script.method("throw" + std::to_string(value), body.bytes(), 2);
}

struct ThrowingScene {
  avm2_test::ScriptBuilder script;
  std::uint32_t first{0};
  std::uint32_t second{0};
  abc::AbcFile file;
  Runtime runtime;
  std::unique_ptr<Interpreter> interpreter;

  ThrowingScene() {
    first = thrower(script, 1);
    second = thrower(script, 2);
    const std::uint32_t init = script.method("init", std::vector<std::uint8_t>{0x47}, 1);
    file = script.finish(init);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime);
  }

  Value listener(std::uint32_t method) {
    return Value::object(std::make_shared<FunctionObject>("throw", &file, method));
  }

  Class* timer_class() { return const_cast<Class*>(runtime.find_class("flash.utils::Timer")); }
};

fo4::swf::SwfFile minimal_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 1;
  file.header.frame_size.x_max = 8000;
  file.header.frame_size.y_max = 6000;
  return file;
}

}  // namespace

TEST(Avm2HandlerFailures, AFailingKeyHandlerDoesNotStopTheOthersAndIsRecorded) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  ASSERT_TRUE(fo4::avm2::add_event_listener(*movie->root(), "keyDown", scene.listener(scene.first),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*movie->root(), "keyDown", scene.listener(scene.second),
                                            false, 0));
  movie->key_event(*scene.interpreter, 65, true);
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
  EXPECT_EQ(scene.interpreter->handler_failures().first_message, "uncaught exception: 1");
}

TEST(Avm2HandlerFailures, AFailingTimerTickDoesNotStopTheTimerOrItsCompletion) {
  ThrowingScene scene;
  auto timer = std::make_shared<TimerObject>(scene.timer_class(), 100.0, 2);
  timer->set_running(true);
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*timer, "timer", scene.listener(scene.first), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*timer, "timerComplete",
                                            scene.listener(scene.second), false, 0));
  scene.interpreter->host_clock().register_timer(timer);

  scene.interpreter->advance_host_time(250.0);
  EXPECT_EQ(scene.interpreter->handler_failures().count, 3u);
  EXPECT_EQ(scene.interpreter->handler_failures().first_message, "uncaught exception: 1");
  EXPECT_EQ(timer->current_count(), 2);
  EXPECT_FALSE(timer->running());
}

TEST(Avm2HandlerFailures, AFailingTimerCompleteHandlerIsRecorded) {
  ThrowingScene scene;
  auto timer = std::make_shared<TimerObject>(scene.timer_class(), 100.0, 1);
  timer->set_running(true);
  ASSERT_TRUE(fo4::avm2::add_event_listener(*timer, "timerComplete", scene.listener(scene.first),
                                            false, 0));
  scene.interpreter->host_clock().register_timer(timer);

  scene.interpreter->advance_host_time(100.0);
  EXPECT_EQ(scene.interpreter->handler_failures().count, 1u);
  EXPECT_EQ(scene.interpreter->handler_failures().first_message, "uncaught exception: 1");
}

TEST(Avm2HandlerFailures, AFailingAddedToStageHandlerDoesNotStopTheRestOfTheSubtree) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  Class* sprite_class = const_cast<Class*>(scene.runtime.find_class("flash.display::Sprite"));
  ASSERT_NE(sprite_class, nullptr);
  auto parent = fo4::avm2::make_display_object(scene.runtime, *sprite_class);
  auto first = fo4::avm2::make_display_object(scene.runtime, *sprite_class);
  auto second = fo4::avm2::make_display_object(scene.runtime, *sprite_class);
  auto* container = static_cast<fo4::avm2::DisplayObjectContainer*>(parent.get());
  ASSERT_TRUE(movie->stage()->add_child(parent));
  ASSERT_TRUE(container->add_child(first));
  ASSERT_TRUE(container->add_child(second));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*first, "addedToStage", scene.listener(scene.first),
                                            false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*second, "addedToStage", scene.listener(scene.second),
                                            false, 0));
  const ExecutionResult result = fo4::avm2::dispatch_added(*scene.interpreter, parent, 0);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
}

namespace {

std::shared_ptr<fo4::avm2::DisplayObject> make_shape(Runtime& runtime) {
  Class* klass = const_cast<Class*>(runtime.find_class("flash.display::Shape"));
  return klass == nullptr ? nullptr : fo4::avm2::make_display_object(runtime, *klass);
}

void define_global(Runtime& runtime, const char* name, const Value& value) {
  fo4::avm2::Multiname multiname;
  multiname.kind = fo4::avm2::MultinameKind::QName;
  multiname.name = name;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  fo4::avm2::Property& property =
      runtime.global_object()->define_own_property(fo4::avm2::primary_key(multiname));
  property.kind = fo4::avm2::PropertyKind::Value;
  property.value = value;
}

std::shared_ptr<fo4::avm2::DisplayObject> make_sprite(Runtime& runtime) {
  Class* klass = const_cast<Class*>(runtime.find_class("flash.display::Sprite"));
  return klass == nullptr ? nullptr : fo4::avm2::make_display_object(runtime, *klass);
}

ExecutionResult call_display(Interpreter& interpreter,
                             const std::shared_ptr<fo4::avm2::DisplayObject>& target,
                             const char* method,
                             const std::shared_ptr<fo4::avm2::DisplayObject>& argument) {
  fo4::avm2::Multiname multiname;
  multiname.kind = fo4::avm2::MultinameKind::QName;
  multiname.name = method;
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  const fo4::avm2::Trait* trait = target->instance_class()->resolve_instance_trait(multiname);
  return interpreter.call_function(*trait->function, Value::object(target),
                                   {Value::object(argument)}, 0);
}

}  // namespace

TEST(Avm2HandlerFailures, AFailingAddedListenerDoesNotSkipTheAddedToStageHandlers) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  auto child = make_shape(scene.runtime);
  ASSERT_NE(child, nullptr);
  ASSERT_TRUE(movie->stage()->add_child(child));
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*child, "added", scene.listener(scene.first), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "addedToStage", scene.listener(scene.second),
                                            false, 0));

  const ExecutionResult result = fo4::avm2::dispatch_added(*scene.interpreter, child, 0);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
}

TEST(Avm2HandlerFailures, AFailingRemovedListenerDoesNotSkipTheRemovedFromStageHandlers) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  auto child = make_shape(scene.runtime);
  ASSERT_NE(child, nullptr);
  ASSERT_TRUE(movie->stage()->add_child(child));
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*child, "removed", scene.listener(scene.first), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "removedFromStage",
                                            scene.listener(scene.second), false, 0));

  const ExecutionResult result = fo4::avm2::dispatch_removed(*scene.interpreter, child, 0);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
}

TEST(Avm2HandlerFailures, AFailingFocusOutDoesNotSkipTheFocusInHandler) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  auto previous = make_shape(scene.runtime);
  auto target = make_shape(scene.runtime);
  ASSERT_NE(previous, nullptr);
  ASSERT_NE(target, nullptr);
  ASSERT_TRUE(
      fo4::avm2::set_display_focus(*scene.interpreter, *movie->stage(), previous, 0).ok());
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*previous, "focusOut", scene.listener(scene.first), false, 0));
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*target, "focusIn", scene.listener(scene.second), false, 0));

  const ExecutionResult result =
      fo4::avm2::set_display_focus(*scene.interpreter, *movie->stage(), target, 0);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
}

TEST(Avm2HandlerFailures, AFailingListenerInsideANestedDispatchIsCountedOnce) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t target_name = script.multiname("target_b");
  const std::uint32_t event_class = script.multiname("Event", "flash.events");
  const std::uint32_t dispatch_name = script.multiname("dispatchEvent");
  avm2_test::CodeBuilder outer;
  outer.getglobalscope();
  outer.getproperty(target_name);
  outer.getlex(event_class);
  outer.pushstring(script.string_of("ping"));
  outer.construct(1);
  outer.callproperty(dispatch_name, 1);
  outer.pop();
  outer.returnvoid();
  const std::uint32_t outer_method = script.method("outer", outer.bytes(), 2);
  const std::uint32_t thrower_method = thrower(script, 1);
  const std::uint32_t init = script.method("init", std::vector<std::uint8_t>{0x47}, 1);
  abc::AbcFile file = script.finish(init);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  auto source = make_shape(runtime);
  auto target = make_shape(runtime);
  ASSERT_NE(source, nullptr);
  ASSERT_NE(target, nullptr);
  define_global(runtime, "target_b", Value::object(target));
  ASSERT_TRUE(fo4::avm2::add_event_listener(
      *target, "ping", Value::object(std::make_shared<FunctionObject>("throw", &file, thrower_method)),
      false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(
      *source, "outer", Value::object(std::make_shared<FunctionObject>("outer", &file, outer_method)),
      false, 0));

  auto event = fo4::avm2::make_event(runtime, "outer", false, false);
  ASSERT_NE(event, nullptr);
  const ExecutionResult result = fo4::avm2::dispatch_event(interpreter, source, event, 0);
  EXPECT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(interpreter.handler_failures().count, 1u);
}

TEST(Avm2HandlerFailures, AFailingRemovedListenerStillRemovesTheChildOnce) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  auto child = make_shape(scene.runtime);
  ASSERT_NE(child, nullptr);
  ASSERT_TRUE(movie->stage()->add_child(child));
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*child, "removed", scene.listener(scene.first), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "removedFromStage",
                                            scene.listener(scene.second), false, 0));

  EXPECT_FALSE(call_display(*scene.interpreter, movie->stage(), "removeChild", child).ok());
  EXPECT_EQ(child->parent(), nullptr);
  EXPECT_EQ(child->stage(), nullptr);
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
  EXPECT_TRUE(call_display(*scene.interpreter, movie->stage(), "removeChild", child).ok());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 2u);
}

TEST(Avm2HandlerFailures, AFailingRemovedListenerDuringAMoveLeavesTheChildOnTheNewParentOnly) {
  ThrowingScene scene;
  const std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  ASSERT_NE(movie, nullptr) << SwfRuntime::last_error();
  auto from = make_sprite(scene.runtime);
  auto to = make_sprite(scene.runtime);
  auto child = make_shape(scene.runtime);
  ASSERT_TRUE(from != nullptr && to != nullptr && child != nullptr);
  ASSERT_TRUE(movie->stage()->add_child(from));
  ASSERT_TRUE(movie->stage()->add_child(to));
  auto* source = static_cast<fo4::avm2::DisplayObjectContainer*>(from.get());
  ASSERT_TRUE(source->add_child(child));
  ASSERT_TRUE(
      fo4::avm2::add_event_listener(*child, "removed", scene.listener(scene.first), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "removedFromStage",
                                            scene.listener(scene.second), false, 0));
  ASSERT_TRUE(fo4::avm2::add_event_listener(*child, "addedToStage", scene.listener(scene.second),
                                            false, 0));

  EXPECT_FALSE(call_display(*scene.interpreter, to, "addChild", child).ok());
  EXPECT_EQ(child->parent(), to.get());
  EXPECT_TRUE(source->children().empty());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 3u);
}
