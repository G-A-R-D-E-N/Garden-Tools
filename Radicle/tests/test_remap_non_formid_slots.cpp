#include "fo4/plugin/load_order.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

std::uint32_t sub_u32(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature && sub.data.size() == 4) {
      std::uint32_t value = 0;
      std::memcpy(&value, sub.data.data(), 4);
      return value;
    }
  }
  return 0;
}

}  // namespace

TEST(RemapNonFormIdSlots, DialPriorityAndSubtypeSurviveMasterRemoval) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Other.esp\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "DIAL", 0);
  const auto record = fo4::test::begin_record(out, "DIAL", 0, 0x00012345);
  fo4::test::ByteWriter priority;
  priority.f32(50.0f);
  fo4::test::write_subrecord(out, "PNAM", priority.bytes);
  fo4::test::write_subrecord(out, "SNAM", std::string_view{"CUST", 4});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* dial = fo4::find_record(reread, fo4::FormKey::parse("012345:Fallout4.esm"));
  ASSERT_NE(dial, nullptr);
  EXPECT_EQ(sub_u32(*dial, "PNAM"), 0x42480000u);
  EXPECT_EQ(sub_u32(*dial, "SNAM"), 0x54535543u);
}

TEST(RemapNonFormIdSlots, PackDataInputValueStaysScalarButCombatStyleRemaps) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Other.esp\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "PACK", 0);
  const auto record = fo4::test::begin_record(out, "PACK", 0, 0x00012346);
  fo4::test::ByteWriter combat_style;
  combat_style.u32(0x02000ABC);
  fo4::test::write_subrecord(out, "CNAM", combat_style.bytes);
  fo4::test::write_subrecord(out, "ANAM", std::string_view{"Float\0", 6});
  fo4::test::ByteWriter value;
  value.f32(2.0f);
  fo4::test::write_subrecord(out, "CNAM", value.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* pack = fo4::find_record(reread, fo4::FormKey::parse("012346:Fallout4.esm"));
  ASSERT_NE(pack, nullptr);
  std::vector<std::uint32_t> cnams;
  for (const auto& sub : pack->subrecords) {
    if (sub.signature.to_string() != "CNAM") continue;
    std::uint32_t v = 0;
    std::memcpy(&v, sub.data.data(), 4);
    cnams.push_back(v);
  }
  ASSERT_EQ(cnams.size(), 2u);
  EXPECT_EQ(cnams[0], 0x01000ABCu);
  EXPECT_EQ(cnams[1], 0x40000000u);
}

TEST(RemapNonFormIdSlots, SceneActionScalarsStayButRadioSoundAndTemplateRemap) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Other.esp\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto u32 = [](std::uint32_t v) {
    fo4::test::ByteWriter w;
    w.u32(v);
    return w.bytes;
  };
  const auto action = [](std::uint16_t type) {
    return std::vector<std::byte>{std::byte(type & 0xFF), std::byte(type >> 8)};
  };
  const auto group = fo4::test::begin_group(out, "SCEN", 0);
  const auto record = fo4::test::begin_record(out, "SCEN", 0, 0x00012347);
  fo4::test::write_subrecord(out, "ANAM", action(2));
  fo4::test::write_subrecord(out, "TNAM", u32(0x40000000));
  fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "ANAM", action(0));
  fo4::test::write_subrecord(out, "HTID", u32(0x02000001));
  fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "ANAM", action(6));
  fo4::test::write_subrecord(out, "HTID", u32(0x02000ABC));
  fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "TNAM", u32(0x02000123));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* scen = fo4::find_record(reread, fo4::FormKey::parse("012347:Fallout4.esm"));
  ASSERT_NE(scen, nullptr);
  std::vector<std::uint32_t> values;
  for (const auto& sub : scen->subrecords) {
    if (sub.data.size() != 4) continue;
    std::uint32_t v = 0;
    std::memcpy(&v, sub.data.data(), 4);
    values.push_back(v);
  }
  EXPECT_EQ(values, (std::vector<std::uint32_t>{0x40000000u, 0x02000001u, 0x01000ABCu, 0x01000123u}));
}
