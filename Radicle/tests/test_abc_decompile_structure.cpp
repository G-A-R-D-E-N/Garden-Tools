#include <fo4/abc/abc.hpp>
#include <fo4/abc/decompile.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "abc_decompile_fixtures.hpp"

namespace {

using fo4::abc::AbcCondRef;
using fo4::abc::AbcCondStep;
using fo4::abc::AbcDecompileIssue;
using fo4::abc::AbcDecompileStatus;
using fo4::abc::AbcException;
using fo4::abc::AbcNodeKind;
using fo4::abc::AbcStructure;
using fo4::test::CodeBuilder;
using fo4::test::decompile_fixture_file;
using fo4::test::make_exception;

struct Recovered {
  AbcDecompileStatus status{AbcDecompileStatus::Ok};
  AbcDecompileIssue issue;
  AbcStructure structure;
  std::string text;
};

Recovered recover(const CodeBuilder& code, const std::vector<AbcException>& exceptions = {}) {
  Recovered result;
  const fo4::abc::AbcFile file = decompile_fixture_file(code.bytes(), exceptions);
  result.status = fo4::abc::recover_structure(file, 0, result.structure, result.issue);
  result.text = fo4::abc::dump_structure(result.structure);
  return result;
}

void expect_step(const AbcCondStep& step, std::uint32_t block, AbcCondRef taken_kind,
                 std::uint32_t taken_step, AbcCondRef fall_kind, std::uint32_t fall_step) {
  EXPECT_EQ(step.block, block);
  EXPECT_EQ(step.taken_kind, taken_kind);
  EXPECT_EQ(step.taken_step, taken_step);
  EXPECT_EQ(step.fall_kind, fall_kind);
  EXPECT_EQ(step.fall_step, fall_step);
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

CodeBuilder try_catch_code() {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameExternalInterface)
      .op(fo4::test::kOpPop)
      .branch(fo4::test::kOpJump, "AFTER")
      .label("HANDLER");
  catch_prologue(code)
      .stmt(1)
      .op(fo4::test::kOpPopScope)
      .op(fo4::test::kOpPopScope)
      .label("AFTER")
      .op(fo4::test::kOpReturnVoid);
  return code;
}

}  // namespace

TEST(AbcDecompileStructure, RecoversAnIfWithoutElse) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_TRUE(result.issue.ok());
  EXPECT_EQ(result.structure.body, 0u);
  EXPECT_EQ(result.text, "if(B0){B1} B2");
}

TEST(AbcDecompileStructure, RecoversAnIfElseWithTheJoinAfterBothArms) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .stmt(1)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .stmt(2)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "if(B0){B1}else{B2} B3");
}

TEST(AbcDecompileStructure, MergesAnAndChainIntoOneCondition) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "END")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "END")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "if(B0+B1){B2} B3");
  ASSERT_EQ(result.structure.root.size(), 2u);
  const auto& cond = result.structure.root[0].cond;
  ASSERT_EQ(cond.size(), 2u);
  expect_step(cond[0], 0, AbcCondRef::False, 0, AbcCondRef::Step, 1);
  expect_step(cond[1], 1, AbcCondRef::False, 0, AbcCondRef::True, 0);
}

TEST(AbcDecompileStructure, MergesAnOrChainWithTheFirstTestJumpingToTheBody) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "BODY")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "END")
      .label("BODY")
      .stmt(1)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "if(B0+B1){B2} B3");
  const auto& cond = result.structure.root[0].cond;
  ASSERT_EQ(cond.size(), 2u);
  expect_step(cond[0], 0, AbcCondRef::True, 0, AbcCondRef::Step, 1);
  expect_step(cond[1], 1, AbcCondRef::False, 0, AbcCondRef::True, 0);
}

TEST(AbcDecompileStructure, RecoversAWhileLoopCompiledAsJumpToTest) {
  CodeBuilder code;
  code.branch(fo4::test::kOpJump, "COND")
      .label("LOOP")
      .op(fo4::test::kOpLabel)
      .stmt(1)
      .label("COND")
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "while(B2){B1} B3");
}

TEST(AbcDecompileStructure, RecoversADoWhileWhoseBodyAndTestShareOneBlock) {
  CodeBuilder code;
  code.label("LOOP")
      .op(fo4::test::kOpLabel)
      .stmt(1)
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "dowhile(B0){} B1");
}

TEST(AbcDecompileStructure, RecoversADoWhileWithAnInnerIf) {
  CodeBuilder code;
  code.label("LOOP")
      .op(fo4::test::kOpLabel)
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "SKIP")
      .stmt(1)
      .label("SKIP")
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "dowhile(B2){if(B0){B1}} B3");
}

TEST(AbcDecompileStructure, RecoversAnInfiniteLoopWithContinueAndBreak) {
  CodeBuilder code;
  code.label("LOOP")
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
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "loop{if(B0){}else{B2 continue} B1 break} B3");
}

TEST(AbcDecompileStructure, TrimsTheTrailingContinueOfAnIfElseInsideAWhile) {
  CodeBuilder code;
  code.branch(fo4::test::kOpJump, "COND")
      .label("LOOP")
      .op(fo4::test::kOpLabel)
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .stmt(1)
      .branch(fo4::test::kOpJump, "COND")
      .label("ELSE")
      .stmt(2)
      .label("COND")
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "while(B4){if(B1){B2}else{B3}} B5");
}

TEST(AbcDecompileStructure, RecoversASwitchWithBreakingCasesAndADefault) {
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
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "switch(B0){case[0]{B1 break} case[1]{B2 break} default{B3}} B4");
}

TEST(AbcDecompileStructure, GroupsCaseIndicesThatShareATargetAndTheDefault) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .lookupswitch("C1", {"C0", "C1", "C1"})
      .label("C0")
      .stmt(1)
      .branch(fo4::test::kOpJump, "END")
      .label("C1")
      .stmt(2)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "switch(B0){case[0]{B1 break} case[1,2]+default{B2}} B3");

  CodeBuilder shared;
  shared.op(fo4::test::kOpGetLocal1)
      .lookupswitch("DEF", {"C0", "C0"})
      .label("C0")
      .stmt(1)
      .branch(fo4::test::kOpJump, "END")
      .label("DEF")
      .stmt(2)
      .label("END")
      .op(fo4::test::kOpReturnVoid);
  const Recovered second = recover(shared);
  ASSERT_EQ(second.status, AbcDecompileStatus::Ok) << second.issue.message;
  EXPECT_EQ(second.text, "switch(B0){case[0,1]{B1 break} default{B2}} B3");
}

TEST(AbcDecompileStructure, RecoversATryCatchWithTypedHandler) {
  const CodeBuilder code = try_catch_code();
  const std::size_t handler = code.offset_of("HANDLER");
  const Recovered result = recover(code, {make_exception(0, handler, handler)});
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "try{B0}catch0{B1} B2");
  ASSERT_EQ(result.structure.root.size(), 2u);
  EXPECT_EQ(result.structure.root[0].kind, AbcNodeKind::Try);
  ASSERT_EQ(result.structure.root[0].catches.size(), 1u);
  EXPECT_EQ(result.structure.root[0].catches[0].exception, 0u);
}

TEST(AbcDecompileStructure, RecoversAShortCircuitAndOrOrAsAValueRegion) {
  CodeBuilder and_code;
  and_code.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpDup)
      .branch(fo4::test::kOpIfFalse, "J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpGetLocal2)
      .label("J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  const Recovered and_result = recover(and_code);
  ASSERT_EQ(and_result.status, AbcDecompileStatus::Ok) << and_result.issue.message;
  EXPECT_EQ(and_result.text, "and(B0){B1/1} B2");
  EXPECT_EQ(and_result.structure.root[0].kind, AbcNodeKind::ShortCircuit);
  EXPECT_TRUE(and_result.structure.root[0].is_and);

  CodeBuilder or_code;
  or_code.op(fo4::test::kOpGetLocal1)
      .op(fo4::test::kOpDup)
      .branch(fo4::test::kOpIfTrue, "J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpGetLocal2)
      .label("J")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  const Recovered or_result = recover(or_code);
  ASSERT_EQ(or_result.status, AbcDecompileStatus::Ok) << or_result.issue.message;
  EXPECT_EQ(or_result.text, "or(B0){B1/1} B2");
  EXPECT_FALSE(or_result.structure.root[0].is_and);
}

TEST(AbcDecompileStructure, RecoversATernaryFromTheStackDepthAtTheJoin) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .pushbyte(1)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .pushbyte(2)
      .label("END")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  ASSERT_EQ(result.status, AbcDecompileStatus::Ok) << result.issue.message;
  EXPECT_EQ(result.text, "tern(B0){B1}else{B2} B3");
  EXPECT_EQ(result.structure.root[0].kind, AbcNodeKind::Ternary);
}

TEST(AbcDecompileStructure, ProducesTheSameTextOnEveryRun) {
  CodeBuilder code;
  code.branch(fo4::test::kOpJump, "COND")
      .label("LOOP")
      .op(fo4::test::kOpLabel)
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .stmt(1)
      .branch(fo4::test::kOpJump, "COND")
      .label("ELSE")
      .stmt(2)
      .label("COND")
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LOOP")
      .op(fo4::test::kOpReturnVoid);
  const fo4::abc::AbcFile file = decompile_fixture_file(code.bytes());
  AbcStructure first;
  AbcStructure second;
  AbcDecompileIssue issue;
  ASSERT_EQ(fo4::abc::recover_structure(file, 0, first, issue), AbcDecompileStatus::Ok);
  ASSERT_EQ(fo4::abc::recover_structure(file, 0, second, issue), AbcDecompileStatus::Ok);
  EXPECT_EQ(fo4::abc::dump_structure(first), fo4::abc::dump_structure(second));
  EXPECT_FALSE(fo4::abc::dump_structure(first).empty());
}

TEST(AbcDecompileStructure, RefusesALoopWithTwoEntries) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "LB")
      .label("LA")
      .stmt(1)
      .branch(fo4::test::kOpJump, "LB")
      .label("LB")
      .stmt(2)
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfTrue, "LA")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  EXPECT_EQ(result.status, AbcDecompileStatus::IrreducibleFlow);
  EXPECT_EQ(result.issue.status, AbcDecompileStatus::IrreducibleFlow);
  EXPECT_NE(result.issue.message.find("irreducible"), std::string::npos);
  EXPECT_TRUE(result.structure.root.empty());
}

TEST(AbcDecompileStructure, RefusesALoopWithSeveralExits) {
  CodeBuilder code;
  code.label("LOOP")
      .op(fo4::test::kOpLabel)
      .op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfTrue, "OUT1")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfTrue, "OUT2")
      .branch(fo4::test::kOpJump, "LOOP")
      .label("OUT1")
      .stmt(1)
      .label("OUT2")
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  EXPECT_EQ(result.status, AbcDecompileStatus::UnstructuredFlow);
  EXPECT_EQ(result.issue.offset, 0u);
  EXPECT_NE(result.issue.message.find("several exits"), std::string::npos);
  EXPECT_TRUE(result.structure.root.empty());
}

TEST(AbcDecompileStructure, RefusesAFinallyOrCatchAllHandler) {
  const CodeBuilder code = try_catch_code();
  const std::size_t handler = code.offset_of("HANDLER");
  const Recovered result = recover(code, {make_exception(0, handler, handler, 0)});
  EXPECT_EQ(result.status, AbcDecompileStatus::UnsupportedTry);
  EXPECT_EQ(result.issue.offset, 0u);
  EXPECT_NE(result.issue.message.find("finally or catch-all"), std::string::npos);
}

TEST(AbcDecompileStructure, RefusesAHandlerWithoutTheCatchPrologue) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameExternalInterface)
      .op(fo4::test::kOpPop)
      .branch(fo4::test::kOpJump, "AFTER")
      .label("HANDLER")
      .op(fo4::test::kOpPop)
      .label("AFTER")
      .op(fo4::test::kOpReturnVoid);
  const std::size_t handler = code.offset_of("HANDLER");
  const Recovered result = recover(code, {make_exception(0, handler, handler)});
  EXPECT_EQ(result.status, AbcDecompileStatus::UnsupportedTry);
  EXPECT_EQ(result.issue.offset, handler);
  EXPECT_NE(result.issue.message.find("catch prologue"), std::string::npos);
}

TEST(AbcDecompileStructure, RefusesTryRangesThatOverlapWithoutNesting) {
  CodeBuilder code;
  code.op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameExternalInterface)
      .op(fo4::test::kOpPop)
      .label("P3")
      .op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameExternalInterface)
      .op(fo4::test::kOpPop)
      .label("P6")
      .op_u30(fo4::test::kOpGetLex, fo4::test::kMultinameExternalInterface)
      .op(fo4::test::kOpPop)
      .label("P9")
      .branch(fo4::test::kOpJump, "AFTER")
      .label("HANDLER")
      .op(fo4::test::kOpPop)
      .label("AFTER")
      .op(fo4::test::kOpReturnVoid);
  const std::size_t handler = code.offset_of("HANDLER");
  const Recovered result =
      recover(code, {make_exception(0, code.offset_of("P6"), handler),
                     make_exception(code.offset_of("P3"), code.offset_of("P9"), handler)});
  EXPECT_EQ(result.status, AbcDecompileStatus::UnsupportedTry);
  EXPECT_EQ(result.issue.offset, code.offset_of("P3"));
  EXPECT_NE(result.issue.message.find("overlap"), std::string::npos);
}

TEST(AbcDecompileStructure, RefusesAStatementBranchInsideAValueArm) {
  CodeBuilder code;
  code.op(fo4::test::kOpGetLocal1)
      .branch(fo4::test::kOpIfFalse, "ELSE")
      .op(fo4::test::kOpGetLocal2)
      .branch(fo4::test::kOpIfFalse, "SKIP")
      .stmt(3)
      .label("SKIP")
      .pushbyte(1)
      .branch(fo4::test::kOpJump, "END")
      .label("ELSE")
      .pushbyte(2)
      .label("END")
      .op(fo4::test::kOpPop)
      .op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  EXPECT_EQ(result.status, AbcDecompileStatus::UnsupportedValueRegion);
  EXPECT_EQ(result.issue.offset, 5u);
  EXPECT_NE(result.issue.message.find("statement-level branch"), std::string::npos);
}

TEST(AbcDecompileStructure, RefusesAMissingBody) {
  CodeBuilder code;
  code.op(fo4::test::kOpReturnVoid);
  const fo4::abc::AbcFile file = decompile_fixture_file(code.bytes());
  AbcStructure structure;
  AbcDecompileIssue issue;
  EXPECT_EQ(fo4::abc::recover_structure(file, 99, structure, issue), AbcDecompileStatus::BodyNotFound);
  EXPECT_EQ(issue.status, AbcDecompileStatus::BodyNotFound);
}

TEST(AbcDecompileStructure, RefusesABodyThatFailsFlowVerification) {
  CodeBuilder code;
  code.op(fo4::test::kOpPop).op(fo4::test::kOpReturnVoid);
  const Recovered result = recover(code);
  EXPECT_EQ(result.status, AbcDecompileStatus::FlowRefused);
  EXPECT_NE(result.issue.message.find("stack underflow"), std::string::npos);
  EXPECT_EQ(result.issue.body, 0u);
}
