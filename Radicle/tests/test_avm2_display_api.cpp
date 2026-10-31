#include <fo4/avm2/display.hpp>
#include <fo4/avm2/events.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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

TEST(Avm2DisplayApi, DisplayPropertyResolvesThroughTheScopeChain) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t x = script.multiname("x");
  avm2_test::CodeBuilder read_code;
  read_code.getlocal0();
  read_code.pushscope();
  read_code.getlex(x);
  read_code.returnvalue();
  const std::uint32_t read_method = script.method("readX", read_code.bytes(), 1);
  avm2_test::CodeBuilder init_code;
  init_code.returnvoid();
  const std::uint32_t init = script.method("init", init_code.bytes(), 1);
  abc::AbcFile file = script.finish(init);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  auto sprite = fo4::avm2::make_display_object(
      runtime, *const_cast<fo4::avm2::Class*>(runtime.find_class("flash.display::Sprite")));
  ASSERT_NE(sprite, nullptr);
  sprite->set_x(5.0);
  const fo4::avm2::FunctionObject read("readX", &file, read_method);
  const ExecutionResult result =
      interpreter.call_function(read, fo4::avm2::Value::object(sprite), {}, 0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 5.0);
}

TEST(Avm2DisplayApi, ClassExtendingSpriteConstructsADisplayObject) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t panel = script.multiname("Panel");
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder script_code;
  script_code.getlex(sprite);
  script_code.newclass(0);
  script_code.construct(0);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcInstanceInfo instance;
  instance.name = panel;
  instance.super_index = sprite;
  instance.iinit = constructor;
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Display);
  ASSERT_NE(result.value.as_object()->instance_class(), nullptr);
  EXPECT_EQ(result.value.as_object()->instance_class()->name, "Panel");
}

TEST(Avm2DisplayApi, LoaderInfoIsAnEventDispatcher) {
  Runtime runtime;
  ASSERT_TRUE(runtime.link().ok());
  const fo4::avm2::Class* loader_info = runtime.find_class("flash.display::LoaderInfo");
  const fo4::avm2::Class* dispatcher = runtime.find_class("flash.events::EventDispatcher");
  ASSERT_NE(loader_info, nullptr);
  ASSERT_NE(dispatcher, nullptr);
  EXPECT_TRUE(loader_info->is_subclass_of(dispatcher));
}

TEST(Avm2DisplayApi, AbcGetterOverridesTheNativeDisplayProperty) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t panel = script.multiname("Panel");
  const std::uint32_t sprite = script.multiname("Sprite", "flash.display");
  const std::uint32_t width = script.multiname("width");
  avm2_test::CodeBuilder constructor_code;
  constructor_code.returnvoid();
  const std::uint32_t constructor = script.method("ctor", constructor_code.bytes(), 1);
  avm2_test::CodeBuilder getter_code;
  getter_code.pushbyte(42);
  getter_code.returnvalue();
  const std::uint32_t getter = script.method("getWidth", getter_code.bytes(), 1);
  avm2_test::CodeBuilder script_code;
  script_code.getlex(sprite);
  script_code.newclass(0);
  script_code.construct(0);
  script_code.returnvalue();
  const std::uint32_t script_method = script.method("init", script_code.bytes(), 1);
  abc::AbcFile file = script.finish(script_method);
  abc::AbcTrait getter_trait;
  getter_trait.name = width;
  getter_trait.kind = abc::AbcTraitKind::Getter;
  getter_trait.method_index = getter;
  abc::AbcInstanceInfo instance;
  instance.name = panel;
  instance.super_index = sprite;
  instance.iinit = constructor;
  instance.traits.push_back(getter_trait);
  file.instances.push_back(instance);
  abc::AbcClassInfo klass;
  klass.cinit = constructor;
  file.classes.push_back(klass);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter{runtime};
  const ExecutionResult made = interpreter.run_script(0);
  ASSERT_TRUE(made.ok()) << made.message;
  ASSERT_TRUE(made.value.is_object());
  fo4::avm2::Multiname multiname;
  multiname.name = "width";
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  const ExecutionResult read =
      interpreter.read_property(*made.value.as_object(), multiname, 0);
  ASSERT_TRUE(read.ok()) << read.message;
  ASSERT_TRUE(read.value.is_number_like());
  EXPECT_EQ(read.value.as_number(), 42.0);
}

TEST(Avm2DisplayApi, ChildInstanceNameIsAPropertyOfItsParent) {
  Runtime runtime;
  ASSERT_TRUE(runtime.link().ok());
  auto clip = std::dynamic_pointer_cast<fo4::avm2::MovieClip>(fo4::avm2::make_display_object(
      runtime, *const_cast<fo4::avm2::Class*>(runtime.find_class("flash.display::MovieClip"))));
  auto child = fo4::avm2::make_display_object(
      runtime, *const_cast<fo4::avm2::Class*>(runtime.find_class("flash.display::Sprite")));
  ASSERT_NE(clip, nullptr);
  ASSERT_NE(child, nullptr);
  child->set_name("List_mc");
  ASSERT_TRUE(static_cast<fo4::avm2::DisplayObjectContainer*>(clip.get())->add_child(child));
  Interpreter interpreter{runtime};
  fo4::avm2::Multiname multiname;
  multiname.name = "List_mc";
  multiname.namespaces.push_back(fo4::avm2::Namespace{});
  const ExecutionResult read = interpreter.read_property(*clip, multiname, 0);
  ASSERT_TRUE(read.ok()) << read.message;
  ASSERT_TRUE(read.value.is_object());
  EXPECT_EQ(read.value.as_object().get(), static_cast<fo4::avm2::Object*>(child.get()));
}

}  // namespace
