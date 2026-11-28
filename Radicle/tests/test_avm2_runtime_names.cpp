#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;

ExecutionResult run_initializer(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  const std::uint32_t method = script.method("init", code.bytes(), 2);
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

ExecutionResult rtqname_round_trip(const char* read_space) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t space = script.string_of("pkg");
  const std::uint32_t read_index = script.string_of(read_space);
  const std::uint32_t qualified = script.rtqname("k");
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(space);
  code.pushbyte(3);
  code.setproperty(qualified);
  code.getlocal(1);
  code.pushstring(read_index);
  code.getproperty(qualified);
  code.returnvalue();
  return run_initializer(script, code);
}

TEST(Avm2RuntimeNames, GetpropertyReadsAPlainObjectByRuntimeName) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("k");
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.pushstring(key);
  code.pushbyte(42);
  code.newobject(1);
  code.pushstring(key);
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 42.0);
}

TEST(Avm2RuntimeNames, SetpropertyStoresUnderTheRuntimeNameForReadBack) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("k");
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(key);
  code.pushbyte(7);
  code.setproperty(runtime_name);
  code.getlocal(1);
  code.pushstring(key);
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 7.0);
}

TEST(Avm2RuntimeNames, CallpropertyCallsAFunctionStoredUnderARuntimeName) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("f");
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder body;
  body.pushbyte(5);
  body.returnvalue();
  const std::uint32_t function = script.method("five", body.bytes(), 1);
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(key);
  code.newfunction(function);
  code.setproperty(runtime_name);
  code.getlocal(1);
  code.pushstring(key);
  code.callproperty(runtime_name, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 5.0);
}

TEST(Avm2RuntimeNames, ArrayReadsAnIntegerRuntimeNameAsAnIndex) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.newarray(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushbyte(0);
  code.pushbyte(9);
  code.setproperty(runtime_name);
  code.getlocal(1);
  code.pushbyte(0);
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 9.0);
}

TEST(Avm2RuntimeNames, RuntimeNameOperandsLeaveTheStackBalanced) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("k");
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.pushbyte(100);
  code.getlocal(1);
  code.pushstring(key);
  code.pushbyte(5);
  code.setproperty(runtime_name);
  code.getlocal(1);
  code.pushstring(key);
  code.getproperty(runtime_name);
  code.pop();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 100.0);
}

TEST(Avm2RuntimeNames, MissingRuntimeNameLeavesTheStackBalanced) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("missing");
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.pushbyte(77);
  code.newobject(0);
  code.pushstring(key);
  code.getproperty(runtime_name);
  code.pop();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 77.0);
}

TEST(Avm2RuntimeNames, RtqnameTakesItsNamespaceFromTheStack) {
  const ExecutionResult same = rtqname_round_trip("pkg");
  ASSERT_TRUE(same.ok()) << same.message;
  ASSERT_TRUE(same.value.is_number_like());
  EXPECT_DOUBLE_EQ(same.value.as_number(), 3.0);
  const ExecutionResult other = rtqname_round_trip("other");
  ASSERT_TRUE(other.ok()) << other.message;
  EXPECT_TRUE(other.value.is_undefined());
}

TEST(Avm2RuntimeNames, RtqnameLTakesItsNamespaceAndNameFromTheStack) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t space = script.string_of("pkg");
  const std::uint32_t key = script.string_of("k");
  const std::uint32_t dynamic_name = script.rtqname_l();
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(space);
  code.pushstring(key);
  code.pushbyte(13);
  code.setproperty(dynamic_name);
  code.getlocal(1);
  code.pushstring(space);
  code.pushstring(key);
  code.getproperty(dynamic_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 13.0);
}

TEST(Avm2RuntimeNames, InitpropertyDefinesUnderTheRuntimeName) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("k");
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(key);
  code.pushbyte(9);
  code.initproperty(runtime_name);
  code.getlocal(1);
  code.pushstring(key);
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 9.0);
}

TEST(Avm2RuntimeNames, NegativeRuntimeNameStoresADynamicKey) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t negative = script.add_int(-1);
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.newarray(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushint(negative);
  code.pushbyte(5);
  code.setproperty(runtime_name);
  code.getlocal(1);
  code.pushint(negative);
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 5.0);
}

TEST(Avm2RuntimeNames, SetpropertyWritesUnderThePublicNamespaceFirst) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t key = script.string_of("k");
  const std::uint32_t mixed = script.namespace_set({script.namespace_of("pkg"), 1u});
  const std::uint32_t runtime_name = script.multiname_l(mixed);
  const std::uint32_t fixed = script.multiname("k");
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(key);
  code.pushbyte(11);
  code.setproperty(runtime_name);
  code.getlocal(1);
  code.getproperty(fixed);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 11.0);
}

TEST(Avm2RuntimeNames, GetpropertyWithoutOperandsIsAStackUnderflow) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::StackUnderflow);
}

TEST(Avm2RuntimeNames, GetpropertyWithOneOperandIsAStackUnderflow) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t runtime_name = script.multiname_l();
  avm2_test::CodeBuilder code;
  code.pushbyte(1);
  code.getproperty(runtime_name);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::StackUnderflow);
}

}  // namespace
