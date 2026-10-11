#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> nested_groups_plugin(std::size_t levels) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  std::vector<std::size_t> offsets;
  for (std::size_t i = 0; i < levels; ++i) {
    offsets.push_back(fo4::test::begin_group(out, "????", 1));
  }
  for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
    fo4::test::end_group(out, *it);
  }
  return out.bytes;
}

}  // namespace

TEST(ReaderLimits, SixteenLevelsOfGroupNestingStillParse) {
  const auto plugin = fo4::read_plugin(nested_groups_plugin(16));
  EXPECT_EQ(fo4::collect_groups(plugin).size(), 16u);
}

TEST(ReaderLimits, SeventeenLevelsOfGroupNestingAreRefused) {
  EXPECT_THROW(fo4::read_plugin(nested_groups_plugin(17)), fo4::parse_error);
}

TEST(ReaderLimits, TwentyNestedGroupsAreRefusedCleanly) {
  EXPECT_THROW(fo4::read_plugin(nested_groups_plugin(20)), fo4::parse_error);
}

TEST(ReaderLimits, CompressedUncompressedSizeBeyondZlibBoundIsRefusedBeforeAllocation) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000801);
  out.u32(0xFFFFFFFFu);
  out.raw(std::string_view{"tiny"});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  try {
    fo4::read_plugin(out.bytes);
    FAIL() << "expected parse_error";
  } catch (const fo4::parse_error& error) {
    EXPECT_NE(error.parse_message().find("zlib expansion bound"), std::string::npos);
  }
}
