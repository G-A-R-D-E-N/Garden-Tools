#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterLimits;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Multiname;
using fo4::avm2::MultinameKind;
using fo4::avm2::Namespace;
using fo4::avm2::NamespaceKind;
using fo4::avm2::Runtime;
using fo4::avm2::Value;

TEST(Avm2Interpreter, ClassObjectIsAnInstanceOfClassSoAsClassKeepsIt) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t object_name = script.multiname("Object");
  const std::uint32_t class_name = script.multiname("Class");
  code.getlex(object_name);
  code.getlex(class_name);
  code.op(0x87);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_object());
  EXPECT_EQ(result.value.as_object()->kind(), fo4::avm2::ObjectKind::Class);
}

TEST(Avm2Interpreter, ClassObjectIsNotAFunction) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t object_name = script.multiname("Object");
  const std::uint32_t function_name = script.multiname("Function");
  code.getlex(object_name);
  code.getlex(function_name);
  code.op(0xB3);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult result = interpreter.run_script(0);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_FALSE(result.value.as_boolean());
}

void push_five(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder& code) { code.pushbyte(5); }

void push_minus_one(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder& code) { code.pushbyte(-1); }

void push_a(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.pushstring(script.string_of("a"));
}

void push_true(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder& code) { code.pushtrue(); }

void push_null(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder& code) { code.pushnull(); }

void push_five_and_a_half(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.pushdouble(script.add_double(5.5));
}

ExecutionResult run_type_op(void (*push)(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder&),
                            const char* type_name, std::uint8_t opcode) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  push(script, code);
  code.getlex(script.multiname(type_name));
  code.op(opcode);
  code.returnvalue();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

void expect_is(void (*push)(avm2_test::ScriptBuilder&, avm2_test::CodeBuilder&),
               const char* type_name, bool expected) {
  const ExecutionResult result = run_type_op(push, type_name, 0xB3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_EQ(result.value.as_boolean(), expected) << type_name;
}

TEST(Avm2Interpreter, PrimitivesAreInstancesOfTheirOwnClasses) {
  expect_is(push_five, "Number", true);
  expect_is(push_five, "int", true);
  expect_is(push_five, "uint", true);
  expect_is(push_five, "Object", true);
  expect_is(push_a, "String", true);
  expect_is(push_true, "Boolean", true);
  expect_is(push_minus_one, "uint", false);
  expect_is(push_five_and_a_half, "int", false);
  expect_is(push_five_and_a_half, "Number", true);
  expect_is(push_null, "Object", false);
}

TEST(Avm2Interpreter, AsKeepsAPrimitiveOfItsOwnType) {
  const ExecutionResult number = run_type_op(push_five, "Number", 0x87);
  ASSERT_TRUE(number.ok()) << number.message;
  ASSERT_TRUE(number.value.is_int32());
  EXPECT_EQ(number.value.as_int32(), 5);
  const ExecutionResult text = run_type_op(push_a, "String", 0x87);
  ASSERT_TRUE(text.ok()) << text.message;
  ASSERT_TRUE(text.value.is_string());
  EXPECT_EQ(text.value.as_string(), "a");
}

}  // namespace
