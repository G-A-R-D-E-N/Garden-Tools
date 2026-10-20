#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"
#include "fo4/pex_analysis/cfg.hpp"
#include "fo4/pex_analysis/disasm.hpp"
#include "fo4/pex_analysis/fragments.hpp"
#include "fo4/pex_analysis/summary.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex::PexVariable;
using fo4::pex_analysis::PexAccessKind;
using fo4::pex_analysis::PexAnalysisStatus;
using fo4::pex_analysis::PexCallKind;
using fo4::pex_analysis::PexFunctionRef;
using fo4::pex_analysis::PexFunctionSlot;

namespace {

constexpr std::uint8_t kAssign = 13;
constexpr std::uint8_t kCmpLt = 16;
constexpr std::uint8_t kJmp = 20;
constexpr std::uint8_t kJmpF = 22;
constexpr std::uint8_t kCallMethod = 23;
constexpr std::uint8_t kCallStatic = 25;
constexpr std::uint8_t kReturn = 26;
constexpr std::uint8_t kPropGet = 28;
constexpr std::uint8_t kPropSet = 29;
constexpr std::uint8_t kArrayLength = 31;
constexpr std::uint8_t kArrayGetElement = 32;
constexpr std::uint8_t kArrayFindElement = 34;
constexpr std::uint8_t kStructGet = 38;
constexpr std::uint8_t kArrayFindStruct = 40;
constexpr std::uint8_t kArrayRemove = 45;
constexpr std::uint8_t kIAdd = 1;

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

PexValue text(std::string value) {
  PexValue result;
  result.type = PexValueType::String;
  result.str = std::move(value);
  return result;
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


PexFunction diamondFunction() {
  return function("Diamond", {
                                  step(kAssign, {identifier("::temp1"), identifier("Count")}),
                                  step(kJmpF, {identifier("::temp1"), integer(3)}),
                                  step(kAssign, {identifier("Result"), integer(1)}),
                                  step(kJmp, {integer(2)}),
                                  step(kAssign, {identifier("Result"), integer(2)}),
                                  step(kReturn, {none()}),
                              });
}

PexFunction whileFunction() {
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

}

TEST(PexAnalysis, CfgSplitsStraightLineAndIfDiamond) {
  const auto cfg = fo4::pex_analysis::build_cfg(diamondFunction());
  ASSERT_EQ(cfg.status, PexAnalysisStatus::Ok);
  ASSERT_EQ(cfg.blocks.size(), 5u);
  EXPECT_EQ(cfg.exitBlock, 4u);
  EXPECT_EQ(cfg.blocks[0].first, 0u);
  EXPECT_EQ(cfg.blocks[0].count, 2u);
  EXPECT_EQ(cfg.blocks[0].successors, (std::vector<std::size_t>{2u, 1u}));
  EXPECT_EQ(cfg.blocks[1].successors, (std::vector<std::size_t>{3u}));
  EXPECT_EQ(cfg.blocks[3].successors, (std::vector<std::size_t>{}));
  EXPECT_TRUE(cfg.loops.empty());
  EXPECT_TRUE(cfg.unreachableBlocks.empty());
  for (std::size_t b = 0; b < cfg.exitBlock; ++b) EXPECT_TRUE(cfg.blocks[b].reachable);
  EXPECT_EQ(cfg.blocks[cfg.exitBlock].first, 6u);
  EXPECT_EQ(cfg.blocks[cfg.exitBlock].count, 0u);
  EXPECT_FALSE(cfg.blocks[cfg.exitBlock].reachable);
}

TEST(PexAnalysis, CfgFindsWhileLoopBackEdge) {
  const auto cfg = fo4::pex_analysis::build_cfg(whileFunction());
  ASSERT_EQ(cfg.status, PexAnalysisStatus::Ok);
  ASSERT_EQ(cfg.loops.size(), 1u);
  EXPECT_EQ(cfg.loops[0].headerInstruction, 1u);
  EXPECT_EQ(cfg.loops[0].backEdgeInstruction, 5u);
  EXPECT_TRUE(cfg.blocks[cfg.loops[0].headerBlock].isLoopHeader);
  EXPECT_EQ(cfg.blocks[cfg.loops[0].headerBlock].predecessors,
            (std::vector<std::size_t>{0u, cfg.loops[0].latchBlock}));
  EXPECT_TRUE(cfg.unreachableBlocks.empty());
}

TEST(PexAnalysis, CfgReportsUnreachableBlockAfterReturn) {
  const auto cfg = fo4::pex_analysis::build_cfg(
      function("Dead", {
                           step(kReturn, {none()}),
                           step(kAssign, {identifier("Count"), integer(1)}),
                           step(kReturn, {none()}),
                       }));
  ASSERT_EQ(cfg.status, PexAnalysisStatus::Ok);
  ASSERT_EQ(cfg.unreachableBlocks.size(), 1u);
  const auto dead = cfg.unreachableBlocks.front();
  EXPECT_EQ(cfg.blocks[dead].first, 1u);
  EXPECT_FALSE(cfg.blocks[dead].reachable);
  EXPECT_TRUE(cfg.blocks[0].reachable);
}

TEST(PexAnalysis, CfgRejectsJumpTargetOutOfRange) {
  const auto forward = fo4::pex_analysis::build_cfg(
      function("Bad", {step(kJmp, {integer(5)}), step(kReturn, {none()})}));
  EXPECT_EQ(forward.status, PexAnalysisStatus::JumpTargetOutOfRange);
  EXPECT_EQ(forward.errorInstruction, 0u);
  EXPECT_EQ(forward.errorTarget, 5);

  const auto backward = fo4::pex_analysis::build_cfg(
      function("Bad", {step(kJmp, {integer(-3)}), step(kReturn, {none()})}));
  EXPECT_EQ(backward.status, PexAnalysisStatus::JumpTargetOutOfRange);
  EXPECT_EQ(backward.errorTarget, -3);
  EXPECT_TRUE(backward.blocks.empty());
}

TEST(PexAnalysis, CfgRejectsJumpWithoutIntegerOperand) {
  const auto malformed = fo4::pex_analysis::build_cfg(
      function("Bad", {step(kJmp, {}), step(kReturn, {none()})}));
  EXPECT_EQ(malformed.status, PexAnalysisStatus::MalformedInstruction);
  EXPECT_EQ(malformed.errorInstruction, 0u);

  const auto conditional = fo4::pex_analysis::build_cfg(function(
      "Bad", {step(kJmpF, {identifier("::temp1")}), step(kReturn, {none()})}));
  EXPECT_EQ(conditional.status, PexAnalysisStatus::MalformedInstruction);
}

TEST(PexAnalysis, DisassemblesIfElseDiamondWithoutLabels) {
  const auto result = fo4::pex_analysis::disassemble_function(diamondFunction());
  ASSERT_EQ(result.status, PexAnalysisStatus::Ok);
  EXPECT_TRUE(result.structured);
  EXPECT_NE(result.text.find("if (::temp1) {"), std::string::npos);
  EXPECT_NE(result.text.find("} else {"), std::string::npos);
  EXPECT_NE(result.text.find("2: assign Result 1"), std::string::npos);
  EXPECT_NE(result.text.find("4: assign Result 2"), std::string::npos);
  EXPECT_EQ(result.text.find("L4:"), std::string::npos);
  EXPECT_EQ(result.text.find("3: jmp 2"), std::string::npos);
}

TEST(PexAnalysis, DisassemblesComputedWhileConditionWithoutGotoFallback) {
  const auto result = fo4::pex_analysis::disassemble_function(whileFunction());
  ASSERT_EQ(result.status, PexAnalysisStatus::Ok);
  EXPECT_TRUE(result.structured);
  EXPECT_NE(result.text.find("while (true) {"), std::string::npos);
  EXPECT_NE(result.text.find("if (!(::temp2)) break;"), std::string::npos);
  EXPECT_NE(result.text.find("3: callmethod Work self ::nonevar"), std::string::npos);
  EXPECT_EQ(result.text.find("L1:"), std::string::npos);
  EXPECT_EQ(result.text.find("5: jmp -4"), std::string::npos);
}

TEST(PexAnalysis, DisassemblesWhileLoopWithVariableCondition) {
  const auto result = fo4::pex_analysis::disassemble_function(function(
      "Spin", {
                  step(kAssign, {identifier("::temp2"), integer(1)}),
                  step(kJmpF, {identifier("::temp2"), integer(4)}),
                  step(kCallMethod, {identifier("Work"), identifier("self"),
                                     identifier("::nonevar")}),
                  step(kAssign, {identifier("::temp2"), integer(0)}),
                  step(kJmp, {integer(-3)}),
                  step(kReturn, {none()}),
              }));
  ASSERT_EQ(result.status, PexAnalysisStatus::Ok);
  EXPECT_TRUE(result.structured);
  EXPECT_NE(result.text.find("while (::temp2) {"), std::string::npos);
  EXPECT_NE(result.text.find("2: callmethod Work self ::nonevar"), std::string::npos);
  EXPECT_NE(result.text.find("5: return none"), std::string::npos);
  EXPECT_EQ(result.text.find("4: jmp"), std::string::npos);
}

TEST(PexAnalysis, DisassemblyFallsBackToLabelsAndGotoStyle) {
  const auto dead = fo4::pex_analysis::disassemble_function(
      function("Dead", {
                           step(kReturn, {none()}),
                           step(kAssign, {identifier("Count"), integer(1)}),
                           step(kReturn, {none()}),
                       }));
  ASSERT_EQ(dead.status, PexAnalysisStatus::Ok);
  EXPECT_FALSE(dead.structured);
  EXPECT_NE(dead.text.find("; unreachable"), std::string::npos);
  EXPECT_NE(dead.text.find("1: assign Count 1"), std::string::npos);

  const auto branch = fo4::pex_analysis::disassemble_function(function(
      "Goto", {step(kJmp, {integer(1)}), step(kReturn, {none()}), step(kReturn, {none()})}));
  ASSERT_EQ(branch.status, PexAnalysisStatus::Ok);
  EXPECT_FALSE(branch.structured);
  EXPECT_NE(branch.text.find("L1:"), std::string::npos);
}

TEST(PexAnalysis, DisassemblyResolvesStringTableNames) {
  const auto result = fo4::pex_analysis::disassemble_function(function(
      "Loader", {step(kCallStatic, {identifier("game"), identifier("GetFormFromFile"),
                                    identifier("::temp30"), text("Fallout4.esm")}),
                 step(kReturn, {none()})}));
  ASSERT_EQ(result.status, PexAnalysisStatus::Ok);
  EXPECT_NE(result.text.find("callstatic game GetFormFromFile ::temp30 \"Fallout4.esm\""),
            std::string::npos);
}

TEST(PexAnalysis, DisassemblyReportsOutOfRangeJumpAsStatus) {
  const auto result = fo4::pex_analysis::disassemble_function(
      function("Bad", {step(kJmp, {integer(5)}), step(kReturn, {none()})}));
  EXPECT_EQ(result.status, PexAnalysisStatus::JumpTargetOutOfRange);
  EXPECT_FALSE(result.structured);
  EXPECT_NE(result.text.find("jump target out of range"), std::string::npos);
  EXPECT_NE(result.text.find("target 5"), std::string::npos);
}

TEST(PexAnalysis, CallGraphResolvesSelfStaticAndRecursiveCalls) {
  auto caller = function("OnInit", {
                                        step(kCallMethod, {identifier("Helper"),
                                                           identifier("self"),
                                                           identifier("::nonevar")}),
                                        step(kCallStatic, {identifier("BetaScript"),
                                                           identifier("Ping"),
                                                           identifier("::nonevar")}),
                                        step(kReturn, {none()}),
                                    });
  auto helper = function("Helper", {
                                        step(kCallMethod, {identifier("OnInit"),
                                                           identifier("self"),
                                                           identifier("::nonevar")}),
                                        step(kReturn, {none()}),
                                    });
  auto ping = function("Ping", {step(kReturn, {none()})});
  auto idle = function("Idle", {step(kReturn, {none()})});
  ping.isNative = true;

  PexFile alpha;
  alpha.sourceFileName = "AlphaScript.psc";
  alpha.objects.push_back(object("AlphaScript", {state("", {caller, helper, idle})}));
  PexFile beta;
  beta.sourceFileName = "BetaScript.psc";
  beta.objects.push_back(object("BetaScript", {state("", {ping})}));

  const std::vector<const PexFile*> files{&alpha, &beta};
  const auto graph = fo4::pex_analysis::build_call_graph(files);

  ASSERT_EQ(graph.calls.size(), 3u);
  EXPECT_EQ(graph.unresolvedCalls, 0u);
  EXPECT_EQ(graph.nativeCalls, 1u);

  const auto& helperCall = graph.calls[0];
  EXPECT_EQ(helperCall.kind, PexCallKind::Method);
  EXPECT_TRUE(helperCall.selfTarget);
  EXPECT_TRUE(helperCall.resolved);
  EXPECT_EQ(helperCall.target.fileIndex, 0u);
  EXPECT_EQ(helperCall.target.functionName, "Helper");
  EXPECT_EQ(helperCall.instructionIndex, 0u);

  const auto& staticCall = graph.calls[1];
  EXPECT_EQ(staticCall.kind, PexCallKind::Static);
  EXPECT_TRUE(staticCall.resolved);
  EXPECT_EQ(staticCall.target.fileIndex, 1u);
  EXPECT_EQ(staticCall.target.objectName, "BetaScript");
  EXPECT_TRUE(staticCall.nativeTarget);

  const auto& recursive = graph.calls[2];
  EXPECT_TRUE(recursive.resolved);
  EXPECT_EQ(recursive.target.functionName, "OnInit");
  EXPECT_EQ(recursive.caller.functionName, "Helper");

  ASSERT_EQ(graph.roots.size(), 1u);
  EXPECT_EQ(graph.roots[0].functionName, "Idle");
  EXPECT_EQ(graph.roots[0].fileIndex, 0u);
}

TEST(PexAnalysis, CallGraphKeepsUnresolvedAndParentCalls) {
  auto caller = function("OnActivate", {
                                           step(kCallMethod, {identifier("Missing"),
                                                              identifier("::temp1"),
                                                              identifier("::nonevar")}),
                                           step(kCallMethod, {identifier("Setup"),
                                                              identifier("Player"),
                                                              identifier("::nonevar")}),
                                           step(kReturn, {none()}),
                                       });
  PexObject host = object("TerminalScript", {state("", {caller})});
  PexVariable player;
  player.name = "Player";
  player.type = "Actor";
  host.variables.push_back(player);

  PexFile file;
  file.sourceFileName = "TerminalScript.psc";
  file.objects.push_back(host);

  const std::vector<const PexFile*> files{&file};
  const auto graph = fo4::pex_analysis::build_call_graph(files);
  ASSERT_EQ(graph.calls.size(), 2u);
  EXPECT_EQ(graph.calls[0].kind, PexCallKind::Method);
  EXPECT_FALSE(graph.calls[0].resolved);
  EXPECT_EQ(graph.calls[0].objectName, "::temp1");
  EXPECT_EQ(graph.unresolvedCalls, 2u);
  EXPECT_EQ(graph.calls[1].objectName, "Actor");
  EXPECT_FALSE(graph.calls[1].resolved);
}

TEST(PexAnalysis, CallGraphReportsPropertyAndVariableAccesses) {
  PexObject host = object("CounterScript", {state("", {function("OnInit", {
      step(kAssign, {identifier("Count"), integer(5)}),
      step(kPropGet, {identifier("Total"), identifier("self"), identifier("::temp1")}),
      step(kAssign, {identifier("::temp1"), identifier("Count")}),
      step(kReturn, {none()}),
  })})});
  PexVariable count;
  count.name = "Count";
  count.type = "Int";
  host.variables.push_back(count);
  PexProperty total;
  total.name = "Total";
  total.type = "Int";
  host.properties.push_back(total);

  PexFile file;
  file.sourceFileName = "CounterScript.psc";
  file.objects.push_back(host);

  const std::vector<const PexFile*> files{&file};
  const auto graph = fo4::pex_analysis::build_call_graph(files);
  ASSERT_EQ(graph.scripts.size(), 1u);
  const auto& accesses = graph.scripts[0].accesses;
  ASSERT_EQ(accesses.size(), 3u);
  EXPECT_EQ(accesses[0].name, "Count");
  EXPECT_FALSE(accesses[0].isProperty);
  EXPECT_EQ(accesses[0].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[0].instructionIndex, 0u);
  EXPECT_TRUE(accesses[1].isProperty);
  EXPECT_EQ(accesses[1].name, "Total");
  EXPECT_EQ(accesses[1].kind, PexAccessKind::Read);
  EXPECT_EQ(accesses[2].name, "Count");
  EXPECT_EQ(accesses[2].kind, PexAccessKind::Read);
}

TEST(PexAnalysis, CallGraphMarksArrayAndPropertyResultOperandsAsWrites) {
  PexObject host = object("ListScript", {state("", {function("OnInit", {
      step(kArrayLength, {identifier("Count"), identifier("Items")}),
      step(kArrayGetElement, {identifier("Found"), identifier("Items"), identifier("Count")}),
      step(kArrayFindElement, {identifier("Items"), identifier("Slot"), identifier("Found"),
                               integer(0)}),
      step(kPropGet, {identifier("Total"), identifier("self"), identifier("Count")}),
      step(kPropSet, {identifier("Total"), identifier("Items"), identifier("Count")}),
      step(kArrayRemove, {identifier("Items"), identifier("Count"), identifier("Slot")}),
      step(kStructGet, {identifier("Found"), identifier("Items"), text("Count")}),
      step(kArrayFindStruct, {identifier("Items"), identifier("Slot"), text("Count"),
                              identifier("Found"), integer(0)}),
      step(kReturn, {none()}),
  })})});
  for (const char* name : {"Count", "Items", "Found", "Slot"}) {
    PexVariable variable;
    variable.name = name;
    variable.type = "Int";
    host.variables.push_back(variable);
  }
  PexProperty total;
  total.name = "Total";
  total.type = "Int";
  host.properties.push_back(total);

  PexFile file;
  file.sourceFileName = "ListScript.psc";
  file.objects.push_back(host);

  const std::vector<const PexFile*> files{&file};
  const auto graph = fo4::pex_analysis::build_call_graph(files);
  ASSERT_EQ(graph.scripts.size(), 1u);
  const auto& accesses = graph.scripts[0].accesses;
  ASSERT_EQ(accesses.size(), 21u);
  EXPECT_EQ(accesses[0].name, "Count");
  EXPECT_EQ(accesses[0].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[2].name, "Found");
  EXPECT_EQ(accesses[2].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[6].name, "Slot");
  EXPECT_EQ(accesses[6].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[9].name, "Count");
  EXPECT_EQ(accesses[9].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[9].instructionIndex, 3u);
  EXPECT_EQ(accesses[11].name, "Items");
  EXPECT_EQ(accesses[11].kind, PexAccessKind::Read);
  EXPECT_EQ(accesses[11].instructionIndex, 4u);
  EXPECT_EQ(accesses[12].name, "Count");
  EXPECT_EQ(accesses[12].kind, PexAccessKind::Read);
  EXPECT_EQ(accesses[15].name, "Slot");
  EXPECT_EQ(accesses[15].kind, PexAccessKind::Read);
  EXPECT_EQ(accesses[15].instructionIndex, 5u);
  EXPECT_EQ(accesses[16].name, "Found");
  EXPECT_EQ(accesses[16].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[16].instructionIndex, 6u);
  EXPECT_EQ(accesses[17].name, "Items");
  EXPECT_EQ(accesses[17].kind, PexAccessKind::Read);
  EXPECT_EQ(accesses[19].name, "Slot");
  EXPECT_EQ(accesses[19].kind, PexAccessKind::Write);
  EXPECT_EQ(accesses[19].instructionIndex, 7u);
  EXPECT_EQ(accesses[20].name, "Found");
  EXPECT_EQ(accesses[20].kind, PexAccessKind::Read);
}

TEST(PexAnalysis, SummaryReportsEventsTransitionsAndProperties) {
  auto onActivate = function("OnActivate", {
                                                step(kCallMethod, {identifier("GoToState"),
                                                                   identifier("self"),
                                                                   identifier("::nonevar"),
                                                                   text("Busy")}),
                                                step(kCallMethod, {identifier("GoToState"),
                                                                   identifier("self"),
                                                                   identifier("::nonevar"),
                                                                   identifier("NextState")}),
                                                step(kPropGet, {identifier("Total"),
                                                                identifier("self"),
                                                                identifier("::temp1")}),
                                                step(kAssign, {identifier("Count"),
                                                               integer(1)}),
                                                step(kReturn, {none()}),
                                            });
  auto onInit = function("OnInit", {step(kReturn, {none()})});

  PexObject host =
      object("StateScript", {state("", {onActivate}), state("Busy", {onInit})});
  PexVariable count;
  count.name = "Count";
  count.type = "Int";
  host.variables.push_back(count);
  PexProperty total;
  total.name = "Total";
  total.type = "Int";
  host.properties.push_back(total);

  PexFile file;
  file.sourceFileName = "StateScript.psc";
  file.objects.push_back(host);

  const std::vector<const PexFile*> files{&file};
  const auto graph = fo4::pex_analysis::build_call_graph(files);
  const auto summary = fo4::pex_analysis::summarize_script(graph, 0, file);

  ASSERT_EQ(summary.objects.size(), 1u);
  const auto& entry = summary.objects[0];
  EXPECT_EQ(entry.name, "StateScript");
  ASSERT_EQ(entry.events.size(), 2u);
  EXPECT_EQ(entry.events[0].function.functionName, "OnActivate");
  EXPECT_EQ(entry.events[0].calls, (std::vector<std::string>{"GoToState"}));
  EXPECT_EQ(entry.events[1].function.stateName, "Busy");

  ASSERT_EQ(entry.transitions.size(), 2u);
  EXPECT_EQ(entry.transitions[0].targetState, "Busy");
  EXPECT_TRUE(entry.transitions[0].literal);
  EXPECT_TRUE(entry.transitions[0].knownState);
  EXPECT_EQ(entry.transitions[1].targetState, "NextState");
  EXPECT_FALSE(entry.transitions[1].literal);
  EXPECT_FALSE(entry.transitions[1].knownState);
  EXPECT_EQ(entry.referencedProperties, (std::vector<std::string>{"Total"}));
  EXPECT_EQ(entry.writtenVariables, (std::vector<std::string>{"Count"}));
}

TEST(PexAnalysis, FragmentResolutionMapsStageToPexFunction) {
  PexFile file;
  file.sourceFileName = "QF_Test.psc";
  file.objects.push_back(object("QF_Test", {state("", {function("Fragment_Stage_0010",
                                                              {step(kReturn, {none()})})})}));

  fo4::QuestVmadFragment hit;
  hit.stage = 10;
  hit.stage_index = 0;
  hit.script = "QF_Test";
  hit.function = "Fragment_Stage_0010";
  fo4::QuestVmadFragment miss;
  miss.stage = 20;
  miss.script = "QF_Test";
  miss.function = "Fragment_Stage_0020";
  fo4::QuestVmadFragment other;
  other.stage = 30;
  other.script = "QF_Absent";
  other.function = "Fragment_Stage_0030";
  const std::vector<fo4::QuestVmadFragment> fragments{hit, miss, other};

  const auto targets = fo4::pex_analysis::resolve_fragments(file, fragments);
  ASSERT_EQ(targets.size(), 3u);
  EXPECT_TRUE(targets[0].scriptFound);
  EXPECT_TRUE(targets[0].functionFound);
  EXPECT_EQ(targets[0].stage, 10u);
  EXPECT_EQ(targets[0].target.objectName, "QF_Test");
  EXPECT_EQ(targets[0].target.functionName, "Fragment_Stage_0010");
  EXPECT_EQ(fo4::pex_analysis::pex_function_at(file, targets[0].target)->name,
            "Fragment_Stage_0010");

  EXPECT_TRUE(targets[1].scriptFound);
  EXPECT_FALSE(targets[1].functionFound);
  EXPECT_EQ(targets[1].target.slot, PexFunctionSlot::State);
  EXPECT_EQ(targets[1].target.objectIndex, fo4::pex_analysis::kNoIndex);

  EXPECT_FALSE(targets[2].scriptFound);
  EXPECT_FALSE(targets[2].functionFound);
  EXPECT_EQ(targets[2].script, "QF_Absent");
}

TEST(PexAnalysis, FunctionRefPointsAtHandlerAndStateFunctions) {
  PexObject host = object("HandlerScript", {state("", {function("OnInit",
                                                               {step(kReturn, {none()})})})});
  PexProperty total;
  total.name = "Total";
  total.type = "Int";
  total.flags = 0x03;
  total.readHandler = function("get_Total", {step(kReturn, {none()})});
  total.writeHandler = function("set_Total", {step(kReturn, {none()})});
  host.properties.push_back(total);

  PexFile file;
  file.objects.push_back(host);

  const std::vector<const PexFile*> files{&file};
  const auto graph = fo4::pex_analysis::build_call_graph(files);
  ASSERT_EQ(graph.scripts[0].functions.size(), 3u);

  const PexFunctionRef& readRef = graph.scripts[0].functions[1];
  EXPECT_EQ(readRef.slot, PexFunctionSlot::PropertyRead);
  EXPECT_EQ(readRef.functionName, "Total");
  ASSERT_NE(fo4::pex_analysis::pex_function_at(file, readRef), nullptr);
  EXPECT_EQ(fo4::pex_analysis::pex_function_at(file, readRef)->name, "get_Total");

  const PexFunctionRef& writeRef = graph.scripts[0].functions[2];
  EXPECT_EQ(writeRef.slot, PexFunctionSlot::PropertyWrite);
  EXPECT_EQ(fo4::pex_analysis::pex_function_at(file, writeRef)->name, "set_Total");
  EXPECT_EQ(fo4::pex_analysis::pex_function_at(file, graph.scripts[0].functions[0])->name,
            "OnInit");
}
