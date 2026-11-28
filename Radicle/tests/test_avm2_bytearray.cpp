#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;

ExecutionResult run_program(Runtime& runtime, avm2_test::ScriptBuilder& script,
                            avm2_test::CodeBuilder& code, std::uint32_t local_count) {
  const std::uint32_t method = script.method("init", code.bytes(), local_count);
  abc::AbcFile file = script.finish(method);
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::TypeError;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

void new_byte_array(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  code.getlex(script.multiname("ByteArray", "flash.utils"));
  code.construct(0);
  code.setlocal(1);
}

void write_byte(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                std::int32_t value) {
  code.getlocal(1);
  code.pushint(script.add_int(value));
  code.callproperty(script.multiname("writeByte"), 1);
  code.pop();
}

void set_index(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
               std::int32_t index, std::int32_t value) {
  const std::uint32_t runtime_name = script.multiname_l();
  code.getlocal(1);
  code.pushint(script.add_int(index));
  code.pushint(script.add_int(value));
  code.setproperty(runtime_name);
}

void get_index(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
               std::int32_t index) {
  const std::uint32_t runtime_name = script.multiname_l();
  code.getlocal(1);
  code.pushint(script.add_int(index));
  code.getproperty(runtime_name);
}

void set_position(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                  std::int32_t offset) {
  code.getlocal(1);
  code.pushbyte(offset);
  code.setproperty(script.multiname("position"));
}

void read_property(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                   const char* name) {
  code.getlocal(1);
  code.getproperty(script.multiname(name));
}

TEST(Avm2ByteArray, ReadsBackWhatWasWrittenFromPositionZero) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  write_byte(script, code, 7);
  write_byte(script, code, 9);
  set_position(script, code, 0);
  code.getlocal(1);
  code.callproperty(script.multiname("readUnsignedByte"), 0);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 7.0);
}

TEST(Avm2ByteArray, PositionAdvancesWithEachWrite) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  write_byte(script, code, 7);
  write_byte(script, code, 9);
  read_property(script, code, "position");
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 2.0);
}

TEST(Avm2ByteArray, LengthGrowsWithWrites) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  write_byte(script, code, 1);
  write_byte(script, code, 2);
  write_byte(script, code, 3);
  read_property(script, code, "length");
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 3.0);
}

TEST(Avm2ByteArray, SettingLengthZeroFillsWhenGrowingAndTruncatesWhenShrinking) {
  Runtime grow_runtime;
  avm2_test::ScriptBuilder grow_script;
  avm2_test::CodeBuilder grow_code;
  new_byte_array(grow_script, grow_code);
  grow_code.getlocal(1);
  grow_code.pushbyte(5);
  grow_code.setproperty(grow_script.multiname("length"));
  get_index(grow_script, grow_code, 4);
  grow_code.returnvalue();
  const ExecutionResult grown = run_program(grow_runtime, grow_script, grow_code, 2);
  ASSERT_TRUE(grown.ok()) << grown.message;
  ASSERT_TRUE(grown.value.is_number_like());
  EXPECT_EQ(grown.value.as_number(), 0.0);

  Runtime shrink_runtime;
  avm2_test::ScriptBuilder shrink_script;
  avm2_test::CodeBuilder shrink_code;
  new_byte_array(shrink_script, shrink_code);
  write_byte(shrink_script, shrink_code, 1);
  write_byte(shrink_script, shrink_code, 2);
  write_byte(shrink_script, shrink_code, 3);
  write_byte(shrink_script, shrink_code, 4);
  shrink_code.getlocal(1);
  shrink_code.pushbyte(2);
  shrink_code.setproperty(shrink_script.multiname("length"));
  read_property(shrink_script, shrink_code, "length");
  shrink_code.returnvalue();
  const ExecutionResult shrunk = run_program(shrink_runtime, shrink_script, shrink_code, 2);
  ASSERT_TRUE(shrunk.ok()) << shrunk.message;
  ASSERT_TRUE(shrunk.value.is_number_like());
  EXPECT_EQ(shrunk.value.as_number(), 2.0);
}

TEST(Avm2ByteArray, IndexWriteExtendsTheArrayAndIndexReadReturnsTheByte) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  set_index(script, code, 4, 9);
  get_index(script, code, 4);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 9.0);
}

TEST(Avm2ByteArray, IndexWriteGrowsLengthToTheWrittenIndex) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  set_index(script, code, 4, 9);
  read_property(script, code, "length");
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 5.0);
}

TEST(Avm2ByteArray, IndexReadPastTheEndAndUnwrittenIndexesReadZero) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  set_index(script, code, 4, 9);
  get_index(script, code, 0);
  code.returnvalue();
  const ExecutionResult unwritten = run_program(runtime, script, code, 2);
  ASSERT_TRUE(unwritten.ok()) << unwritten.message;
  ASSERT_TRUE(unwritten.value.is_number_like());
  EXPECT_EQ(unwritten.value.as_number(), 0.0);

  Runtime past_runtime;
  avm2_test::ScriptBuilder past_script;
  avm2_test::CodeBuilder past_code;
  new_byte_array(past_script, past_code);
  set_index(past_script, past_code, 4, 9);
  get_index(past_script, past_code, 10);
  past_code.returnvalue();
  const ExecutionResult past = run_program(past_runtime, past_script, past_code, 2);
  ASSERT_TRUE(past.ok()) << past.message;
  ASSERT_TRUE(past.value.is_number_like());
  EXPECT_EQ(past.value.as_number(), 0.0);
}

TEST(Avm2ByteArray, ReadByteIsSignedAndReadUnsignedByteIsNot) {
  Runtime signed_runtime;
  avm2_test::ScriptBuilder signed_script;
  avm2_test::CodeBuilder signed_code;
  new_byte_array(signed_script, signed_code);
  write_byte(signed_script, signed_code, 255);
  set_position(signed_script, signed_code, 0);
  signed_code.getlocal(1);
  signed_code.callproperty(signed_script.multiname("readByte"), 0);
  signed_code.returnvalue();
  const ExecutionResult signed_result = run_program(signed_runtime, signed_script, signed_code, 2);
  ASSERT_TRUE(signed_result.ok()) << signed_result.message;
  ASSERT_TRUE(signed_result.value.is_number_like());
  EXPECT_EQ(signed_result.value.as_number(), -1.0);

  Runtime unsigned_runtime;
  avm2_test::ScriptBuilder unsigned_script;
  avm2_test::CodeBuilder unsigned_code;
  new_byte_array(unsigned_script, unsigned_code);
  write_byte(unsigned_script, unsigned_code, 255);
  set_position(unsigned_script, unsigned_code, 0);
  unsigned_code.getlocal(1);
  unsigned_code.callproperty(unsigned_script.multiname("readUnsignedByte"), 0);
  unsigned_code.returnvalue();
  const ExecutionResult unsigned_result =
      run_program(unsigned_runtime, unsigned_script, unsigned_code, 2);
  ASSERT_TRUE(unsigned_result.ok()) << unsigned_result.message;
  ASSERT_TRUE(unsigned_result.value.is_number_like());
  EXPECT_EQ(unsigned_result.value.as_number(), 255.0);
}

TEST(Avm2ByteArray, WriteUtfBytesThenReadUtfBytesRoundTripsMultiByteText) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  code.getlocal(1);
  code.pushstring(script.string_of("caf\xC3\xA9"));
  code.callproperty(script.multiname("writeUTFBytes"), 1);
  code.pop();
  set_position(script, code, 0);
  code.getlocal(1);
  code.pushbyte(5);
  code.callproperty(script.multiname("readUTFBytes"), 1);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "caf\xC3\xA9");
}

TEST(Avm2ByteArray, EndianDefaultsToBigAndReadsBackWhatWasSet) {
  Runtime default_runtime;
  avm2_test::ScriptBuilder default_script;
  avm2_test::CodeBuilder default_code;
  new_byte_array(default_script, default_code);
  read_property(default_script, default_code, "endian");
  default_code.returnvalue();
  const ExecutionResult defaulted = run_program(default_runtime, default_script, default_code, 2);
  ASSERT_TRUE(defaulted.ok()) << defaulted.message;
  ASSERT_TRUE(defaulted.value.is_string());
  EXPECT_EQ(defaulted.value.as_string(), "bigEndian");

  Runtime set_runtime;
  avm2_test::ScriptBuilder set_script;
  avm2_test::CodeBuilder set_code;
  new_byte_array(set_script, set_code);
  set_code.getlocal(1);
  set_code.pushstring(set_script.string_of("littleEndian"));
  set_code.setproperty(set_script.multiname("endian"));
  read_property(set_script, set_code, "endian");
  set_code.returnvalue();
  const ExecutionResult set = run_program(set_runtime, set_script, set_code, 2);
  ASSERT_TRUE(set.ok()) << set.message;
  ASSERT_TRUE(set.value.is_string());
  EXPECT_EQ(set.value.as_string(), "littleEndian");

  Runtime constant_runtime;
  avm2_test::ScriptBuilder constant_script;
  avm2_test::CodeBuilder constant_code;
  constant_code.getlex(constant_script.multiname("Endian", "flash.utils"));
  constant_code.getproperty(constant_script.multiname("LITTLE_ENDIAN"));
  constant_code.returnvalue();
  const ExecutionResult constant = run_program(constant_runtime, constant_script, constant_code, 1);
  ASSERT_TRUE(constant.ok()) << constant.message;
  ASSERT_TRUE(constant.value.is_string());
  EXPECT_EQ(constant.value.as_string(), "littleEndian");

  Runtime invalid_runtime;
  avm2_test::ScriptBuilder invalid_script;
  avm2_test::CodeBuilder invalid_code;
  new_byte_array(invalid_script, invalid_code);
  invalid_code.getlocal(1);
  invalid_code.pushstring(invalid_script.string_of("middleEndian"));
  invalid_code.setproperty(invalid_script.multiname("endian"));
  invalid_code.returnvalue();
  const ExecutionResult invalid = run_program(invalid_runtime, invalid_script, invalid_code, 2);
  EXPECT_EQ(invalid.status, InterpreterStatus::TypeError);
}

TEST(Avm2ByteArray, BytesAvailableCountsFromPositionToTheEnd) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  for (std::int32_t value = 1; value <= 5; ++value) {
    write_byte(script, code, value);
  }
  set_position(script, code, 2);
  read_property(script, code, "bytesAvailable");
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 3.0);
}

TEST(Avm2ByteArray, ClearEmptiesTheArrayAndResetsThePosition) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  write_byte(script, code, 1);
  write_byte(script, code, 2);
  write_byte(script, code, 3);
  code.getlocal(1);
  code.callproperty(script.multiname("clear"), 0);
  code.pop();
  read_property(script, code, "length");
  read_property(script, code, "position");
  code.add();
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 0.0);
}

TEST(Avm2ByteArray, Base64DecoderShapedLoopWritesIndexedBytesAndReadsThemBack) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  code.pushbyte(0);
  code.setlocal(2);
  code.jump("write_check");
  code.label("write_body");
  code.getlocal(1);
  code.getlocal(2);
  code.getlocal(2);
  code.pushbyte(3);
  code.multiply();
  code.setproperty(script.multiname_l());
  code.getlocal(2);
  code.increment();
  code.setlocal(2);
  code.label("write_check");
  code.getlocal(2);
  code.pushbyte(16);
  code.iflt("write_body");
  set_position(script, code, 0);
  code.pushbyte(0);
  code.setlocal(3);
  code.label("read_check");
  code.getlocal(1);
  code.getproperty(script.multiname("bytesAvailable"));
  code.pushbyte(0);
  code.greaterthan();
  code.iffalse("read_done");
  code.getlocal(3);
  code.getlocal(1);
  code.callproperty(script.multiname("readUnsignedByte"), 0);
  code.add();
  code.setlocal(3);
  code.jump("read_check");
  code.label("read_done");
  code.getlocal(3);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 360.0);
}

TEST(Avm2ByteArray, IndexLoopOverLengthSumsEveryByte) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t runtime_name = script.multiname_l();
  new_byte_array(script, code);
  for (std::int32_t value = 1; value <= 5; ++value) {
    write_byte(script, code, value);
  }
  code.pushbyte(0);
  code.setlocal(2);
  code.pushbyte(0);
  code.setlocal(3);
  code.jump("check");
  code.label("body");
  code.getlocal(3);
  code.getlocal(1);
  code.getlocal(2);
  code.getproperty(runtime_name);
  code.add();
  code.setlocal(3);
  code.getlocal(2);
  code.increment();
  code.setlocal(2);
  code.label("check");
  code.getlocal(2);
  code.getlocal(1);
  code.getproperty(script.multiname("length"));
  code.iflt("body");
  code.getlocal(3);
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 4);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 15.0);
}

TEST(Avm2ByteArray, SettingLengthAboveTheCapIsATypeError) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  code.getlocal(1);
  code.pushdouble(script.add_double(4294967295.0));
  code.setproperty(script.multiname("length"));
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError);
}

TEST(Avm2ByteArray, ReadUtfBytesClampsToBytesAvailable) {
  Runtime text_runtime;
  avm2_test::ScriptBuilder text_script;
  avm2_test::CodeBuilder text_code;
  new_byte_array(text_script, text_code);
  text_code.getlocal(1);
  text_code.pushstring(text_script.string_of("abcde"));
  text_code.callproperty(text_script.multiname("writeUTFBytes"), 1);
  text_code.pop();
  set_position(text_script, text_code, 0);
  text_code.getlocal(1);
  text_code.pushbyte(100);
  text_code.callproperty(text_script.multiname("readUTFBytes"), 1);
  text_code.returnvalue();
  const ExecutionResult text = run_program(text_runtime, text_script, text_code, 2);
  ASSERT_TRUE(text.ok()) << text.message;
  ASSERT_TRUE(text.value.is_string());
  EXPECT_EQ(text.value.as_string(), "abcde");

  Runtime position_runtime;
  avm2_test::ScriptBuilder position_script;
  avm2_test::CodeBuilder position_code;
  new_byte_array(position_script, position_code);
  position_code.getlocal(1);
  position_code.pushstring(position_script.string_of("abcde"));
  position_code.callproperty(position_script.multiname("writeUTFBytes"), 1);
  position_code.pop();
  set_position(position_script, position_code, 0);
  position_code.getlocal(1);
  position_code.pushbyte(100);
  position_code.callproperty(position_script.multiname("readUTFBytes"), 1);
  position_code.pop();
  read_property(position_script, position_code, "position");
  position_code.returnvalue();
  const ExecutionResult position = run_program(position_runtime, position_script, position_code, 2);
  ASSERT_TRUE(position.ok()) << position.message;
  ASSERT_TRUE(position.value.is_number_like());
  EXPECT_EQ(position.value.as_number(), 5.0);
}

TEST(Avm2ByteArray, IndexWriteBeyondTheCapIsDropped) {
  Runtime runtime;
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  new_byte_array(script, code);
  set_index(script, code, 300000000, 1);
  read_property(script, code, "length");
  code.returnvalue();
  const ExecutionResult result = run_program(runtime, script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 0.0);
}

}  // namespace
