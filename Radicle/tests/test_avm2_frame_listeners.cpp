#include <fo4/avm2/class.hpp>
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
using fo4::avm2::DisplayObject;
using fo4::avm2::ExecutionResult;
using fo4::avm2::FunctionObject;
using fo4::avm2::Interpreter;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::Property;
using fo4::avm2::Runtime;
using fo4::avm2::SwfRuntime;
using fo4::avm2::Value;

Multiname public_name(const char* name) {
  Multiname multiname;
  multiname.kind = MultinameKind::QName;
  multiname.name = name;
  multiname.namespaces.push_back(Namespace{});
  return multiname;
}

fo4::swf::SwfFile minimal_movie() {
  fo4::swf::SwfFile file;
  file.header.frame_rate = 24u * 256u;
  file.header.frame_count = 1;
  file.header.frame_size.x_max = 8000;
  file.header.frame_size.y_max = 6000;
  return file;
}

struct Scene {
  avm2_test::ScriptBuilder script;
  abc::AbcFile file;
  Runtime runtime;
  std::unique_ptr<Interpreter> interpreter;

  std::uint32_t throw_method() {
    avm2_test::CodeBuilder body;
    body.pushbyte(1);
    body.throw_op();
    body.returnvoid();
    return script.method("throw_one", body.bytes(), 2);
  }

  std::uint32_t tag_method(const std::string& tag) {
    avm2_test::CodeBuilder body;
    const std::uint32_t log_name = script.multiname("log");
    body.findpropstrict(log_name);
    body.dup();
    body.getproperty(log_name);
    body.pushstring(script.string_of(tag));
    body.add();
    body.initproperty(log_name);
    body.returnvoid();
    return script.method("tag_" + tag, body.bytes(), 2);
  }

  std::uint32_t tag_and_invalidate(const std::string& tag) {
    avm2_test::CodeBuilder body;
    const std::uint32_t log_name = script.multiname("log");
    body.findpropstrict(log_name);
    body.dup();
    body.getproperty(log_name);
    body.pushstring(script.string_of(tag));
    body.add();
    body.initproperty(log_name);
    body.getglobalscope();
    body.getproperty(script.multiname("theStage"));
    body.callpropvoid(script.multiname("invalidate"), 0);
    body.returnvoid();
    return script.method("tag_and_invalidate_" + tag, body.bytes(), 2);
  }

  void start() {
    const std::uint32_t init = script.method("init", std::vector<std::uint8_t>{0x47}, 1);
    file = script.finish(init);
    runtime.add_file(file);
    runtime.link();
    interpreter = std::make_unique<Interpreter>(runtime);
    define_global("log", Value::string(std::string{}));
  }

  void define_global(const char* name, const Value& value) {
    Property& property = runtime.global_object()->define_own_property(
        fo4::avm2::primary_key(public_name(name)));
    property.kind = fo4::avm2::PropertyKind::Value;
    property.value = value;
  }

  std::string log() {
    Value value;
    if (!runtime.global_object()->read_value(public_name("log"), value) || !value.is_string()) {
      return {};
    }
    return value.as_string();
  }

  Value listener(std::uint32_t method) {
    return Value::object(std::make_shared<FunctionObject>("listener", &file, method));
  }
};

std::shared_ptr<DisplayObject> make_display(Runtime& runtime, const char* name) {
  Class* klass = const_cast<Class*>(runtime.find_class(name));
  if (klass == nullptr) {
    return nullptr;
  }
  return fo4::avm2::make_display_object(runtime, *klass);
}

void call_dispatcher(Scene& scene, const std::shared_ptr<DisplayObject>& target,
                     const char* method, const char* type, const Value& listener,
                     bool use_capture) {
  const fo4::avm2::Trait* trait =
      target->instance_class()->resolve_instance_trait(public_name(method));
  ASSERT_NE(trait, nullptr) << method;
  ASSERT_NE(trait->function, nullptr) << method;
  const ExecutionResult result = scene.interpreter->call_function(
      *trait->function, Value::object(target),
      {Value::string(type), listener, Value::boolean(use_capture)}, 0);
  ASSERT_TRUE(result.ok()) << result.message;
}

void listen(Scene& scene, const std::shared_ptr<DisplayObject>& target, const char* type,
            const Value& listener) {
  call_dispatcher(scene, target, "addEventListener", type, listener, false);
}

void unlisten(Scene& scene, const std::shared_ptr<DisplayObject>& target, const char* type,
              const Value& listener) {
  call_dispatcher(scene, target, "removeEventListener", type, listener, false);
}

std::shared_ptr<SwfRuntime> create_movie(Scene& scene) {
  std::shared_ptr<SwfRuntime> movie =
      SwfRuntime::create(scene.runtime, *scene.interpreter, minimal_movie());
  EXPECT_NE(movie, nullptr) << SwfRuntime::last_error();
  return movie;
}

void invalidate_stage(Scene& scene, const std::shared_ptr<SwfRuntime>& movie) {
  const std::shared_ptr<DisplayObject> stage = movie->stage();
  const fo4::avm2::Trait* trait =
      stage->instance_class()->resolve_instance_trait(public_name("invalidate"));
  ASSERT_NE(trait, nullptr);
  ASSERT_NE(trait->function, nullptr);
  const ExecutionResult result =
      scene.interpreter->call_function(*trait->function, Value::object(stage), {}, 0);
  ASSERT_TRUE(result.ok()) << result.message;
}

}  // namespace

TEST(Avm2FrameListeners, AnOffListObjectReceivesEnterAndExitOncePerFrame) {
  Scene scene;
  const std::uint32_t enter = scene.tag_method("s");
  const std::uint32_t exit = scene.tag_method("e");
  scene.start();
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(shape, nullptr);
  listen(scene, shape, "enterFrame", scene.listener(enter));
  listen(scene, shape, "exitFrame", scene.listener(exit));
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "sesese");
}

TEST(Avm2FrameListeners, RemovingTheListenersStopsDeliveryAndReleasesTheObject) {
  Scene scene;
  const std::uint32_t enter = scene.tag_method("s");
  const std::uint32_t exit = scene.tag_method("e");
  scene.start();
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(shape, nullptr);
  const Value enter_listener = scene.listener(enter);
  const Value exit_listener = scene.listener(exit);
  listen(scene, shape, "enterFrame", enter_listener);
  listen(scene, shape, "exitFrame", exit_listener);
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);
  movie->advance(*scene.interpreter, 0.125);
  ASSERT_EQ(scene.log(), "sesese");

  unlisten(scene, shape, "enterFrame", enter_listener);
  unlisten(scene, shape, "exitFrame", exit_listener);
  std::weak_ptr<DisplayObject> weak = shape;
  shape.reset();
  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "sesese");
  EXPECT_TRUE(weak.expired());
}

namespace {

std::weak_ptr<DisplayObject> start_ticker(Scene& scene, const Value& listener) {
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  if (shape == nullptr) {
    return {};
  }
  listen(scene, shape, "enterFrame", listener);
  return shape;
}

}  // namespace

TEST(Avm2FrameListeners, AnOffListObjectStaysAliveWhileItsListenerIsRegistered) {
  Scene scene;
  const std::uint32_t enter = scene.tag_method("s");
  scene.start();
  std::weak_ptr<DisplayObject> ticker = start_ticker(scene, scene.listener(enter));
  ASSERT_FALSE(ticker.expired());
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "sss");
  EXPECT_FALSE(ticker.expired());
}

TEST(Avm2FrameListeners, AnOffListObjectReceivesACaptureListenerAtItsTarget) {
  Scene scene;
  const std::uint32_t enter = scene.tag_method("s");
  scene.start();
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(shape, nullptr);
  call_dispatcher(scene, shape, "addEventListener", "enterFrame", scene.listener(enter), true);
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "sss");
}

TEST(Avm2FrameListeners, AnObjectOnTheListAndOffItIsDeliveredOncePerFrame) {
  Scene scene;
  const std::uint32_t a = scene.tag_method("a");
  const std::uint32_t b = scene.tag_method("b");
  scene.start();
  auto sprite = make_display(scene.runtime, "flash.display::Sprite");
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(sprite, nullptr);
  ASSERT_NE(shape, nullptr);
  listen(scene, sprite, "enterFrame", scene.listener(a));
  listen(scene, shape, "enterFrame", scene.listener(b));
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);

  ASSERT_TRUE(movie->stage()->add_child(sprite));
  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "ababab");

  ASSERT_TRUE(movie->stage()->remove_child(sprite));
  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "abababababab");
}

TEST(Avm2FrameListeners, ListenersAddedAndRemovedDuringABroadcastTakeEffectWithoutBreakingIt) {
  Scene scene;
  const std::uint32_t b = scene.tag_method("b");
  const std::uint32_t c = scene.tag_method("c");
  avm2_test::CodeBuilder remover;
  const std::uint32_t log_name = scene.script.multiname("log");
  const std::uint32_t remove_name = scene.script.multiname("removeEventListener");
  const std::uint32_t add_name = scene.script.multiname("addEventListener");
  const std::uint32_t self_name = scene.script.multiname("self");
  const std::uint32_t target_name = scene.script.multiname("target_c");
  const std::uint32_t listener_name = scene.script.multiname("listener_c");
  remover.getlocal0();
  remover.pushstring(scene.script.string_of("enterFrame"));
  remover.getglobalscope();
  remover.getproperty(self_name);
  remover.callproperty(remove_name, 2);
  remover.pop();
  remover.getglobalscope();
  remover.getproperty(target_name);
  remover.pushstring(scene.script.string_of("enterFrame"));
  remover.getglobalscope();
  remover.getproperty(listener_name);
  remover.callproperty(add_name, 2);
  remover.pop();
  remover.findpropstrict(log_name);
  remover.dup();
  remover.getproperty(log_name);
  remover.pushstring(scene.script.string_of("a"));
  remover.add();
  remover.initproperty(log_name);
  remover.returnvoid();
  const std::uint32_t remove_and_add = scene.script.method("remove_and_add", remover.bytes(), 2);
  scene.start();

  auto target_a = make_display(scene.runtime, "flash.display::Shape");
  auto target_b = make_display(scene.runtime, "flash.display::Shape");
  auto target_c = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(target_a, nullptr);
  ASSERT_NE(target_b, nullptr);
  ASSERT_NE(target_c, nullptr);
  const Value self_listener = scene.listener(remove_and_add);
  const Value c_listener = scene.listener(c);
  scene.define_global("self", self_listener);
  scene.define_global("target_c", Value::object(target_c));
  scene.define_global("listener_c", c_listener);
  listen(scene, target_a, "enterFrame", self_listener);
  listen(scene, target_b, "enterFrame", scene.listener(b));
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "abbcbc");
}

TEST(Avm2FrameListeners, TearingDownTheRuntimeReleasesTheTrackedObjects) {
  std::weak_ptr<DisplayObject> weak;
  {
    Scene scene;
    const std::uint32_t enter = scene.tag_method("s");
    scene.start();
    auto shape = make_display(scene.runtime, "flash.display::Shape");
    ASSERT_NE(shape, nullptr);
    listen(scene, shape, "enterFrame", scene.listener(enter));
    weak = shape;
    shape.reset();
    ASSERT_FALSE(weak.expired());
  }
  EXPECT_TRUE(weak.expired());
}

TEST(Avm2FrameListeners, AFailingFrameListenerDoesNotStopTheOtherTargetsOrThePlayback) {
  Scene scene;
  const std::uint32_t thrower = scene.throw_method();
  const std::uint32_t tag = scene.tag_method("b");
  scene.start();
  auto failing = make_display(scene.runtime, "flash.display::Shape");
  auto passing = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(failing, nullptr);
  ASSERT_NE(passing, nullptr);
  listen(scene, failing, "enterFrame", scene.listener(thrower));
  listen(scene, passing, "enterFrame", scene.listener(tag));
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "bbb");
  EXPECT_TRUE(movie->playing());
  EXPECT_EQ(scene.interpreter->handler_failures().count, 3u);
}

TEST(Avm2FrameListeners, AnObjectOnAnotherMoviesStageIsNotDeliveredByThisMovie) {
  Scene scene;
  const std::uint32_t tag = scene.tag_method("a");
  scene.start();
  auto sprite = make_display(scene.runtime, "flash.display::Sprite");
  ASSERT_NE(sprite, nullptr);
  listen(scene, sprite, "enterFrame", scene.listener(tag));
  const std::shared_ptr<SwfRuntime> first = create_movie(scene);
  const std::shared_ptr<SwfRuntime> second = create_movie(scene);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_TRUE(first->stage()->add_child(sprite));

  second->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "");
  first->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "aaa");
}

TEST(Avm2FrameListeners, TwoInvalidatesInAFrameGiveOneRender) {
  Scene scene;
  const std::uint32_t render = scene.tag_method("r");
  scene.start();
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);
  listen(scene, movie->stage(), "render", scene.listener(render));
  invalidate_stage(scene, movie);
  invalidate_stage(scene, movie);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "r");
}

TEST(Avm2FrameListeners, WithoutAnInvalidateThereIsNoRender) {
  Scene scene;
  const std::uint32_t render = scene.tag_method("r");
  scene.start();
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);
  listen(scene, movie->stage(), "render", scene.listener(render));

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "");
}

TEST(Avm2FrameListeners, AFailingRenderListenerIsRecordedAndDoesNotStopTheOthers) {
  Scene scene;
  const std::uint32_t thrower = scene.throw_method();
  const std::uint32_t render = scene.tag_method("r");
  scene.start();
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(shape, nullptr);
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);
  listen(scene, movie->stage(), "render", scene.listener(thrower));
  listen(scene, shape, "render", scene.listener(render));
  invalidate_stage(scene, movie);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "r");
  EXPECT_EQ(scene.interpreter->handler_failures().count, 1u);
  EXPECT_TRUE(movie->playing());
  EXPECT_TRUE(movie->frame_error().empty());
}

TEST(Avm2FrameListeners, InvalidateDuringARenderWaitsForTheNextFrame) {
  Scene scene;
  const std::uint32_t render = scene.tag_and_invalidate("r");
  scene.start();
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);
  scene.define_global("theStage", Value::object(movie->stage()));
  listen(scene, movie->stage(), "render", scene.listener(render));
  invalidate_stage(scene, movie);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "rrr");
}

TEST(Avm2FrameListeners, AnOffListObjectReceivesRenderWhileItListens) {
  Scene scene;
  const std::uint32_t render = scene.tag_method("r");
  scene.start();
  auto shape = make_display(scene.runtime, "flash.display::Shape");
  ASSERT_NE(shape, nullptr);
  const std::shared_ptr<SwfRuntime> movie = create_movie(scene);
  ASSERT_NE(movie, nullptr);
  listen(scene, shape, "render", scene.listener(render));
  invalidate_stage(scene, movie);

  movie->advance(*scene.interpreter, 0.125);
  EXPECT_EQ(scene.log(), "r");
}
