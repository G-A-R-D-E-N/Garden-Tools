#include "abc_test_support.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_asm.hpp"
#include "fo4/abc/abc_text.hpp"
#include "fo4/abc/opcodes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using fo4::abc::AbcFile;
using fo4::abc::AbcStatus;

namespace {

AbcFile parse_or_fail(const std::vector<std::uint8_t>& bytes) {
  AbcFile file;
  EXPECT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  return file;
}

bool branch_free_text(const fo4::abc::AbcInstruction& instruction, std::string& out) {
  out = instruction.mnemonic;
  for (const fo4::abc::AbcDecodedOperand& operand : instruction.operands) {
    switch (operand.role) {
      case fo4::abc::AbcOperandRole::Offset:
      case fo4::abc::AbcOperandRole::Switch:
        return false;
      case fo4::abc::AbcOperandRole::None:
        break;
      case fo4::abc::AbcOperandRole::Count:
        out += " " + std::to_string(operand.value);
        break;
      case fo4::abc::AbcOperandRole::Immediate:
        out += " " + std::to_string(operand.immediate);
        break;
      default:
        out += " @" + std::to_string(operand.value);
        break;
    }
  }
  return true;
}

}  // namespace

TEST(AbcAsm, DumpAssembleSerializeRoundTripsTheSyntheticFixtureByteForByte) {
  const std::vector<std::uint8_t> bytes = fo4::test::build_abc();
  const AbcFile parsed = parse_or_fail(bytes);
  const std::string text = fo4::abc::render_abc_text(parsed);

  AbcFile assembled;
  fo4::abc::AbcTextError error;
  ASSERT_EQ(fo4::abc::assemble_abc_text(text, assembled, &error), AbcStatus::Ok)
      << error.message << " at line " << error.line;

  std::vector<std::uint8_t> serialized;
  ASSERT_EQ(assembled.serialize(serialized), AbcStatus::Ok);
  EXPECT_EQ(serialized, bytes);
}

TEST(AbcAsm, AssembledModelRendersTheSameTextAgain) {
  const AbcFile parsed = parse_or_fail(fo4::test::build_abc());
  const std::string text = fo4::abc::render_abc_text(parsed);
  AbcFile assembled;
  ASSERT_EQ(fo4::abc::assemble_abc_text(text, assembled), AbcStatus::Ok);
  EXPECT_EQ(fo4::abc::render_abc_text(assembled), text);
}

TEST(AbcAsm, EditedPoolValueSurvivesAssembleAndSerialize) {
  AbcFile file = parse_or_fail(fo4::test::build_abc());
  ASSERT_FALSE(file.pool.strings.empty());
  file.pool.strings[1] = "RadicleAssembled";
  const std::string text = fo4::abc::render_abc_text(file);

  AbcFile assembled;
  ASSERT_EQ(fo4::abc::assemble_abc_text(text, assembled), AbcStatus::Ok);
  std::vector<std::uint8_t> serialized;
  ASSERT_EQ(assembled.serialize(serialized), AbcStatus::Ok);
  const AbcFile reread = parse_or_fail(serialized);
  ASSERT_GT(reread.pool.strings.size(), 1u);
  EXPECT_EQ(reread.pool.strings[1], "RadicleAssembled");
}

TEST(AbcAsm, RejectsTextWithoutAVersionLine) {
  const AbcFile parsed = parse_or_fail(fo4::test::build_abc());
  std::string text = fo4::abc::render_abc_text(parsed);
  const std::size_t version = text.find("abc ");
  ASSERT_NE(version, std::string::npos);
  text.replace(version, text.find('\n', version) - version, "abc");
  AbcFile assembled;
  fo4::abc::AbcTextError error;
  EXPECT_EQ(fo4::abc::assemble_abc_text(text, assembled, &error), AbcStatus::InconsistentModel);
  EXPECT_FALSE(error.message.empty());
}

TEST(AbcAsm, RejectsAnUnknownOpcode) {
  AbcFile file = parse_or_fail(fo4::test::build_abc());
  ASSERT_FALSE(file.bodies.empty());
  ASSERT_FALSE(file.bodies[0].code.empty());
  const fo4::abc::AbcOpcodeInfo* info = fo4::abc::opcode_info(file.bodies[0].code[0]);
  ASSERT_NE(info, nullptr);
  const std::string mnemonic = info->mnemonic;
  std::string text = fo4::abc::render_abc_text(file);
  const std::size_t position = text.find(" " + mnemonic);
  ASSERT_NE(position, std::string::npos);
  text.replace(position, mnemonic.size() + 1, " notanopcode");
  AbcFile assembled;
  fo4::abc::AbcTextError error;
  EXPECT_EQ(fo4::abc::assemble_abc_text(text, assembled, &error), AbcStatus::InconsistentModel);
  EXPECT_FALSE(error.message.empty());
}

TEST(AbcAsm, AssemblesOneInstructionToItsOpcodeAndOperandBytes) {
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(fo4::abc::assemble_instruction_text("pushbyte -3", bytes), AbcStatus::Ok);
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x24, 0xFD}));
  ASSERT_EQ(fo4::abc::assemble_instruction_text("getlocal 1", bytes), AbcStatus::Ok);
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x62, 0x01}));
  ASSERT_EQ(fo4::abc::assemble_instruction_text("returnvoid", bytes), AbcStatus::Ok);
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x47}));
}

TEST(AbcAsm, EveryBranchFreeFixtureInstructionRoundTripsThroughTheDecoder) {
  const AbcFile file = parse_or_fail(fo4::test::build_abc());
  std::size_t checked = 0;
  for (const fo4::abc::AbcMethodBody& body : file.bodies) {
    std::vector<fo4::abc::AbcInstruction> instructions;
    ASSERT_EQ(fo4::abc::decode_code(body.code, instructions, body.exceptions), AbcStatus::Ok);
    for (const fo4::abc::AbcInstruction& instruction : instructions) {
      std::string text;
      if (!branch_free_text(instruction, text)) {
        continue;
      }
      std::vector<std::uint8_t> bytes;
      ASSERT_EQ(fo4::abc::assemble_instruction_text(text, bytes), AbcStatus::Ok) << text;
      std::vector<fo4::abc::AbcInstruction> reread;
      ASSERT_EQ(fo4::abc::decode_code(bytes, reread), AbcStatus::Ok) << text;
      ASSERT_EQ(reread.size(), 1u) << text;
      std::string reread_text;
      ASSERT_TRUE(branch_free_text(reread[0], reread_text)) << text;
      EXPECT_EQ(reread_text, text);
      ++checked;
    }
  }
  EXPECT_GT(checked, 0u);
}

TEST(AbcAsm, RefusesInstructionTextThatIsNotOneBranchFreeInstruction) {
  const char* const refused[] = {"", "notanopcode", "getlocal", "returnvoid 1", "jump -> L0",
                                 "pushbyte 300"};
  for (const char* text : refused) {
    std::vector<std::uint8_t> bytes{0x01};
    fo4::abc::AbcTextError error;
    EXPECT_EQ(fo4::abc::assemble_instruction_text(text, bytes, &error),
              AbcStatus::InconsistentModel)
        << text;
    EXPECT_FALSE(error.message.empty()) << text;
    EXPECT_EQ(error.line, 1u) << text;
    EXPECT_TRUE(bytes.empty()) << text;
  }
}

TEST(AbcAsm, DeadBranchTargetsRenderAsAbsoluteOffsetsAndRoundTripByteForByte) {
  // Regression: a dead target past the end or mid-instruction must print =offset, not a bare label.
  struct DeadCode {
    std::vector<std::uint8_t> code;
    std::string reference;
  };
  const std::vector<DeadCode> cases{
      {{0x47, 0x10, 0xE8, 0x03, 0x00}, "-> =1005"},
      {{0x47, 0x10, 0xFE, 0xFF, 0xFF}, "-> =3"},
  };
  for (const DeadCode& dead : cases) {
    AbcFile file = parse_or_fail(fo4::test::build_abc());
    ASSERT_FALSE(file.bodies.empty());
    file.bodies[0].code = dead.code;
    std::vector<std::uint8_t> expected;
    ASSERT_EQ(file.serialize(expected), AbcStatus::Ok);

    const std::string text = fo4::abc::render_abc_text(file);
    EXPECT_NE(text.find(dead.reference), std::string::npos) << text;
    AbcFile assembled;
    fo4::abc::AbcTextError error;
    ASSERT_EQ(fo4::abc::assemble_abc_text(text, assembled, &error), AbcStatus::Ok)
        << error.message << " at line " << error.line;
    ASSERT_FALSE(assembled.bodies.empty());
    EXPECT_EQ(assembled.bodies[0].code, dead.code);
    std::vector<std::uint8_t> serialized;
    ASSERT_EQ(assembled.serialize(serialized), AbcStatus::Ok);
    EXPECT_EQ(serialized, expected);
  }
}
