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
  fo4::avm2::Multiname multiname = fo4::avm2::builtin_name(name);
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

}  // namespace
