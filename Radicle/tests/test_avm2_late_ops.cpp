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

constexpr std::uint8_t kDxns = 0x06;
constexpr std::uint8_t kDxnslate = 0x07;
constexpr std::uint8_t kPushnamespace = 0x31;
constexpr std::uint8_t kFinddef = 0x5F;
constexpr std::uint8_t kSetpropertylate = 0x69;
constexpr std::uint8_t kDeletepropertylate = 0x6B;
constexpr std::uint8_t kGetglobalslot = 0x6E;
constexpr std::uint8_t kSetglobalslot = 0x6F;
constexpr std::uint8_t kEscXelem = 0x71;
constexpr std::uint8_t kEscXattr = 0x72;
constexpr std::uint8_t kCheckfilter = 0x78;

ExecutionResult run_code(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code) {
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  EXPECT_TRUE(runtime.add_file(file).ok());
  EXPECT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

ExecutionResult run_string_op(std::uint8_t opcode, const std::string& text) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of(text));
  code.op(opcode);
  code.returnvalue();
  return run_code(script, code);
}

TEST(Avm2LateOps, PushnamespaceReturnsTheUriOfTheConstantPoolEntry) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.op_u30(kPushnamespace, script.namespace_of("flash.display"));
  code.returnvalue();
  const ExecutionResult named = run_code(script, code);
  ASSERT_TRUE(named.ok()) << named.message;
  ASSERT_TRUE(named.value.is_string());
  EXPECT_EQ(named.value.as_string(), "flash.display");

  avm2_test::ScriptBuilder public_script;
  avm2_test::CodeBuilder public_code;
  public_code.op_u30(kPushnamespace, public_script.namespace_of(""));
  public_code.returnvalue();
  const ExecutionResult publicly = run_code(public_script, public_code);
  ASSERT_TRUE(publicly.ok()) << publicly.message;
  ASSERT_TRUE(publicly.value.is_string());
  EXPECT_EQ(publicly.value.as_string(), "");
}

TEST(Avm2LateOps, DxnsLeavesTheOperandStackAlone) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(9));
  code.op_u30(kDxns, script.string_of("http://example.com/xml"));
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 9);
}

TEST(Avm2LateOps, DxnslatePopsTheUriItIsGiven) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(9));
  code.pushstring(script.string_of("http://example.com/xml"));
  code.op(kDxnslate);
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 9);
}

TEST(Avm2LateOps, SetpropertylateStoresTheValueUnderTheNameFromTheStack) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.dup();
  code.pushstring(script.string_of("k"));
  code.pushint(script.add_int(42));
  code.op(kSetpropertylate);
  code.getproperty(script.multiname("k"));
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 42);
}

TEST(Avm2LateOps, SetpropertylateRefusesANullObject) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushnull();
  code.pushstring(script.string_of("k"));
  code.pushint(script.add_int(1));
  code.op(kSetpropertylate);
  code.returnvoid();
  const ExecutionResult result = run_code(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::NullReference);
  EXPECT_TRUE(result.message.starts_with("cannot write")) << result.message;
}

TEST(Avm2LateOps, DeletepropertylatePushesOnlyATrueBoolForAnExistingKey) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.dup();
  code.pushstring(script.string_of("k"));
  code.pushint(script.add_int(42));
  code.op(kSetpropertylate);
  code.dup();
  code.pushstring(script.string_of("k"));
  code.op(kDeletepropertylate);
  code.swap();
  code.pop();
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_boolean());
  EXPECT_TRUE(result.value.as_boolean());
}

TEST(Avm2LateOps, DeletepropertylateRemovesTheKeyNamedByTheStack) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.newobject(0);
  code.dup();
  code.pushstring(script.string_of("k"));
  code.pushint(script.add_int(42));
  code.op(kSetpropertylate);
  code.dup();
  code.pushstring(script.string_of("k"));
  code.op(kDeletepropertylate);
  code.pop();
  code.getproperty(script.multiname("k"));
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_undefined());
}

TEST(Avm2LateOps, GetglobalslotReadsTheGlobalObjectNotTheTopScope) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(7));
  code.op_u30(kSetglobalslot, 3);
  code.newobject(0);
  code.pushscope();
  code.op_u30(kGetglobalslot, 3);
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 7);
}

TEST(Avm2LateOps, GetglobalslotPastTheEndIsUndefined) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.op_u30(kGetglobalslot, 40);
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_TRUE(result.value.is_undefined());
}

TEST(Avm2LateOps, EscXelemEscapesAmpersandAndAngleBracketsButNotQuotes) {
  const ExecutionResult result = run_string_op(kEscXelem, "a&b<c>d\"e");
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "a&amp;b&lt;c&gt;d\"e");
}

TEST(Avm2LateOps, EscXattrAlsoEscapesDoubleQuotes) {
  const ExecutionResult result = run_string_op(kEscXattr, "a&b<c>d\"e");
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "a&amp;b&lt;c&gt;d&quot;e");
}

TEST(Avm2LateOps, EscapeConvertsANonStringWithToStringFirst) {
  avm2_test::ScriptBuilder number_script;
  avm2_test::CodeBuilder number_code;
  number_code.pushint(number_script.add_int(42));
  number_code.op(kEscXelem);
  number_code.returnvalue();
  const ExecutionResult number = run_code(number_script, number_code);
  ASSERT_TRUE(number.ok()) << number.message;
  ASSERT_TRUE(number.value.is_string());
  EXPECT_EQ(number.value.as_string(), "42");

  avm2_test::ScriptBuilder null_script;
  avm2_test::CodeBuilder null_code;
  null_code.pushnull();
  null_code.op(kEscXattr);
  null_code.returnvalue();
  const ExecutionResult nothing = run_code(null_script, null_code);
  ASSERT_TRUE(nothing.ok()) << nothing.message;
  ASSERT_TRUE(nothing.value.is_string());
  EXPECT_EQ(nothing.value.as_string(), "null");
}

TEST(Avm2LateOps, CheckfilterLeavesANonNullValueOnTheStack) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushint(script.add_int(5));
  code.op(kCheckfilter);
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 5);
}

TEST(Avm2LateOps, CheckfilterRefusesNullAndUndefined) {
  avm2_test::ScriptBuilder null_script;
  avm2_test::CodeBuilder null_code;
  null_code.pushnull();
  null_code.op(kCheckfilter);
  null_code.returnvoid();
  const ExecutionResult nothing = run_code(null_script, null_code);
  EXPECT_EQ(nothing.status, InterpreterStatus::TypeError);

  avm2_test::ScriptBuilder undefined_script;
  avm2_test::CodeBuilder undefined_code;
  undefined_code.pushundefined();
  undefined_code.op(kCheckfilter);
  undefined_code.returnvoid();
  const ExecutionResult undefined = run_code(undefined_script, undefined_code);
  EXPECT_EQ(undefined.status, InterpreterStatus::TypeError);
}

TEST(Avm2LateOps, FinddefPushesTheScopeThatHoldsTheName) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.pushstring(script.string_of("k"));
  code.pushint(script.add_int(1));
  code.newobject(1);
  code.pushscope();
  code.op_multiname(kFinddef, script.multiname("k"));
  code.getproperty(script.multiname("k"));
  code.returnvalue();
  const ExecutionResult result = run_code(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_int32());
  EXPECT_EQ(result.value.as_int32(), 1);
}

}  // namespace
