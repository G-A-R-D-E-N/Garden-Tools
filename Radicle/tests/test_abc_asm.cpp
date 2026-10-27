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
  const std::size_t position = text.find(" " + mnemonic + "\n");
  ASSERT_NE(position, std::string::npos);
  text.replace(position, mnemonic.size() + 2, " notanopcode\n");
  AbcFile assembled;
  fo4::abc::AbcTextError error;
  EXPECT_EQ(fo4::abc::assemble_abc_text(text, assembled, &error), AbcStatus::InconsistentModel);
  EXPECT_FALSE(error.message.empty());
}
