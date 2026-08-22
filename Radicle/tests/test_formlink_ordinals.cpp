#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MasterA.esm\0", 12});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"MasterB.esm\0", 12});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x02000800);
  fo4::test::ByteWriter first;
  first.u32(0x00000123);
  fo4::test::write_subrecord(out, "NAME", first.bytes);
  const std::array<std::byte, 4> unrelated{
      std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  fo4::test::write_subrecord(out, "ZZZZ", unrelated);
  fo4::test::ByteWriter second;
  second.u32(0x01000456);
  fo4::test::write_subrecord(out, "NAME", second.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

}

TEST(FormLinkOrdinal, RepeatedSubrecordsHaveStableOccurrenceIdentity) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto before = fo4::collect_form_links(plugin);
  ASSERT_EQ(before.size(), 2u);
  EXPECT_EQ(before[0].subrecord.to_string(), "NAME");
  EXPECT_EQ(before[0].subrecord_ordinal, 0u);
  EXPECT_EQ(before[0].offset, 0u);
  EXPECT_EQ(before[0].key, fo4::FormKey::parse("000123:MasterA.esm"));
  EXPECT_EQ(before[1].subrecord.to_string(), "NAME");
  EXPECT_EQ(before[1].subrecord_ordinal, 1u);
  EXPECT_EQ(before[1].offset, 0u);
  EXPECT_EQ(before[1].key, fo4::FormKey::parse("000456:MasterB.esm"));

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::PluginKey::from_filename("MasterB.esm"),
       fo4::PluginKey::from_filename("MasterA.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto after = fo4::collect_form_links(reread);
  ASSERT_EQ(after.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after[i].record, before[i].record);
    EXPECT_EQ(after[i].subrecord, before[i].subrecord);
    EXPECT_EQ(after[i].subrecord_ordinal, before[i].subrecord_ordinal);
    EXPECT_EQ(after[i].offset, before[i].offset);
    EXPECT_EQ(after[i].key, before[i].key);
  }
  EXPECT_NE(after[0].raw, before[0].raw);
  EXPECT_NE(after[1].raw, before[1].raw);
}
