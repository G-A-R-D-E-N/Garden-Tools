#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;
using fo4::avm2::to_number;

constexpr std::uint8_t kHasnext = 0x1F;
constexpr std::uint8_t kNextname = 0x1E;
constexpr std::uint8_t kNextvalue = 0x23;
constexpr std::uint8_t kHasnext2 = 0x32;
constexpr std::uint8_t kSxi1 = 0x50;
constexpr std::uint8_t kSxi8 = 0x51;
constexpr std::uint8_t kSxi16 = 0x52;
constexpr std::uint8_t kCoerceB = 0x81;
constexpr std::uint8_t kCoerceI = 0x83;
constexpr std::uint8_t kCoerceD = 0x84;
constexpr std::uint8_t kCoerceU = 0x88;
constexpr std::uint8_t kCoerceO = 0x89;

ExecutionResult run_init(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                         std::uint32_t local_count = 1) {
  const std::uint32_t method = script.method("init", code.bytes(), local_count);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

ExecutionResult run_sign_extend(std::int32_t input, std::uint8_t opcode) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(input));
  code.op(opcode);
  code.returnvalue();
  return run_init(script, code);
}

void push_one_property_object(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.pushstring(script.string_of("value"));
  code.pushbyte(7);
  code.newobject(1);
  code.setlocal(1);
  code.pushstring(script.string_of(""));
  code.setlocal(3);
  code.pushbyte(0);
  code.setlocal(2);
}

void append_name_and_value(avm2_test::CodeBuilder& code) {
  code.getlocal(3);
  code.getlocal(1);
  code.getlocal(2);
  code.op(kNextname);
  code.add();
  code.getlocal(1);
  code.getlocal(2);
  code.op(kNextvalue);
  code.add();
  code.setlocal(3);
}

TEST(Avm2MoreOps, SxiOneSignExtendsTheLowBit) {
  const ExecutionResult odd = run_sign_extend(3, kSxi1);
  ASSERT_TRUE(odd.ok()) << odd.message;
  EXPECT_EQ(odd.value.as_int32(), -1);
  const ExecutionResult even = run_sign_extend(2, kSxi1);
  ASSERT_TRUE(even.ok()) << even.message;
  EXPECT_EQ(even.value.as_int32(), 0);
}

TEST(Avm2MoreOps, SxiEightSignExtendsTheLowByte) {
  const ExecutionResult all_ones = run_sign_extend(0x1FF, kSxi8);
  ASSERT_TRUE(all_ones.ok()) << all_ones.message;
  EXPECT_EQ(all_ones.value.as_int32(), -1);
  const ExecutionResult high_bit = run_sign_extend(0x180, kSxi8);
  ASSERT_TRUE(high_bit.ok()) << high_bit.message;
  EXPECT_EQ(high_bit.value.as_int32(), -128);
  const ExecutionResult positive = run_sign_extend(0x7F, kSxi8);
  ASSERT_TRUE(positive.ok()) << positive.message;
  EXPECT_EQ(positive.value.as_int32(), 127);
}

TEST(Avm2MoreOps, SxiSixteenSignExtendsTheLowHalf) {
  const ExecutionResult negative = run_sign_extend(0x18000, kSxi16);
  ASSERT_TRUE(negative.ok()) << negative.message;
  EXPECT_EQ(negative.value.as_int32(), -32768);
  const ExecutionResult positive = run_sign_extend(0x12345, kSxi16);
  ASSERT_TRUE(positive.ok()) << positive.message;
  EXPECT_EQ(positive.value.as_int32(), 0x2345);
}

TEST(Avm2MoreOps, CoerceBMakesANonZeroIntABoolean) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(7);
  code.op(kCoerceB);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2MoreOps, CoerceITruncatesADoubleToAnInt) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushdouble(script.add_double(3.9));
  code.op(kCoerceI);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 3);
}

TEST(Avm2MoreOps, CoerceDMakesAnIntANumber) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(7);
  code.op(kCoerceD);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_FALSE(result.value.is_int32());
  EXPECT_TRUE(result.value.is_number_like());
  EXPECT_EQ(to_number(result.value), 7.0);
}

TEST(Avm2MoreOps, CoerceUWrapsANegativeIntToAUint) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(-1);
  code.op(kCoerceU);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_uint32());
  EXPECT_EQ(result.value.as_uint32(), 4294967295u);
}

TEST(Avm2MoreOps, CoerceOTurnsUndefinedIntoNullAndKeepsOtherValues) {
  avm2_test::ScriptBuilder undefined_script;
  avm2_test::CodeBuilder undefined_code;
  undefined_code.pushundefined();
  undefined_code.op(kCoerceO);
  undefined_code.returnvalue();
  const ExecutionResult undefined_result = run_init(undefined_script, undefined_code);
  ASSERT_TRUE(undefined_result.ok()) << undefined_result.message;
  EXPECT_TRUE(undefined_result.value.is_null());

  avm2_test::ScriptBuilder int_script;
  avm2_test::CodeBuilder int_code;
  int_code.pushbyte(5);
  int_code.op(kCoerceO);
  int_code.returnvalue();
  const ExecutionResult int_result = run_init(int_script, int_code);
  ASSERT_TRUE(int_result.ok()) << int_result.message;
  ASSERT_TRUE(int_result.value.is_int32());
  EXPECT_EQ(int_result.value.as_int32(), 5);
}

TEST(Avm2MoreOps, HasnextReturnsOneBasedIndexThenZeroAtTheEnd) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  push_one_property_object(script, code);
  code.getlocal(1);
  code.pushbyte(0);
  code.op(kHasnext);
  code.pushbyte(10);
  code.multiply();
  code.getlocal(1);
  code.pushbyte(1);
  code.op(kHasnext);
  code.add();
  code.returnvalue();
  const ExecutionResult result = run_init(script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(to_number(result.value), 10.0);
}

TEST(Avm2MoreOps, HasnextOnANonObjectEnumeratesNothing) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushbyte(5);
  code.pushbyte(0);
  code.op(kHasnext);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(to_number(result.value), 0.0);
}

TEST(Avm2MoreOps, HasnextRefusesAShortStackWithStackUnderflow) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.op(kHasnext);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::StackUnderflow) << result.message;
}

TEST(Avm2MoreOps, HasnextLoopAgreesWithNextnameAndNextvalue) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  push_one_property_object(script, code);
  code.label("loop");
  code.getlocal(1);
  code.getlocal(2);
  code.op(kHasnext);
  code.dup();
  code.setlocal(2);
  code.iffalse("end");
  append_name_and_value(code);
  code.jump("loop");
  code.label("end");
  code.getlocal(3);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "value7");
}

TEST(Avm2MoreOps, Hasnext2LoopAgreesWithNextnameAndNextvalue) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  push_one_property_object(script, code);
  code.label("loop");
  code.op_u30(kHasnext2, 1);
  code.u30(2);
  code.iffalse("end");
  append_name_and_value(code);
  code.jump("loop");
  code.label("end");
  code.getlocal(3);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "value7");
}

TEST(Avm2MoreOps, HasnextLoopVisitsEveryKeyOfAnObject) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a"));
  code.pushbyte(1);
  code.pushstring(script.string_of("b"));
  code.pushbyte(2);
  code.newobject(2);
  code.setlocal(1);
  code.pushbyte(0);
  code.setlocal(3);
  code.pushbyte(0);
  code.setlocal(2);
  code.label("loop");
  code.getlocal(1);
  code.getlocal(2);
  code.op(kHasnext);
  code.dup();
  code.setlocal(2);
  code.iffalse("end");
  code.getlocal(3);
  code.getlocal(1);
  code.getlocal(2);
  code.op(kNextvalue);
  code.add();
  code.pushbyte(100);
  code.add();
  code.setlocal(3);
  code.jump("loop");
  code.label("end");
  code.getlocal(3);
  code.returnvalue();
  const ExecutionResult result = run_init(script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(to_number(result.value), 203.0);
}

TEST(Avm2MoreOps, NewOpcodesRefuseAnEmptyStackWithStackUnderflow) {
  const std::uint8_t opcodes[] = {kSxi1, kSxi8, kSxi16, kCoerceB, kCoerceI,
                                  kCoerceD, kCoerceU, kCoerceO, kHasnext};
  for (const std::uint8_t opcode : opcodes) {
    avm2_test::ScriptBuilder script;
    avm2_test::CodeBuilder code;
    code.op(opcode);
    code.returnvalue();
    const ExecutionResult result = run_init(script, code);
    EXPECT_EQ(result.status, InterpreterStatus::StackUnderflow) << result.message;
  }
}

}  // namespace
