#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_asm.hpp"
#include "fo4/abc/opcodes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using fo4::abc::AbcInstruction;
using fo4::abc::AbcStatus;

TEST(AbcAsmPushshort, AssemblesTheSixteenBitTwosComplementU30Form) {
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(fo4::abc::assemble_instruction_text("pushshort -5", bytes), AbcStatus::Ok);
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x25, 0xFB, 0xFF, 0x03}));
  ASSERT_EQ(fo4::abc::assemble_instruction_text("pushshort 300", bytes), AbcStatus::Ok);
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x25, 0xAC, 0x02}));
}

TEST(AbcAsmPushshort, CanonicalFormDecodesBackToTheSameImmediate) {
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(fo4::abc::assemble_instruction_text("pushshort -5", bytes), AbcStatus::Ok);
  std::vector<AbcInstruction> decoded;
  ASSERT_EQ(fo4::abc::decode_code(bytes, decoded), AbcStatus::Ok);
  ASSERT_EQ(decoded.size(), 1u);
  ASSERT_EQ(decoded[0].operands.size(), 1u);
  EXPECT_EQ(decoded[0].operands[0].immediate, -5);
  EXPECT_EQ(decoded[0].length, 4u);
}

TEST(AbcAsmPushshort, RefusesAnImmediateThatDoesNotFitSixteenBits) {
  std::vector<std::uint8_t> bytes{0x01};
  EXPECT_EQ(fo4::abc::assemble_instruction_text("pushshort 32768", bytes),
            AbcStatus::InconsistentModel);
  EXPECT_TRUE(bytes.empty());
  EXPECT_EQ(fo4::abc::assemble_instruction_text("pushshort -32769", bytes),
            AbcStatus::InconsistentModel);
  EXPECT_TRUE(bytes.empty());
  EXPECT_EQ(fo4::abc::assemble_instruction_text("pushshort 65536", bytes),
            AbcStatus::InconsistentModel);
  EXPECT_TRUE(bytes.empty());
}

TEST(AbcAsmPushshort, WideSignExtendedFormKeepsItsFiveBytes) {
  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(fo4::abc::assemble_instruction_text("pushshort 4294967291", bytes), AbcStatus::Ok);
  EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x25, 0xFB, 0xFF, 0xFF, 0xFF, 0x0F}));
  std::vector<AbcInstruction> decoded;
  ASSERT_EQ(fo4::abc::decode_code(bytes, decoded), AbcStatus::Ok);
  ASSERT_EQ(decoded.size(), 1u);
  ASSERT_EQ(decoded[0].operands.size(), 1u);
  EXPECT_EQ(decoded[0].operands[0].immediate, -5);
  EXPECT_EQ(decoded[0].length, 6u);
  std::vector<std::uint8_t> reencoded;
  ASSERT_EQ(fo4::abc::encode_code(decoded, reencoded), AbcStatus::Ok);
  EXPECT_EQ(reencoded, bytes);
}
