#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/text.hpp>
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
using fo4::avm2::Value;

const std::string kSmile = "\xF0\x9F\x98\x80";
const std::string kLoneHigh = "\xED\xA0\xBD";
const std::string kLoneLow = "\xED\xB8\x80";

ExecutionResult run_initializer(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  const std::uint32_t method = script.method("init", code.bytes(), 1);
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

TEST(Avm2StringUtf16, ConvertsNonBmpTextToSurrogatePairsAndBack) {
  EXPECT_EQ(fo4::avm2::utf16_from_utf8(kSmile), u"\xD83D\xDE00");
  EXPECT_EQ(fo4::avm2::utf8_from_utf16(u"\xD83D\xDE00"), kSmile);
  EXPECT_EQ(fo4::avm2::utf8_from_utf16(u"\xD83D"), kLoneHigh);
  EXPECT_EQ(fo4::avm2::utf16_from_utf8(kLoneLow), u"\xDE00");
}

TEST(Avm2StringUtf16, ValueStringMergesAdjacentSurrogateHalves) {
  EXPECT_EQ(Value::string(kLoneHigh + kLoneLow).as_string(), kSmile);
  EXPECT_EQ(Value::string(kLoneHigh).as_string(), kLoneHigh);
  EXPECT_EQ(Value::string(std::string("\xED\x9F\xBF") + "A").as_string(),
            std::string("\xED\x9F\xBF") + "A");
}

TEST(Avm2StringUtf16, LengthCountsCodeUnitsOfNonBmpText) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile));
  code.getproperty(length);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 3.0);
}

TEST(Avm2StringUtf16, IndexedAccessReadsOneCodeUnit) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t index = script.multiname("2");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile));
  code.getproperty(index);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kLoneLow);
}

TEST(Avm2StringUtf16, CharCodeAtReturnsSurrogateCodeUnits) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t char_code_at = script.multiname("charCodeAt");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile));
  code.pushint(script.add_int(2));
  code.callproperty(char_code_at, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_DOUBLE_EQ(result.value.as_number(), 56832.0);
}

TEST(Avm2StringUtf16, CharAtReturnsOneCodeUnit) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t char_at = script.multiname("charAt");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile));
  code.pushbyte(1);
  code.callproperty(char_at, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kLoneHigh);
}

TEST(Avm2StringUtf16, IndexOfCountsCodeUnits) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t index_of = script.multiname("indexOf");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushstring(script.string_of("b"));
  code.callproperty(index_of, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 3);
}

TEST(Avm2StringUtf16, SubstringUsesCodeUnitBounds) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t substring = script.multiname("substring");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushbyte(1);
  code.pushbyte(3);
  code.callproperty(substring, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kSmile);
}

TEST(Avm2StringUtf16, FromCharCodeCombinesSurrogatePair) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_class = script.multiname("String");
  const std::uint32_t from_char_code = script.multiname("fromCharCode");
  avm2_test::CodeBuilder code;
  code.getlex(string_class);
  code.pushint(script.add_int(0xD83D));
  code.pushint(script.add_int(0xDE00));
  code.callproperty(from_char_code, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kSmile);
}

TEST(Avm2StringUtf16, FromCharCodeKeepsAnUnpairedSurrogate) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_class = script.multiname("String");
  const std::uint32_t from_char_code = script.multiname("fromCharCode");
  avm2_test::CodeBuilder code;
  code.getlex(string_class);
  code.pushint(script.add_int(0xD83D));
  code.callproperty(from_char_code, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kLoneHigh);
}

TEST(Avm2StringUtf16, IndexOfClampsANegativeStartToZero) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t index_of = script.multiname("indexOf");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushstring(script.string_of(kSmile));
  code.pushint(script.add_int(-1));
  code.callproperty(index_of, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 1);
}

TEST(Avm2StringUtf16, IndexOfDoesNotMatchAPairStartingInsideIt) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t index_of = script.multiname("indexOf");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushstring(script.string_of(kSmile));
  code.pushbyte(2);
  code.callproperty(index_of, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), -1);
}

TEST(Avm2StringUtf16, LastIndexOfSearchesBackFromTheStartCodeUnit) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t last_index_of = script.multiname("lastIndexOf");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b" + kSmile + "b"));
  code.pushstring(script.string_of("b"));
  code.pushbyte(5);
  code.callproperty(last_index_of, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 3);
}

TEST(Avm2StringUtf16, LastIndexOfFindsAPairThatStartsAtOrBeforeTheStart) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t last_index_of = script.multiname("lastIndexOf");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b" + kSmile + "b"));
  code.pushstring(script.string_of(kSmile));
  code.pushbyte(2);
  code.callproperty(last_index_of, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 1);
}

TEST(Avm2StringUtf16, SubstrCountsCodeUnitsFromANegativeStart) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t substr = script.multiname("substr");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushbyte(1);
  code.pushbyte(2);
  code.callproperty(substr, 2);
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushint(script.add_int(-1));
  code.callproperty(substr, 1);
  code.add();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kSmile + "b");
}

TEST(Avm2StringUtf16, SliceAcceptsNegativeBoundsInCodeUnits) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t slice = script.multiname("slice");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushbyte(1);
  code.pushint(script.add_int(-1));
  code.callproperty(slice, 2);
  code.pushstring(script.string_of("a" + kSmile + "b"));
  code.pushint(script.add_int(-1));
  code.callproperty(slice, 1);
  code.add();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kSmile + "b");
}

TEST(Avm2StringUtf16, SplitByAPairSeparatorKeepsThePairWhole) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t split = script.multiname("split");
  const std::uint32_t join = script.multiname("join");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("a" + kSmile + "b" + kSmile + "c"));
  code.pushstring(script.string_of(kSmile));
  code.callproperty(split, 1);
  code.pushstring(script.string_of("|"));
  code.callproperty(join, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "a|b|c");
}

TEST(Avm2StringUtf16, SplitWithAnEmptySeparatorSplitsAPairIntoItsHalves) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t split = script.multiname("split");
  const std::uint32_t join = script.multiname("join");
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of(kSmile));
  code.pushstring(script.string_of(""));
  code.callproperty(split, 1);
  code.pushstring(script.string_of("|"));
  code.callproperty(join, 1);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kLoneHigh + "|" + kLoneLow);
}

TEST(Avm2StringUtf16, FromCharCodeKeepsOnlyTheLowSixteenBits) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_class = script.multiname("String");
  const std::uint32_t from_char_code = script.multiname("fromCharCode");
  avm2_test::CodeBuilder code;
  code.getlex(string_class);
  code.pushint(script.add_int(0x1D83D));
  code.pushint(script.add_int(0xDE00));
  code.callproperty(from_char_code, 2);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), kSmile);
}

TEST(Avm2StringUtf16, FromCharCodeWithNoArgumentsIsEmpty) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_class = script.multiname("String");
  const std::uint32_t from_char_code = script.multiname("fromCharCode");
  avm2_test::CodeBuilder code;
  code.getlex(string_class);
  code.callproperty(from_char_code, 0);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "");
}

TEST(Avm2StringUtf16, FromCharCodeJoinsAHighHalfWithALaterLowHalf) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t string_class = script.multiname("String");
  const std::uint32_t from_char_code = script.multiname("fromCharCode");
  avm2_test::CodeBuilder code;
  code.getlex(string_class);
  code.pushint(script.add_int(0x61));
  code.pushint(script.add_int(0xD83D));
  code.pushint(script.add_int(0xDE00));
  code.callproperty(from_char_code, 3);
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "a" + kSmile);
}

TEST(Avm2StringUtf16, ConcatenatedSurrogateHalvesEqualTheCharacter) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t char_at = script.multiname("charAt");
  avm2_test::CodeBuilder code;
  const std::uint32_t text = script.string_of(kSmile);
  code.pushstring(text);
  code.pushbyte(0);
  code.callproperty(char_at, 1);
  code.pushstring(text);
  code.pushbyte(1);
  code.callproperty(char_at, 1);
  code.add();
  code.pushstring(text);
  code.strictequals();
  code.returnvalue();
  const ExecutionResult result = run_initializer(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(fo4::avm2::to_boolean(result.value));
}

}  // namespace
