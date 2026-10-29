#include "abc_test_support.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/abc/abc_text.hpp"

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

std::uint64_t fnv1a(const std::string& text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const char character : text) {
    hash ^= static_cast<unsigned char>(character);
    hash *= 1099511628211ULL;
  }
  return hash;
}

}  // namespace

TEST(AbcText, RendersEverySectionOfTheSyntheticFixture) {
  const AbcFile file = parse_or_fail(fo4::test::build_abc());
  const std::string text = fo4::abc::render_abc_text(file);
  EXPECT_NE(text.find("abc 46.16\n"), std::string::npos);
  EXPECT_NE(text.find("strings "), std::string::npos);
  EXPECT_NE(text.find("ints "), std::string::npos);
  EXPECT_NE(text.find("uints "), std::string::npos);
  EXPECT_NE(text.find("doubles "), std::string::npos);
  EXPECT_NE(text.find("namespaces "), std::string::npos);
  EXPECT_NE(text.find("ns_sets "), std::string::npos);
  EXPECT_NE(text.find("multinames "), std::string::npos);
  EXPECT_NE(text.find("metadata "), std::string::npos);
  EXPECT_NE(text.find("methods "), std::string::npos);
  EXPECT_NE(text.find("instances "), std::string::npos);
  EXPECT_NE(text.find("classes "), std::string::npos);
  EXPECT_NE(text.find("scripts "), std::string::npos);
  EXPECT_NE(text.find("  body 0 method "), std::string::npos);
  EXPECT_NE(text.find("    code\n"), std::string::npos);
  EXPECT_NE(text.find("    end_code\n"), std::string::npos);
  EXPECT_NE(text.find("    exceptions "), std::string::npos);
  EXPECT_NE(text.find("end\n"), std::string::npos);
  EXPECT_EQ(text.find("decode failed"), std::string::npos);
}

TEST(AbcText, RenderingIsDeterministicAcrossParses) {
  const std::vector<std::uint8_t> bytes = fo4::test::build_abc();
  const std::string first = fo4::abc::render_abc_text(parse_or_fail(bytes));
  std::vector<std::uint8_t> serialized;
  const AbcFile file = parse_or_fail(bytes);
  ASSERT_EQ(file.serialize(serialized), AbcStatus::Ok);
  const std::string second = fo4::abc::render_abc_text(parse_or_fail(serialized));
  EXPECT_EQ(first, second);
  EXPECT_EQ(fnv1a(first), fnv1a(second));
}

TEST(AbcText, EditedPoolValueAppearsInTheText) {
  AbcFile file = parse_or_fail(fo4::test::build_abc());
  const std::string before = fo4::abc::render_abc_text(file);
  ASSERT_FALSE(file.pool.strings.empty());
  file.pool.strings[1] = "RadicleEdited";
  const std::string after = fo4::abc::render_abc_text(file);
  EXPECT_NE(after.find("RadicleEdited"), std::string::npos);
  EXPECT_EQ(before == after, false);
  EXPECT_EQ(fo4::abc::render_abc_text(file), after);
}

TEST(AbcText, BranchTargetsRenderAsLabels) {
  AbcFile file = parse_or_fail(fo4::test::build_abc());
  const std::string text = fo4::abc::render_abc_text(file);
  bool has_label_definition = false;
  std::size_t position = 0;
  while ((position = text.find("L0:\n", position)) != std::string::npos) {
    has_label_definition = true;
    break;
  }
  if (text.find("-> L") != std::string::npos || text.find("switch ") != std::string::npos) {
    EXPECT_TRUE(has_label_definition) << text.substr(0, 4000);
  }
}
