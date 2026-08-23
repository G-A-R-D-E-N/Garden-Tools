#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <string_view>

namespace {

std::vector<std::byte> make_nested_plugin(std::uint32_t stored_count) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(stored_count);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "MISC", 0);
  const auto first = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"First\0", 6});
  fo4::test::end_record(out, first);

  const auto nested = fo4::test::begin_group(out, "MISC", 1);
  const auto second = fo4::test::begin_record(out, "MISC", 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Second\0", 7});
  fo4::test::end_record(out, second);
  fo4::test::end_group(out, nested);
  fo4::test::end_group(out, top);
  return out.bytes;
}

}

TEST(HedrCount, CleanPluginPreservesOriginalBytesEvenWhenStoredCountIsStale) {
  const auto bytes = make_nested_plugin(999);
  const auto plugin = fo4::read_plugin(bytes, "StaleButClean.esp");
  ASSERT_EQ(plugin.header.record_count, 999u);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(HedrCount, MixedPathPreservesNoncanonicalCountWhenStructureIsUnchanged) {
  const auto bytes = make_nested_plugin(999);
  auto plugin = fo4::read_plugin(bytes, "StaleButMixed.esp");
  plugin.original_range.clear();

  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(HedrCount, DirtyRebuildCountsAllGroupsAndNonTes4RecordsRecursively) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Rebuilt.esp");

  // xEdit's writer counts every major record as 1 and each GRUP as
  // 1 + its recursive children, then subtracts one for TES4 itself.
  // This fixture therefore has: top GRUP + first record + nested GRUP +
  // second record = 4.
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "Rebuilt.esp");
  EXPECT_EQ(reread.header.record_count, 4u);
  EXPECT_EQ(fo4::collect_records(reread).size(), 3u);  // TES4 + two MISC
  EXPECT_EQ(fo4::collect_groups(reread).size(), 2u);
}
