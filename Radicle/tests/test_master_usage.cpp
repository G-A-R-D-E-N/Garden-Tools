#include "fo4/plugin/master_usage.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace {

fo4::Plugin plugin_with_three_masters() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  for (const std::string_view master : {std::string_view{"Fallout4.esm\0", 13},
                                        std::string_view{"Unused.esp\0", 11},
                                        std::string_view{"Linked.esp\0", 11}}) {
    fo4::test::write_subrecord(out, "MAST", master);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto override_record = fo4::test::begin_record(out, "MISC", 0, 0x00012345);
  fo4::test::end_record(out, override_record);
  const auto own = fo4::test::begin_record(out, "MISC", 0, 0x03000800);
  fo4::test::ByteWriter ksiz;
  ksiz.u32(1);
  fo4::test::write_subrecord(out, "KSIZ", ksiz.bytes);
  fo4::test::ByteWriter kwda;
  kwda.u32(0x02000ABC);
  fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
  fo4::test::end_record(out, own);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Patch.esp");
}

}  // namespace

TEST(MasterUsage, CountsOverridesAndLinksPerMaster) {
  auto plugin = plugin_with_three_masters();
  const auto usage = fo4::scan_master_usage(plugin);
  ASSERT_EQ(usage.references.size(), 3u);
  EXPECT_EQ(usage.references[0], 1u);
  EXPECT_EQ(usage.references[1], 0u);
  EXPECT_EQ(usage.references[2], 1u);
  EXPECT_TRUE(usage.complete());
}

TEST(MasterUsage, DroppingUnusedMasterKeepsEveryLink) {
  auto plugin = plugin_with_three_masters();
  const auto kept = fo4::used_masters(plugin, fo4::scan_master_usage(plugin));
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[0].key.filename(), "Fallout4.esm");
  EXPECT_EQ(kept[1].key.filename(), "Linked.esp");

  const auto cleaned = fo4::remap_plugin_masters(plugin, kept);
  const auto reread = fo4::read_plugin(fo4::write_plugin(cleaned), "Patch.esp");
  ASSERT_EQ(reread.header.masters.size(), 2u);
  EXPECT_NE(fo4::find_record(reread, fo4::FormKey::parse("012345:Fallout4.esm")), nullptr);
  const auto* own = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(own, nullptr);
  const auto usage = fo4::scan_master_usage(reread);
  EXPECT_EQ(usage.references[1], 1u);
}
