#include <fo4/avm2/display.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/snapshot.hpp>
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

fo4::avm2::Value property(Interpreter& interpreter, const fo4::avm2::Value& object,
                          const char* name) {
  if (!object.is_object()) return fo4::avm2::Value::undefined();
  fo4::avm2::Multiname multiname = fo4::avm2::builtin_name(name);
  fo4::avm2::Value out;
  if (!object.as_object()->read_value(multiname, out)) return fo4::avm2::Value::undefined();
  return out;
}

TEST(Avm2Compat, ConstructsBitmapDataAndReadsItsFields) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t bitmap_data = script.multiname("BitmapData", "flash.display");
  code.getlex(bitmap_data);
  code.pushbyte(10);
  code.pushbyte(20);
  code.pushtrue();
  code.pushbyte(0);
  code.construct(4);
  code.returnvalue();
  const ExecutionResult result = harness.run(script, code, 1);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_DOUBLE_EQ(property(harness.interpreter, result.value, "width").as_number(), 10.0);
  EXPECT_DOUBLE_EQ(property(harness.interpreter, result.value, "height").as_number(), 20.0);
  EXPECT_TRUE(property(harness.interpreter, result.value, "transparent").as_boolean());
}

TEST(Avm2Compat, ConstructsFiltersAndKeyboardConstants) {
  Harness harness;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t blur = script.multiname("BlurFilter", "flash.filters");
  const std::uint32_t keyboard = script.multiname("Keyboard", "flash.ui");
  const std::uint32_t up = script.multiname("UP");
  code.getlex(blur);
  code.pushbyte(2);
  code.pushbyte(3);
  code.pushbyte(1);
  code.construct(3);
  code.setlocal(1);
  code.getlocal(1);
  code.getproperty(script.multiname("blurX"));
  code.returnvalue();
  const ExecutionResult filter_result = harness.run(script, code, 2);
  ASSERT_TRUE(filter_result.ok()) << filter_result.message;
  EXPECT_DOUBLE_EQ(filter_result.value.as_number(), 2.0);

  Harness second;
  avm2_test::ScriptBuilder key_script;
  avm2_test::CodeBuilder key_code;
  const std::uint32_t key_class = key_script.multiname("Keyboard", "flash.ui");
  const std::uint32_t key_up = key_script.multiname("UP");
  key_code.getlex(key_class);
  key_code.getproperty(key_up);
  key_code.returnvalue();
  const ExecutionResult key_result = second.run(key_script, key_code, 1);
  ASSERT_TRUE(key_result.ok()) << key_result.message;
  EXPECT_EQ(key_result.value.as_uint32(), 38u);

  Harness third;
  avm2_test::ScriptBuilder package_script;
  avm2_test::CodeBuilder package_code;
  const std::uint32_t qualified_index =
      package_script.multiname("getQualifiedClassName", "flash.utils");
  package_code.pushnull();
  package_code.getlex(qualified_index);
  package_code.getlex(package_script.multiname("Sprite", "flash.display"));
  package_code.call(1);
  package_code.returnvalue();
  const ExecutionResult package_result = third.run(package_script, package_code, 1);
  ASSERT_TRUE(package_result.ok()) << package_result.message;
  ASSERT_TRUE(package_result.value.is_string());
  EXPECT_EQ(package_result.value.as_string(), "flash.display::Sprite");
}

TEST(Avm2Compat, DisplayFiltersAndBitmapDataReachTheSnapshot) {
  fo4::avm2::DisplayObject object(nullptr);
  object.set_filters({fo4::avm2::Value::number(1.0)});
  object.set_bitmap_data(fo4::avm2::Value::number(2.0));
  const fo4::avm2::FrameSnapshot snapshot = fo4::avm2::build_frame_snapshot(object);
  ASSERT_EQ(snapshot.entries.size(), 1u);
  EXPECT_EQ(snapshot.entries[0].filter_count, 1u);
}

}  // namespace
