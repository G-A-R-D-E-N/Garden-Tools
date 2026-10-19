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
  const auto loop = result.text.find("while (true) {");
  const auto prefix = result.text.find("1: cmp_lt ::temp2 ::temp1 10");
  const auto guard = result.text.find("if (!(::temp2)) break;");
  ASSERT_NE(loop, std::string::npos);
  ASSERT_NE(prefix, std::string::npos);
  ASSERT_NE(guard, std::string::npos);
  EXPECT_LT(loop, prefix);
  EXPECT_LT(prefix, guard);
  EXPECT_NE(result.text.find("  3: callmethod Work self ::nonevar"), std::string::npos);
  EXPECT_NE(result.text.find("6: return none"), std::string::npos);
  EXPECT_EQ(result.text.find("L1:"), std::string::npos);
  EXPECT_EQ(result.text.find("5: jmp -4"), std::string::npos);
}

TEST(PexAnalysis, DisassemblesNestedIfElse) {
  const auto result = fo4::pex_analysis::disassemble_function(nestedIfFunction());
  ASSERT_EQ(result.status, fo4::pex_analysis::PexAnalysisStatus::Ok);
  ASSERT_TRUE(result.structured);
  const auto outer = result.text.find("if (::temp1) {");
  const auto inner = result.text.find("  if (::temp2) {");
  ASSERT_NE(outer, std::string::npos);
  ASSERT_NE(inner, std::string::npos);
  EXPECT_LT(outer, inner);
  EXPECT_NE(result.text.find("2: assign Result 1"), std::string::npos);
  EXPECT_NE(result.text.find("4: assign Result 2"), std::string::npos);
  EXPECT_NE(result.text.find("6: assign Result 3"), std::string::npos);
  EXPECT_NE(result.text.find("7: return none"), std::string::npos);
  EXPECT_EQ(result.text.find("L0:"), std::string::npos);
  EXPECT_EQ(result.text.find("3: jmp 2"), std::string::npos);
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
