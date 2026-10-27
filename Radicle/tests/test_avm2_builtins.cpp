#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;

ExecutionResult run_initializer(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                                std::uint32_t local_count = 1) {
  const std::uint32_t method = script.method("init", code.bytes(), local_count);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::TypeError;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

TEST(Avm2Builtins, ComputesMathAbsOfANegativeInt) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t math = script.multiname("Math");
  const std::uint32_t abs_name = script.multiname("abs");
  avm2_test::CodeBuilder code;
  code.getlex(math);
  code.pushbyte(-5);
  code.callproperty(abs_name, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 5.0);
}

TEST(Avm2Builtins, ReadsTheMathPiStaticConstant) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t math = script.multiname("Math");
  const std::uint32_t pi = script.multiname("PI");
  avm2_test::CodeBuilder code;
  code.getlex(math);
  code.getproperty(pi);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_NEAR(result.value.as_number(), 3.14159265358979323846, 1e-12);
}

TEST(Avm2Builtins, MathMaxReturnsTheLargerArgument) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t math = script.multiname("Math");
  const std::uint32_t max_name = script.multiname("max");
  avm2_test::CodeBuilder code;
  code.getlex(math);
  code.pushbyte(3);
  code.pushbyte(7);
  code.callproperty(max_name, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 7.0);
}

TEST(Avm2Builtins, StringConversionOfAnIntProducesItsDecimalText) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_class = script.multiname("String");
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.getlex(string_class);
  code.pushbyte(41);
  code.call(1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "41");
}

TEST(Avm2Builtins, CallsToStringOnAnIntPrimitiveThroughBoxing) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  code.pushbyte(7);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "7");
}

TEST(Avm2Builtins, BooleanConversionOfZeroIsFalse) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t boolean_class = script.multiname("Boolean");
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.getlex(boolean_class);
  code.pushbyte(0);
  code.call(1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_FALSE(result.value.as_boolean());
}

TEST(Avm2Builtins, TopLevelIsNaNRecognisesNonNumericText) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t is_nan = script.multiname("isNaN");
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.getlex(is_nan);
  code.pushstring(script.string_of("not a number"));
  code.call(1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2Builtins, NewObjectResolvesObjectToString) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "[object Object]");
}

}  // namespace
