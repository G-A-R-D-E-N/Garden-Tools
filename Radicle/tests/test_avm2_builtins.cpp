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



TEST(Avm2Builtins, StringCharAtReturnsTheIndexedCharacter) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t char_at = script.multiname("charAt");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("abc"));
  code.pushbyte(1);
  code.callproperty(char_at, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "b");
}

TEST(Avm2Builtins, StringToUpperCaseRaisesAsciiLetters) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t to_upper = script.multiname("toUpperCase");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("abc"));
  code.callproperty(to_upper, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "ABC");
}

TEST(Avm2Builtins, StringIndexOfFindsASubstring) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t index_of = script.multiname("indexOf");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("hello"));
  code.pushstring(script.string_of("ll"));
  code.callproperty(index_of, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 2);
}

TEST(Avm2Builtins, StringSplitProducesAnArrayThatJoinCanRender) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t split = script.multiname("split");
  const std::uint32_t join = script.multiname("join");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a,b"));
  code.pushstring(script.string_of(","));
  code.callproperty(split, 1);
  code.pushstring(script.string_of("-"));
  code.callproperty(join, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "a-b");
}

TEST(Avm2Builtins, ArrayPushReturnsTheNewLength) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t push = script.multiname("push");
  avm2_test::CodeBuilder code;
  code.pushbyte(1);
  code.pushbyte(2);
  code.newarray(2);
  code.pushbyte(3);
  code.callproperty(push, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 3.0);
}

TEST(Avm2Builtins, ArrayJoinUsesTheGivenSeparator) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t join = script.multiname("join");
  avm2_test::CodeBuilder code;
  code.pushbyte(7);
  code.pushbyte(8);
  code.newarray(2);
  code.pushstring(script.string_of(","));
  code.callproperty(join, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "7,8");
}

TEST(Avm2Builtins, ArrayPopReturnsTheLastElement) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t pop = script.multiname("pop");
  avm2_test::CodeBuilder code;
  code.pushbyte(7);
  code.pushbyte(8);
  code.newarray(2);
  code.callproperty(pop, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 8);
}

TEST(Avm2Builtins, ArraySortOrdersNumericElementsByText) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t sort = script.multiname("sort");
  const std::uint32_t first = script.multiname("0");
  avm2_test::CodeBuilder code;
  code.pushbyte(3);
  code.pushbyte(1);
  code.pushbyte(2);
  code.newarray(3);
  code.callproperty(sort, 0);
  code.getproperty(first);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 1);
}

TEST(Avm2Builtins, ErrorConversionCarriesTheMessage) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t error_class = script.multiname("Error");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.getlex(error_class);
  code.pushstring(script.string_of("boom"));
  code.call(1);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "Error: boom");
}

TEST(Avm2Builtins, TypeErrorConstructionKeepsTheSubclassName) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t type_error = script.multiname("TypeError");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.getlex(type_error);
  code.pushstring(script.string_of("bad value"));
  code.call(1);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "TypeError: bad value");
}

}  // namespace
