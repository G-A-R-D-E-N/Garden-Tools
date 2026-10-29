#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/disasm.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

namespace {

constexpr std::uint8_t kIAdd = 1;
constexpr std::uint8_t kAssign = 13;
constexpr std::uint8_t kCmpLt = 16;
constexpr std::uint8_t kJmp = 20;
constexpr std::uint8_t kJmpT = 21;
constexpr std::uint8_t kJmpF = 22;
constexpr std::uint8_t kCallMethod = 23;
constexpr std::uint8_t kReturn = 26;

PexValue identifier(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexValue integer(std::int32_t number) {
  PexValue value;
  value.type = PexValueType::Integer;
  value.int_ = number;
  return value;
}

PexValue none() { return PexValue{}; }

PexInstruction step(std::uint8_t opcode, std::vector<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcode;
  instruction.args = std::move(args);
  return instruction;
}

PexFunction function(std::string name, std::vector<PexInstruction> instructions) {
  PexFunction result;
  result.name = std::move(name);
  result.instructions = std::move(instructions);
  return result;
}

PexObject object(std::string name, std::vector<PexState> states) {
  PexObject result;
  result.name = std::move(name);
  result.parentClassName = "ObjectReference";
  result.autoStateName = "";
  result.states = std::move(states);
  return result;
}

PexState state(std::string name, std::vector<PexFunction> functions) {
  PexState result;
  result.name = std::move(name);
  result.functions = std::move(functions);
  return result;
}

PexFunction computedWhileFunction() {
  return function("Loop", {
                              step(kAssign, {identifier("::temp1"), integer(0)}),
                              step(kCmpLt, {identifier("::temp2"), identifier("::temp1"),
                                            integer(10)}),
                              step(kJmpF, {identifier("::temp2"), integer(4)}),
                              step(kCallMethod, {identifier("Work"), identifier("self"),
                                                 identifier("::nonevar")}),
                              step(kIAdd, {identifier("::temp1"), identifier("::temp1"),
                                           integer(1)}),
                              step(kJmp, {integer(-4)}),
                              step(kReturn, {none()}),
                          });
}

PexFunction nestedIfFunction() {
  return function("Nested", {
                              step(kJmpF, {identifier("::temp1"), integer(6)}),
                              step(kJmpF, {identifier("::temp2"), integer(3)}),
                              step(kAssign, {identifier("Result"), integer(1)}),
                              step(kJmp, {integer(2)}),
                              step(kAssign, {identifier("Result"), integer(2)}),
                              step(kJmp, {integer(2)}),
                              step(kAssign, {identifier("Result"), integer(3)}),
                              step(kReturn, {none()}),
                          });
}

PexFunction sequentialLoopsFunction() {
  return function("Sequential", {
                                   step(kAssign, {identifier("::temp2"), integer(1)}),
                                   step(kJmpF, {identifier("::temp2"), integer(4)}),
                                   step(kCallMethod, {identifier("Work"), identifier("self"),
                                                      identifier("::nonevar")}),
                                   step(kAssign, {identifier("::temp2"), integer(0)}),
                                   step(kJmp, {integer(-3)}),
                                   step(kAssign, {identifier("::temp3"), integer(1)}),
                                   step(kJmpF, {identifier("::temp3"), integer(4)}),
                                   step(kCallMethod, {identifier("Rest"), identifier("self"),
                                                      identifier("::nonevar")}),
                                   step(kAssign, {identifier("::temp3"), integer(0)}),
                                   step(kJmp, {integer(-3)}),
                                   step(kReturn, {none()}),
                               });
}

PexFunction branchInLoopFunction() {
  return function("BranchInLoop", {
                                     step(kAssign, {identifier("::temp1"), integer(0)}),
                                     step(kJmpF, {identifier("::temp2"), integer(6)}),
                                     step(kJmpF, {identifier("::temp3"), integer(3)}),
                                     step(kAssign, {identifier("Result"), integer(1)}),
                                     step(kJmp, {integer(2)}),
                                     step(kAssign, {identifier("Result"), integer(2)}),
                                     step(kJmp, {integer(-5)}),
                                     step(kReturn, {none()}),
                                 });
}

PexFunction loopInBranchFunction() {
  return function("LoopInBranch", {
                                     step(kJmpF, {identifier("::temp1"), integer(5)}),
                                     step(kJmpF, {identifier("::temp2"), integer(3)}),
                                     step(kIAdd, {identifier("::temp1"), identifier("::temp1"),
                                                  integer(1)}),
                                     step(kJmp, {integer(-2)}),
                                     step(kJmp, {integer(2)}),
                                     step(kAssign, {identifier("Result"), integer(2)}),
                                     step(kReturn, {none()}),
                                 });
}

PexFunction shortCircuitFunction() {
  return function("ShortCircuit", {
                                     step(kJmpT, {identifier("::temp1"), integer(3)}),
                                     step(kAssign, {identifier("Result"), integer(1)}),
                                     step(kJmp, {integer(2)}),
                                     step(kAssign, {identifier("Result"), integer(2)}),
                                     step(kReturn, {none()}),
                                 });
}

PexFunction jmptLoopHeaderFunction() {
  return function("JmptLoopHeader", {
                                       step(kJmpT, {identifier("::temp1"), integer(3)}),
                                       step(kIAdd, {identifier("::temp1"), identifier("::temp1"),
                                                    integer(1)}),
                                       step(kJmp, {integer(-2)}),
                                       step(kReturn, {none()}),
                                   });
}

PexFunction deepNestingFunction(std::size_t depth) {
  std::vector<PexInstruction> instructions;
  instructions.reserve(depth + 2);
  for (std::size_t i = 0; i < depth; ++i) {
    instructions.push_back(step(kJmpF, {identifier("::t" + std::to_string(i)),
                                        integer(static_cast<std::int32_t>(depth + 1 - i))}));
  }
  instructions.push_back(step(kAssign, {identifier("Result"), integer(1)}));
  instructions.push_back(step(kReturn, {none()}));
  return function("DeepNesting", std::move(instructions));
}

PexFile singleFunctionFile(const PexFunction& fn) {
  PexFile file;
  file.majorVersion = 3;
  file.minorVersion = 9;
  file.gameId = 2;
  file.sourceFileName = "DisasmShapes.psc";
  file.objects.push_back(object("DisasmShapes", {state("", {fn})}));
  file.rebuildStringTable();
  return file;
}

std::size_t listed_count(const std::string& text, std::size_t index) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%4zu: ", index);
  std::size_t count = 0;
  for (auto at = text.find(buffer); at != std::string::npos; at = text.find(buffer, at + 1)) {
    ++count;
  }
  return count;
}

}

TEST(PexAnalysis, DisassemblesWhileLoopWithComputedCondition) {
  const auto result = fo4::pex_analysis::disassemble_function(computedWhileFunction());
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  ASSERT_TRUE(result.structured);
  const std::string expected =
      "   0: assign ::temp1 0\n"
      "while (true) {\n"
      "     1: cmp_lt ::temp2 ::temp1 10\n"
      "  if (!(::temp2)) break;\n"
      "     3: callmethod Work self ::nonevar\n"
      "     4: iadd ::temp1 ::temp1 1\n"
      "}\n"
      "   6: return none\n";
  EXPECT_EQ(result.text, expected);
}

TEST(PexAnalysis, DisassemblesNestedIfElse) {
  const auto result = fo4::pex_analysis::disassemble_function(nestedIfFunction());
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  ASSERT_TRUE(result.structured);
  const std::string expected =
      "if (::temp1) {\n"
      "  if (::temp2) {\n"
      "       2: assign Result 1\n"
      "  } else {\n"
      "       4: assign Result 2\n"
      "  }\n"
      "} else {\n"
      "     6: assign Result 3\n"
      "}\n"
      "   7: return none\n";
  EXPECT_EQ(result.text, expected);
}

TEST(PexAnalysis, DisassemblesSequentialLoops) {
  const auto result = fo4::pex_analysis::disassemble_function(sequentialLoopsFunction());
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  ASSERT_TRUE(result.structured);
  const auto first = result.text.find("while (::temp2) {");
  const auto second = result.text.find("while (::temp3) {");
  ASSERT_NE(first, std::string::npos);
  ASSERT_NE(second, std::string::npos);
  EXPECT_LT(first, second);
  EXPECT_NE(result.text.find("2: callmethod Work self ::nonevar"), std::string::npos);
  EXPECT_NE(result.text.find("5: assign ::temp3 1"), std::string::npos);
  EXPECT_NE(result.text.find("7: callmethod Rest self ::nonevar"), std::string::npos);
  EXPECT_NE(result.text.find("10: return none"), std::string::npos);
  EXPECT_EQ(result.text.find("L1:"), std::string::npos);
}

TEST(PexAnalysis, DisassemblesBranchInsideLoopAndLoopInsideBranch) {
  const auto branch = fo4::pex_analysis::disassemble_function(branchInLoopFunction());
  ASSERT_EQ(branch.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  ASSERT_TRUE(branch.structured);
  const auto loop = branch.text.find("while (::temp2) {");
  const auto nested = branch.text.find("  if (::temp3) {");
  ASSERT_NE(loop, std::string::npos);
  ASSERT_NE(nested, std::string::npos);
  EXPECT_LT(loop, nested);
  EXPECT_NE(branch.text.find("3: assign Result 1"), std::string::npos);
  EXPECT_NE(branch.text.find("5: assign Result 2"), std::string::npos);
  EXPECT_EQ(branch.text.find("6: jmp -5"), std::string::npos);

  const auto inner = fo4::pex_analysis::disassemble_function(loopInBranchFunction());
  ASSERT_EQ(inner.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  ASSERT_TRUE(inner.structured);
  const auto branchLine = inner.text.find("if (::temp1) {");
  const auto body = inner.text.find("  while (::temp2) {");
  ASSERT_NE(branchLine, std::string::npos);
  ASSERT_NE(body, std::string::npos);
  EXPECT_LT(branchLine, body);
  EXPECT_NE(inner.text.find("2: iadd ::temp1 ::temp1 1"), std::string::npos);
  EXPECT_NE(inner.text.find("5: assign Result 2"), std::string::npos);
  EXPECT_EQ(inner.text.find("L1:"), std::string::npos);
}

TEST(PexAnalysis, DisassemblyKeepsLabelledListingForShortCircuitJmpt) {
  const auto result = fo4::pex_analysis::disassemble_function(shortCircuitFunction());
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_FALSE(result.structured);
  EXPECT_EQ(result.text.find("if ("), std::string::npos);
  EXPECT_NE(result.text.find("L3:"), std::string::npos);
  EXPECT_NE(result.text.find("2: jmp 2"), std::string::npos);
  EXPECT_NE(result.text.find("3: assign Result 2"), std::string::npos);
}

TEST(PexAnalysis, DisassemblyKeepsLabelledListingForJmptLoopHeader) {
  const auto result = fo4::pex_analysis::disassemble_function(jmptLoopHeaderFunction());
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_FALSE(result.structured);
  EXPECT_EQ(result.text.find("while ("), std::string::npos);
  EXPECT_NE(result.text.find("L0:"), std::string::npos);
  EXPECT_NE(result.text.find("L3:"), std::string::npos);
  EXPECT_NE(result.text.find("1: iadd ::temp1 ::temp1 1"), std::string::npos);
  EXPECT_NE(result.text.find("2: jmp -2"), std::string::npos);
}

TEST(PexAnalysis, DisassemblyBailsOnTailJumpThatDoesNotReachTheEnd) {
  const auto loopback = fo4::pex_analysis::disassemble_function(
      function("TailLoopback", {
                                   step(kJmpF, {identifier("::temp1"), integer(2)}),
                                   step(kJmp, {integer(-1)}),
                                   step(kJmp, {integer(-1)}),
                               }));
  ASSERT_EQ(loopback.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_FALSE(loopback.structured);
  EXPECT_EQ(loopback.text.find("while ("), std::string::npos);
  EXPECT_NE(loopback.text.find("2: jmp -1"), std::string::npos);

  const auto returned = fo4::pex_analysis::disassemble_function(
      function("TailJumpToReturn", {
                                       step(kJmpF, {identifier("::temp1"), integer(2)}),
                                       step(kReturn, {none()}),
                                       step(kJmp, {integer(-1)}),
                                   }));
  ASSERT_EQ(returned.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_FALSE(returned.structured);
  EXPECT_EQ(returned.text.find("if ("), std::string::npos);
  EXPECT_NE(returned.text.find("2: jmp -1"), std::string::npos);
}

TEST(PexAnalysis, DisassemblyFallsBackPastTheNestingCapWithoutRecursing) {
  const auto atCap = fo4::pex_analysis::disassemble_function(deepNestingFunction(64));
  ASSERT_EQ(atCap.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_TRUE(atCap.structured);
  EXPECT_NE(atCap.text.find("if ("), std::string::npos);
  const auto pastCap = fo4::pex_analysis::disassemble_function(deepNestingFunction(65));
  ASSERT_EQ(pastCap.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_FALSE(pastCap.structured);
  EXPECT_EQ(pastCap.text.find("if ("), std::string::npos);
  constexpr std::size_t kDepth = 50000;
  const auto result = fo4::pex_analysis::disassemble_function(deepNestingFunction(kDepth));
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  EXPECT_FALSE(result.structured);
  EXPECT_EQ(result.text.find("if ("), std::string::npos);
  EXPECT_NE(result.text.find("L" + std::to_string(kDepth + 1) + ":"), std::string::npos);
  EXPECT_NE(result.text.find(std::to_string(kDepth) + ": assign Result 1"), std::string::npos);
}

TEST(PexAnalysis, StructuredListingSurvivesPexEncoding) {
  const auto source = nestedIfFunction();
  const auto decoded = PexFile::fromBytes(singleFunctionFile(source).toBytes());
  ASSERT_EQ(decoded.objects.size(), 1u);
  ASSERT_EQ(decoded.objects[0].states.size(), 1u);
  ASSERT_EQ(decoded.objects[0].states[0].functions.size(), 1u);
  const auto& reloaded = decoded.objects[0].states[0].functions[0];
  const auto inMemory = fo4::pex_analysis::disassemble_function(source);
  const auto fromFile = fo4::pex_analysis::disassemble_function(reloaded);
  ASSERT_TRUE(inMemory.structured);
  EXPECT_TRUE(fromFile.structured);
  EXPECT_EQ(fromFile.text, inMemory.text);
  EXPECT_NE(fromFile.text.find("if (::temp2) {"), std::string::npos);

  auto shifted = nestedIfFunction();
  shifted.instructions[3].args = {integer(3)};
  const auto shiftedFile = PexFile::fromBytes(singleFunctionFile(shifted).toBytes());
  ASSERT_EQ(shiftedFile.objects.size(), 1u);
  const auto fromShifted =
      fo4::pex_analysis::disassemble_function(shiftedFile.objects[0].states[0].functions[0]);
  EXPECT_FALSE(fromShifted.structured);
  EXPECT_EQ(fromShifted.text.find("if ("), std::string::npos);
}

TEST(PexAnalysis, StructuredListingKeepsEveryInstructionOnce) {
  const std::vector<PexFunction> functions{computedWhileFunction(), nestedIfFunction(),
                                           sequentialLoopsFunction(), branchInLoopFunction(),
                                           loopInBranchFunction()};
  for (const auto& fn : functions) {
    const auto structured = fo4::pex_analysis::disassemble_function(fn);
    const auto plain = fo4::pex_analysis::disassemble_function(fn, {false});
    ASSERT_TRUE(structured.structured);
    ASSERT_FALSE(plain.structured);
    for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
      EXPECT_LE(listed_count(structured.text, i), 1u);
      if (fo4::pex_analysis::pex_is_jump(fn.instructions[i].opCode)) continue;
      EXPECT_EQ(listed_count(structured.text, i), 1u);
      EXPECT_EQ(listed_count(plain.text, i), 1u);
    }
  }
}
