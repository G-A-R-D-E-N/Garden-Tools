#include <fo4/abc/abc.hpp>
#include <fo4/abc/decompile.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "abc_decompile_fixtures.hpp"

namespace {

using fo4::abc::AbcDecompileIssue;
using fo4::abc::AbcDecompileStatus;
using fo4::abc::AbcException;
using fo4::abc::AbcFile;
using fo4::abc::AbcFunction;
using fo4::test::CodeBuilder;
using fo4::test::decompile_fixture_file;
using fo4::test::make_exception;

constexpr std::uint8_t kIfEq = 0x13;
constexpr std::uint8_t kIfLt = 0x15;
constexpr std::uint8_t kIfNlt = 0x0C;
constexpr std::uint8_t kPushWith = 0x1C;
constexpr std::uint8_t kNextName = 0x1E;
constexpr std::uint8_t kPushString = 0x2C;
constexpr std::uint8_t kPushInt = 0x2D;
constexpr std::uint8_t kPushUInt = 0x2E;
constexpr std::uint8_t kPushDouble = 0x2F;
constexpr std::uint8_t kNewFunction = 0x40;
constexpr std::uint8_t kConstruct = 0x42;
constexpr std::uint8_t kCallStatic = 0x44;
constexpr std::uint8_t kCallProperty = 0x46;
constexpr std::uint8_t kConstructProp = 0x4A;
constexpr std::uint8_t kCallPropVoid = 0x4F;
constexpr std::uint8_t kApplyType = 0x53;
constexpr std::uint8_t kNewObject = 0x55;
constexpr std::uint8_t kNewArray = 0x56;
constexpr std::uint8_t kNewActivation = 0x57;
constexpr std::uint8_t kGetDescendants = 0x59;
constexpr std::uint8_t kFindPropStrict = 0x5D;
constexpr std::uint8_t kSetProperty = 0x61;
constexpr std::uint8_t kGetScopeObject = 0x65;
constexpr std::uint8_t kGetProperty = 0x66;
constexpr std::uint8_t kGetSlot = 0x6C;
constexpr std::uint8_t kConvertI = 0x73;
constexpr std::uint8_t kCoerce = 0x80;
constexpr std::uint8_t kCoerceA = 0x82;
constexpr std::uint8_t kIncrement = 0x91;
constexpr std::uint8_t kAdd = 0xA0;
constexpr std::uint8_t kSubtract = 0xA1;
constexpr std::uint8_t kMultiply = 0xA2;
constexpr std::uint8_t kAddI = 0xC5;

constexpr std::uint32_t kAttributeName = 15;

struct Decompiled {
  AbcDecompileStatus status{AbcDecompileStatus::Ok};
  AbcDecompileIssue issue;
  AbcFunction function;
  std::string text;
};

Decompiled run(const AbcFile& file, std::uint32_t body = 0) {
  Decompiled result;
  result.status = fo4::abc::decompile_body(file, body, result.function, result.issue);
  result.text = fo4::abc::dump_function(result.function);
  return result;
}

Decompiled decompile(const CodeBuilder& code, const std::vector<AbcException>& exceptions = {}) {
  return run(decompile_fixture_file(code.bytes(), exceptions));
}

void expect_text(const CodeBuilder& code, const std::string& expected,
                 const std::vector<AbcException>& exceptions = {}) {
  const Decompiled result = decompile(code, exceptions);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, expected);
}

Decompiled expect_refusal(const CodeBuilder& code, AbcDecompileStatus status) {
  Decompiled result = decompile(code);
  EXPECT_EQ(result.status, status) << result.issue.message;
  EXPECT_EQ(result.issue.status, status);
  EXPECT_FALSE(result.issue.message.empty());
  EXPECT_TRUE(result.function.statements.empty());
  EXPECT_TRUE(result.text.empty());
  return result;
}

CodeBuilder& catch_prologue(CodeBuilder& code) {
  return code.op(fo4::test::kOpGetLocal0)
      .op(fo4::test::kOpPushScope)
      .op_u30(fo4::test::kOpNewCatch, 0)
      .op(fo4::test::kOpDup)
      .op_u30(fo4::test::kOpSetLocal, 5)
      .op(fo4::test::kOpDup)
      .op(fo4::test::kOpPushScope)
      .op(fo4::test::kOpSwap)
      .op_u30(fo4::test::kOpSetSlot, 1);
}

}  // namespace

TEST(AbcDecompileExpr, RecoversAMemberCallStatementAndDropsTheTrailingReturn) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameTest)
      .op_u30(kPushString, fo4::test::kStringClick)
      .op_u30_u30(kCallPropVoid, fo4::test::kMultinameClick, 1)
      .op(fo4::test::kOpReturnVoid);
  expect_text(code, "Test.click(\"click\");\n");
}

TEST(AbcDecompileExpr, WritesANamespacedNameByValue) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameExternalInterface)
      .op_u30(kPushString, fo4::test::kStringFoo)
      .op_u30_u30(kCallPropVoid, fo4::test::kMultinameCall, 1)
      .op(fo4::test::kOpReturnVoid);
  expect_text(code,
              "flash.external::ExternalInterface.flash.external::call(\"Foo\");\n");
}

TEST(AbcDecompileExpr, TurnsFindpropstrictIntoABareCall) {
  CodeBuilder code;
  code.op_u30(kFindPropStrict, fo4::test::kMultinameCall)
      .op_u30(kPushString, fo4::test::kStringFoo)
      .op_u30_u30(kCallProperty, fo4::test::kMultinameCall, 1)
      .op(fo4::test::kOpReturnValue);
  expect_text(code, "return flash.external::call(\"Foo\");\n");

  CodeBuilder construct;
  construct.op_u30(kFindPropStrict, fo4::test::kMultinameTest)
      .op_u30_u30(kConstructProp, fo4::test::kMultinameTest, 0)
      .op(fo4::test::kOpReturnValue);
  expect_text(construct, "return new Test();\n");
}

TEST(AbcDecompileExpr, ParenthesisesByPrecedenceAndRightOperandAssociativity) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .pushbyte(2)
      .op(kAdd)
      .pushbyte(3)
      .op(kMultiply)
      .op(fo4::test::kOpReturnValue);
  expect_text(code, "var local1;\nreturn (local1 + 2) * 3;\n");

  CodeBuilder right;
  right.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpGetLocal2)
      .op(fo4::test::kOpGetLocal3)
      .op(kSubtract)
      .op(kSubtract)
      .op(fo4::test::kOpReturnValue);
  expect_text(right, "var local1;\nvar local2;\nvar local3;\nreturn local1 - (local2 - local3);\n");
}

TEST(AbcDecompileExpr, AssignsLocalsAndKeepsAReadOfTheOldValue) {
  CodeBuilder code;
  code.pushbyte(5)
      .op_u30(fo4::test::kOpSetLocal, 1)
      .op(fo4::test::kOpGetLocal1)
      .pushbyte(1)
      .op(kAdd)
      .op_u30(fo4::test::kOpSetLocal, 1)
      .op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpReturnValue);
  expect_text(code, "var local1;\nlocal1 = 5;\nlocal1 = local1 + 1;\nreturn local1;\n");

  CodeBuilder spill;
  spill.op(fo4::test::kOpGetLocal1)
      .pushbyte(9)
      .op_u30(fo4::test::kOpSetLocal, 1)
      .op(fo4::test::kOpReturnValue);
  expect_text(spill, "var local1;\nvar _t0;\n_t0 = local1;\nlocal1 = 9;\nreturn _t0;\n");
}

TEST(AbcDecompileExpr, TurnsDupThenSetlocalIntoAnAssignmentExpression) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .pushbyte(1)
      .op(kAdd)
      .op(fo4::test::kOpDup)
      .op(fo4::test::kOpSetLocal3)
      .op(fo4::test::kOpReturnValue);
  expect_text(code, "var local1;\nvar local3;\nreturn local3 = local1 + 1;\n");
}

TEST(AbcDecompileExpr, WritesPropertyReadsAndWrites) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpGetLocal1)
      .op_u30(kGetProperty, fo4::test::kMultinameCounter)
      .pushbyte(1)
      .op(kAdd)
      .op_u30(kSetProperty, fo4::test::kMultinameCounter)
      .op(fo4::test::kOpReturnVoid);
  expect_text(code, "var local1;\nlocal1.counter = local1.counter + 1;\n");
}

TEST(AbcDecompileExpr, WritesPoolLiteralsArraysAndObjectLiterals) {
  CodeBuilder array;
  array.op_u30(kPushString, fo4::test::kStringFoo)
      .op_u30(kPushInt, 1)
      .op_u30(kPushDouble, 1)
      .op_u30(kNewArray, 3)
      .op(fo4::test::kOpReturnValue);
  expect_text(array, "return [\"Foo\", 7, 1.5];\n");

  CodeBuilder object;
  object.op_u30(kPushString, fo4::test::kStringFoo)
      .op_u30(kPushUInt, 1)
      .op_u30(kNewObject, 1)
      .op(fo4::test::kOpReturnValue);
  expect_text(object, "return {Foo: 9};\n");
}

TEST(AbcDecompileExpr, WritesCastsAndTreatsCoerceAnyAsTransparent) {
  CodeBuilder cast;
  cast.op(fo4::test::kOpGetLocal1)
      .op(kConvertI)
      .op(kConvertI)
      .op(fo4::test::kOpReturnValue);
  expect_text(cast, "var local1;\nreturn int(local1);\n");

  CodeBuilder typed;
  typed.op(fo4::test::kOpGetLocal1)
      .op_u30(kCoerce, fo4::test::kMultinameTest)
      .op(fo4::test::kOpReturnValue);
  expect_text(typed, "var local1;\nreturn Test(local1);\n");

  CodeBuilder any;
  any.op(fo4::test::kOpGetLocal1).op(kCoerceA).op(fo4::test::kOpReturnValue);
  expect_text(any, "var local1;\nreturn local1;\n");
}

TEST(AbcDecompileExpr, WritesIncrementAsAnAdditionThatKeepsTheNumericCoercion) {
  CodeBuilder plain;
  plain.op(fo4::test::kOpGetLocal1).op(kIncrement).op(fo4::test::kOpReturnValue);
  expect_text(plain, "var local1;\nreturn Number(local1) + 1;\n");

  CodeBuilder numeric;
  numeric.op(fo4::test::kOpGetLocal1).op(kConvertI).op(kIncrement).op(fo4::test::kOpReturnValue);
  expect_text(numeric, "var local1;\nreturn int(local1) + 1;\n");
}

TEST(AbcDecompileExpr, WritesAVectorTypeApplication) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameObject)
      .op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameTest)
      .op_u30(kApplyType, 1)
      .op_u30(kConstruct, 0)
      .op(fo4::test::kOpReturnValue);
  expect_text(code, "return new Object.<Test>();\n");
}

TEST(AbcDecompileExpr, WritesE4xDescendantsAndAttributes) {
  AbcFile file = decompile_fixture_file(
      CodeBuilder{}
          .op(fo4::test::kOpGetLocal1)
          .op_u30(kGetDescendants, fo4::test::kMultinameClick)
          .op(fo4::test::kOpReturnValue)
          .bytes());
  fo4::abc::AbcMultiname attribute;
  attribute.kind = fo4::abc::AbcMultinameKind::QNameA;
  attribute.name = fo4::test::kStringClick;
  attribute.ns = 2;
  file.pool.multinames.push_back(attribute);
  const Decompiled descendants = run(file);
  ASSERT_EQ(descendants.status, AbcDecompileStatus::Ok) << descendants.issue.message;
  EXPECT_EQ(descendants.text, "var local1;\nreturn local1..click;\n");

  AbcFile member = decompile_fixture_file(CodeBuilder{}
                                              .op(fo4::test::kOpGetLocal1)
                                              .op_u30(kGetProperty, kAttributeName)
                                              .op(fo4::test::kOpReturnValue)
                                              .bytes());
  member.pool.multinames.push_back(attribute);
  const Decompiled read = run(member);
  ASSERT_EQ(read.status, AbcDecompileStatus::Ok) << read.issue.message;
  EXPECT_EQ(read.text, "var local1;\nreturn local1.@click;\n");

  AbcFile query = decompile_fixture_file(CodeBuilder{}
                                             .op(fo4::test::kOpGetLocal1)
                                             .op_u30(kGetDescendants, kAttributeName)
                                             .op(fo4::test::kOpReturnValue)
                                             .bytes());
  query.pool.multinames.push_back(attribute);
  const Decompiled all = run(query);
  ASSERT_EQ(all.status, AbcDecompileStatus::Ok) << all.issue.message;
  EXPECT_EQ(all.text, "var local1;\nreturn local1..@click;\n");
}

TEST(AbcDecompileExpr, RecoversAnIfAndAnIfElse) {
  CodeBuilder plain;
  plain.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(plain, "var local1;\nif (local1) {\n  1;\n}\n");

  CodeBuilder both;
  both.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .stmt(1)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .stmt(2)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(both, "var local1;\nif (local1) {\n  1;\n} else {\n  2;\n}\n");
}

TEST(AbcDecompileExpr, WritesAndOrChainsAsOneCondition) {
  CodeBuilder both;
  both.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "END")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(both, "var local1;\nvar local2;\nif (local1 && local2) {\n  1;\n}\n");

  CodeBuilder either;
  either.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "BODY")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "END")
      .label("BODY")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(either, "var local1;\nvar local2;\nif (local1 || local2) {\n  1;\n}\n");
}

TEST(AbcDecompileExpr, NeverFlipsARelationalOperatorWhenNegatingACondition) {
  CodeBuilder jump_if_less;
  jump_if_less.op(fo4::test::kOpGetLocal1)
      .pushbyte(3)
      .branch(kIfLt, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(jump_if_less, "var local1;\nif (!(local1 < 3)) {\n  1;\n}\n");

  CodeBuilder jump_if_not_less;
  jump_if_not_less.op(fo4::test::kOpGetLocal1)
      .pushbyte(3)
      .branch(kIfNlt, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(jump_if_not_less, "var local1;\nif (local1 < 3) {\n  1;\n}\n");

  CodeBuilder equal;
  equal.op(fo4::test::kOpGetLocal1)
      .pushbyte(3)
      .branch(kIfEq, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(equal, "var local1;\nif (local1 != 3) {\n  1;\n}\n");
}

TEST(AbcDecompileExpr, RecoversWhileDoWhileAndInfiniteLoops) {
  CodeBuilder loop;
  loop.branch(fo4::test::kOpJump, "COND")
      .label("LOOP")
      .op(fo4::test::kOpLabel)
      .stmt(1)
      .label("COND")
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  expect_text(loop, "var local1;\nwhile (local1) {\n  1;\n}\n");

  CodeBuilder tail;
  tail.label("LOOP")
      .op(fo4::test::kOpLabel)
      .stmt(1)
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  expect_text(tail, "var local1;\ndo {\n  1;\n} while (local1);\n");

  CodeBuilder forever;
  forever.label("LOOP")
      .op(fo4::test::kOpLabel)
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "CONT")
      .stmt(1)
      .branch(fo4::test::kOpJump, "OUT")
      .label("CONT")
      .stmt(2)
      .branch(fo4::test::kOpJump, "LOOP")
      .label("OUT")
      .op(fo4::test::kOpReturnVoid);
  expect_text(forever,
              "var local1;\nwhile (true) {\n  if (!local1) {\n    2;\n    continue;\n  }\n  1;\n  break;\n}\n");
}

TEST(AbcDecompileExpr, MovesAStatementPrefixOfAWhileConditionIntoTheLoopBody) {
  CodeBuilder code;
  code.branch(fo4::test::kOpJump, "COND")
      .label("LOOP")
      .op(fo4::test::kOpLabel)
      .stmt(1)
      .label("COND")
      .stmt(5)
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  expect_text(code,
              "var local1;\nwhile (true) {\n  5;\n  if (!local1) {\n    break;\n  }\n  1;\n}\n");
}

TEST(AbcDecompileExpr, GroupsSwitchCaseLabelsByLookupIndex) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .lookupswitch("DEF", {"C0", "C1"})
      .label("C0")
      .stmt(1)
      .branch(fo4::test::kOpJump, "END")
      .label("C1")
      .stmt(2)
      .branch(fo4::test::kOpJump, "END")
      .label("DEF")
      .stmt(3)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(code,
              "var local1;\nswitch (local1) {\n  case 0:\n    1;\n    break;\n  case 1:\n    2;\n    "
              "break;\n  default:\n    3;\n}\n");

  CodeBuilder shared;
  shared.op(fo4::test::kOpGetLocal1)
      .lookupswitch("C1", {"C0", "C1", "C1"})
      .label("C0")
      .stmt(1)
      .branch(fo4::test::kOpJump, "END")
      .label("C1")
      .stmt(2)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  expect_text(shared,
              "var local1;\nswitch (local1) {\n  case 0:\n    1;\n    break;\n  case 1:\n  case 2:\n  "
              "default:\n    2;\n}\n");
}

TEST(AbcDecompileExpr, WritesATryCatchWithTheCatchPrologueConsumed) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameTest)
      .op(fo4::test::kOpPop)
      .branch(fo4::test::kOpJump, "AFTER")
      .label("HANDLER");
  catch_prologue(code)
      .stmt(1)
      .op(fo4::test::kOpPopScope)
      .op(fo4::test::kOpPopScope)
      .label("AFTER")
      .op(fo4::test::kOpReturnVoid);
  const std::size_t handler = code.offset_of("HANDLER");
  expect_text(code, "try {\n  Test;\n} catch (helper:*) {\n  1;\n}\n",
              {make_exception(0, handler, handler)});
}

TEST(AbcDecompileExpr, WritesAWithBlockUntilItsPopscope) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameTest)
      .op(kPushWith)
      .stmt(1)
      .op(fo4::test::kOpPopScope)
      .stmt(2)
      .op(fo4::test::kOpReturnVoid);
  expect_text(code, "with (Test) {\n  1;\n}\n2;\n");
}

TEST(AbcDecompileExpr, WritesShortCircuitAndTernaryAsValues) {
  CodeBuilder both;
  both.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpDup)
      .branch(fo4::test::kOpIfFalse, "J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpGetLocal2)
      .label("J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_text(both, "var local1;\nvar local2;\nlocal1 && local2;\n");

  CodeBuilder either;
  either.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpDup)
      .branch(fo4::test::kOpIfTrue, "J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpGetLocal2)
      .label("J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_text(either, "var local1;\nvar local2;\nlocal1 || local2;\n");

  CodeBuilder ternary;
  ternary.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .pushbyte(1)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .pushbyte(2)
      .label("END")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_text(ternary, "var local1;\nlocal1 ? 1 : 2;\n");
}

TEST(AbcDecompileExpr, MapsActivationSlotsToTheBodyTraitNames) {
  AbcFile file = decompile_fixture_file(
      CodeBuilder{}
          .op(fo4::test::kOpGetLocal0)
          .op(fo4::test::kOpPushScope)
          .op(kNewActivation)
          .op(fo4::test::kOpDup)
          .op_u30(fo4::test::kOpSetLocal, 3)
          .op(fo4::test::kOpPushScope)
          .op_u30(fo4::test::kOpGetLocal, 3)
          .pushbyte(5)
          .op_u30(fo4::test::kOpSetSlot, 1)
          .op_u30(kGetScopeObject, 1)
          .op_u30(kGetSlot, 1)
          .op(fo4::test::kOpReturnValue)
          .bytes());
  fo4::abc::AbcTrait trait;
  trait.name = fo4::test::kMultinameCounter;
  trait.kind = fo4::abc::AbcTraitKind::Slot;
  trait.slot_id = 1;
  file.bodies[0].traits.push_back(trait);
  file.methods[0].flags = static_cast<std::uint8_t>(file.methods[0].flags | 0x02u);
  const Decompiled result = run(file);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "var counter;\ncounter = 5;\nreturn counter;\n");
}

TEST(AbcDecompileExpr, NamesParametersFromTheMethodInfo) {
  AbcFile file = decompile_fixture_file(CodeBuilder{}.op(fo4::test::kOpReturnVoid).bytes());
  ASSERT_GT(file.bodies.size(), 1u);
  file.bodies[1].code =
      CodeBuilder{}.op(fo4::test::kOpGetLocal1).op(fo4::test::kOpReturnValue).bytes();
  const Decompiled result = run(file, 1);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "return click;\n");
  ASSERT_EQ(result.function.param_names.size(), 1u);
  EXPECT_EQ(result.function.param_names[0], "click");
  EXPECT_EQ(result.function.body, 1u);
  EXPECT_EQ(result.function.method, 1u);
}

TEST(AbcDecompileExpr, ProducesTheSameTextOnEveryRun) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameTest)
      .op(fo4::test::kOpPop)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .stmt(2)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Decompiled first = decompile(code);
  const Decompiled second = decompile(code);
  ASSERT_EQ(first.status, AbcDecompileStatus::Ok) << first.issue.message;
  EXPECT_EQ(first.text, second.text);
  EXPECT_FALSE(first.text.empty());
}

TEST(AbcDecompileExpr, RefusesNewfunctionAndNamesTheOffset) {
  CodeBuilder code;
  code.op_u30(kNewFunction, 1).op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid);
  const Decompiled result = expect_refusal(code, AbcDecompileStatus::UnsupportedOpcode);
  EXPECT_EQ(result.issue.offset, 0u);
  EXPECT_EQ(result.issue.body, 0u);
  EXPECT_EQ(result.issue.method, 0u);
}

TEST(AbcDecompileExpr, RefusesCallstaticAndForInIteration) {
  CodeBuilder call;
  call.op(fo4::test::kOpGetLocal1)
      .op_u30_u30(kCallStatic, 1, 0)
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_refusal(call, AbcDecompileStatus::UnsupportedOpcode);

  CodeBuilder iterate;
  iterate.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpGetLocal2)
      .op(kNextName)
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_refusal(iterate, AbcDecompileStatus::UnsupportedOpcode);
}

TEST(AbcDecompileExpr, RefusesIntegerArithmeticOpcodes) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpGetLocal2)
      .op(kAddI)
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_refusal(code, AbcDecompileStatus::UnsupportedOpcode);
}

TEST(AbcDecompileExpr, RefusesAStatementInsideAValueArm) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .pushbyte(1)
      .op(fo4::test::kOpDup)
      .op(fo4::test::kOpSetLocal3)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .pushbyte(2)
      .label("END")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_refusal(code, AbcDecompileStatus::UnsupportedValueRegion);
}

TEST(AbcDecompileExpr, RefusesAScopeObjectUsedAsAValue) {
  AbcFile file =
      decompile_fixture_file(CodeBuilder{}.op(kNewActivation).op(fo4::test::kOpReturnValue).bytes());
  file.methods[0].flags = static_cast<std::uint8_t>(file.methods[0].flags | 0x02u);
  const Decompiled result = run(file);
  EXPECT_EQ(result.status, AbcDecompileStatus::UnsupportedExpression) << result.issue.message;
  EXPECT_FALSE(result.issue.message.empty());
  EXPECT_TRUE(result.function.statements.empty());
}

TEST(AbcDecompileExpr, WritesWholeNumberDoublesWithoutAnExponent) {
  struct Row {
    double value;
    const char* text;
  };
  const Row rows[] = {{10.0, "return 10;\n"},
                      {1500000.0, "return 1500000;\n"},
                      {0.0, "return 0;\n"},
                      {-4.0, "return -4;\n"},
                      {1e15, "return 1e+15;\n"},
                      {2.5, "return 2.5;\n"}};
  for (const Row& row : rows) {
    AbcFile file = decompile_fixture_file(CodeBuilder{}
                                              .op_u30(kPushDouble, 1)
                                              .op(fo4::test::kOpReturnValue)
                                              .bytes());
    file.pool.doubles[1] = row.value;
    const Decompiled result = run(file);
    ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
    EXPECT_EQ(result.text, row.text);
  }
}

TEST(AbcDecompileExpr, WritesACatchHandlerThatOpensWithAnIf) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameTest)
      .op(fo4::test::kOpPop)
      .branch(fo4::test::kOpJump, "AFTER")
      .label("HANDLER");
  catch_prologue(code)
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "SKIP")
      .stmt(1)
      .label("SKIP")
      .op(fo4::test::kOpPopScope)
      .op(fo4::test::kOpPopScope)
      .label("AFTER")
      .op(fo4::test::kOpReturnVoid);
  const std::size_t handler = code.offset_of("HANDLER");
  expect_text(code,
              "var local1;\ntry {\n  Test;\n} catch (helper:*) {\n  if (local1) {\n    1;\n  }\n}\n",
              {make_exception(0, handler, handler)});
}

TEST(AbcDecompileExpr, RefusesASlotOnAnObjectThatIsNotAnActivation) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .op_u30(kGetSlot, 1)
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_refusal(code, AbcDecompileStatus::UnsupportedMember);
}

TEST(AbcDecompileExpr, RefusesAScopeLookupWithARuntimePart) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .op_u30(kFindPropStrict, fo4::test::kMultinameRuntimeName)
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  expect_refusal(code, AbcDecompileStatus::UnsupportedExpression);
}

TEST(AbcDecompileExpr, RefusesAMissingBodyAndLeavesTheOutputEmpty) {
  const AbcFile file = decompile_fixture_file(CodeBuilder{}.op(fo4::test::kOpReturnVoid).bytes());
  const Decompiled result = run(file, 99);
  EXPECT_EQ(result.status, AbcDecompileStatus::BodyNotFound);
  EXPECT_TRUE(result.function.statements.empty());
  EXPECT_FALSE(result.issue.message.empty());
}
