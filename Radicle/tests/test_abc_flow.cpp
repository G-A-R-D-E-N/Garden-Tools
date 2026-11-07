#include <fo4/abc/abc.hpp>
#include <fo4/abc/flow.hpp>
#include <fo4/abc/opcodes.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using fo4::abc::AbcControlFlowGraph;
using fo4::abc::AbcEdgeKind;
using fo4::abc::AbcFlowIssue;
using fo4::abc::AbcFlowStatus;
using fo4::abc::AbcInstruction;
using fo4::abc::AbcOperandRole;
using fo4::abc::AbcStatus;

struct Built {
  AbcStatus decode{AbcStatus::Ok};
  AbcFlowStatus status{AbcFlowStatus::Ok};
  AbcFlowIssue issue;
  AbcControlFlowGraph graph;
  std::vector<AbcInstruction> instructions;
};

Built build(const std::vector<std::uint8_t>& code,
            const std::vector<std::uint32_t>& entries = {}) {
  Built built;
  built.decode = fo4::abc::decode_code(code, built.instructions);
  if (built.decode != AbcStatus::Ok) {
    built.status = AbcFlowStatus::DecodeFailed;
    return built;
  }
  built.status = fo4::abc::build_control_flow(built.instructions, entries, built.graph,
                                              built.issue);
  return built;
}

bool has_edge(const AbcControlFlowGraph& graph, std::size_t block, std::uint32_t target,
              AbcEdgeKind kind) {
  for (const auto& edge : graph.blocks[block].successors) {
    if (edge.target == target && edge.kind == kind) {
      return true;
    }
  }
  return false;
}

struct OperandCase {
  const char* name;
  AbcOperandRole role;
  std::vector<std::uint8_t> bytes;
};

}  // namespace

TEST(AbcFlow, SplitsAConditionalBranchIntoThreeBlocks) {
  const auto built = build({0x26, 0x12, 0x02, 0x00, 0x00, 0x24, 0x01, 0x47});
  ASSERT_EQ(built.decode, AbcStatus::Ok) << fo4::abc::status_name(built.decode);
  ASSERT_EQ(built.status, AbcFlowStatus::Ok) << built.issue.message;
  EXPECT_EQ(built.graph.code_len, 8u);
  ASSERT_EQ(built.graph.blocks.size(), 3u);
  EXPECT_EQ(built.graph.blocks[0].start_offset, 0u);
  EXPECT_EQ(built.graph.blocks[0].end_offset, 5u);
  EXPECT_TRUE(has_edge(built.graph, 0, 7, AbcEdgeKind::Branch)) << built.issue.message;
  EXPECT_TRUE(has_edge(built.graph, 0, 5, AbcEdgeKind::Fallthrough));
  for (const auto& block : built.graph.blocks) {
    EXPECT_TRUE(block.reachable) << "block " << block.index;
  }
  EXPECT_EQ(built.graph.unreachable_block_count(), 0u);
}

TEST(AbcFlow, MarksCodeAfterAReturnAsUnreachable) {
  const auto built = build({0x47, 0x24, 0x01, 0x47});
  ASSERT_EQ(built.status, AbcFlowStatus::Ok) << built.issue.message;
  ASSERT_EQ(built.graph.blocks.size(), 2u);
  EXPECT_TRUE(built.graph.blocks[0].reachable);
  EXPECT_FALSE(built.graph.blocks[1].reachable);
  EXPECT_EQ(built.graph.unreachable_block_count(), 1u);
}

TEST(AbcFlow, KeepsASelfJumpTerminating) {
  const auto built = build({0x10, 0xFC, 0xFF, 0xFF});
  ASSERT_EQ(built.status, AbcFlowStatus::Ok) << built.issue.message;
  ASSERT_EQ(built.graph.blocks.size(), 1u);
  ASSERT_EQ(built.graph.blocks[0].successors.size(), 1u);
  EXPECT_EQ(built.graph.blocks[0].successors[0].target, 0u);
  EXPECT_EQ(built.graph.blocks[0].successors[0].block, 0u);
  EXPECT_EQ(built.graph.blocks[0].successors[0].kind, AbcEdgeKind::Branch);
}

TEST(AbcFlow, RejectsABranchIntoTheMiddleOfAnInstruction) {
  const auto built = build({0x10, 0xFD, 0xFF, 0xFF, 0x47});
  EXPECT_EQ(built.status, AbcFlowStatus::TargetInsideInstruction);
  EXPECT_EQ(built.issue.offset, 0u);
  EXPECT_NE(built.issue.message.find("inside an instruction"), std::string::npos)
      << built.issue.message;
}

TEST(AbcFlow, RejectsAJumpPastTheLastInstruction) {
  const auto built = build({0x10, 0x01, 0x00, 0x00, 0x47});
  ASSERT_EQ(built.decode, AbcStatus::Ok) << "the decoder accepts a jump to the end of the body";
  EXPECT_EQ(built.status, AbcFlowStatus::TargetOutsideCode);
  EXPECT_EQ(built.issue.offset, 0u);
}

TEST(AbcFlow, ResolvesLookupSwitchTargetsFromTheOpcodeAddress) {
  const auto built = build({0x1B, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x47});
  ASSERT_EQ(built.status, AbcFlowStatus::Ok) << built.issue.message;
  ASSERT_EQ(built.graph.blocks.size(), 2u);
  EXPECT_TRUE(has_edge(built.graph, 0, 0, AbcEdgeKind::Switch));
  EXPECT_TRUE(has_edge(built.graph, 0, 8, AbcEdgeKind::Switch));
  EXPECT_FALSE(has_edge(built.graph, 0, 8, AbcEdgeKind::Fallthrough));
}

TEST(AbcFlow, MarksAnExceptionEntryItsOwnBlock) {
  const auto built = build({0x47, 0x29, 0x47}, {1});
  ASSERT_EQ(built.status, AbcFlowStatus::Ok) << built.issue.message;
  ASSERT_EQ(built.graph.blocks.size(), 2u);
  EXPECT_EQ(built.graph.blocks[1].start_offset, 1u);
  EXPECT_TRUE(built.graph.blocks[1].reachable);
  EXPECT_EQ(built.graph.block_index_at(1), 1u);
  EXPECT_EQ(built.graph.block_index_at(2), fo4::abc::kNoFlowIndex);
}

TEST(AbcFlow, RejectsAnEntryThatIsNotAnInstructionStart) {
  const auto built = build({0x24, 0x01, 0x47}, {1});
  EXPECT_EQ(built.status, AbcFlowStatus::TargetOutsideCode);
  EXPECT_EQ(built.issue.offset, 1u);
}

TEST(AbcFlow, RejectsALayoutThatIsNotContiguous) {
  std::vector<AbcInstruction> code(2);
  code[0].offset = 0;
  code[0].opcode = 0x02;
  code[0].mnemonic = "nop";
  code[0].length = 1;
  code[1].offset = 4;
  code[1].opcode = 0x47;
  code[1].mnemonic = "returnvoid";
  code[1].length = 1;
  AbcControlFlowGraph graph;
  AbcFlowIssue issue;
  EXPECT_EQ(fo4::abc::build_control_flow(code, {}, graph, issue), AbcFlowStatus::InstructionLayout);
  EXPECT_EQ(issue.offset, 4u);
}

/* Invariant: every operand kind decodes to one instruction and re-encodes to the same bytes. */
TEST(AbcFlow, DecodesAndReencodesOneFixturePerOperandKind) {
  const std::vector<OperandCase> cases{
      {"none", AbcOperandRole::None, {0x02}},
      {"multiname", AbcOperandRole::Multiname, {0x60, 0x01}},
      {"string", AbcOperandRole::String, {0x2C, 0x01}},
      {"namespace", AbcOperandRole::Namespace, {0x31, 0x01}},
      {"method", AbcOperandRole::Method, {0x40, 0x01}},
      {"class", AbcOperandRole::Class, {0x58, 0x01}},
      {"int", AbcOperandRole::Int, {0x2D, 0x01}},
      {"uint", AbcOperandRole::UInt, {0x2E, 0x01}},
      {"double", AbcOperandRole::Double, {0x2F, 0x01}},
      {"count", AbcOperandRole::Count, {0x53, 0x01}},
      {"immediate", AbcOperandRole::Immediate, {0x24, 0x01}},
      {"offset", AbcOperandRole::Offset, {0x10, 0x00, 0x00, 0x00}},
      {"switch", AbcOperandRole::Switch,
       {0x1B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  };
  for (const OperandCase& one : cases) {
    std::vector<AbcInstruction> instructions;
    ASSERT_EQ(fo4::abc::decode_code(one.bytes, instructions),
              AbcStatus::Ok) << one.name;
    ASSERT_EQ(instructions.size(), 1u) << one.name;
    if (one.role == AbcOperandRole::None) {
      EXPECT_TRUE(instructions[0].operands.empty()) << one.name;
    } else {
      ASSERT_FALSE(instructions[0].operands.empty()) << one.name;
      EXPECT_EQ(instructions[0].operands[0].role, one.role) << one.name;
    }
    std::vector<std::uint8_t> reencoded;
    ASSERT_EQ(fo4::abc::encode_code(instructions, reencoded),
              AbcStatus::Ok) << one.name;
    EXPECT_EQ(reencoded, one.bytes) << one.name;
  }
}

TEST(AbcFlow, EncodesABranchFromItsAbsoluteTarget) {
  std::vector<AbcInstruction> code(2);
  code[0].offset = 0;
  code[0].opcode = 0x10;
  code[0].mnemonic = "jump";
  code[0].length = 4;
  fo4::abc::AbcDecodedOperand offset;
  offset.role = AbcOperandRole::Offset;
  offset.target = 8;
  code[0].operands.push_back(offset);
  code[1].offset = 4;
  code[1].opcode = 0x47;
  code[1].mnemonic = "returnvoid";
  code[1].length = 1;
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(fo4::abc::encode_code(code, bytes), AbcStatus::Ok);
  const std::vector<std::uint8_t> expected{0x10, 0x04, 0x00, 0x00, 0x47};
  EXPECT_EQ(bytes, expected);
}

TEST(AbcFlow, RejectsAnInstructionWithAMissingOperand) {
  std::vector<AbcInstruction> code(1);
  code[0].offset = 0;
  code[0].opcode = 0x2C;
  code[0].mnemonic = "pushstring";
  code[0].length = 1;
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(fo4::abc::encode_code(code, bytes), AbcStatus::InconsistentModel);
}
