#include "fo4/graph/conflict_graph.hpp"
#include "fo4/plugin/load_order.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST(RemapNonFormIdSlots, NpcNam7FloatSurvivesMasterRemoval) {
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

  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x00012351);
  fo4::test::ByteWriter unused;
  unused.u32(0x02000ABC);
  fo4::test::write_subrecord(out, "NAM7", unused.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* npc = fo4::find_record(reread, fo4::FormKey::parse("012351:Fallout4.esm"));
  ASSERT_NE(npc, nullptr);
  EXPECT_EQ(sub_u32(*npc, "NAM7"), 0x02000ABCu);
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

TEST(RemapNonFormIdSlots, SceneActionTopicDataRemapsOnlyForDialogueAndRadio) {
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
  const auto record = fo4::test::begin_record(out, "SCEN", 0, 0x00012353);
  fo4::test::write_subrecord(out, "ANAM", action(0));
  fo4::test::write_subrecord(out, "DATA", u32(0x02000042));
  fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "ANAM", action(2));
  fo4::test::write_subrecord(out, "DATA", u32(0x02000043));
  fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{});
  fo4::test::write_subrecord(out, "ANAM", action(6));
  fo4::test::write_subrecord(out, "DATA", u32(0x02000044));
  fo4::test::write_subrecord(out, "ANAM", std::vector<std::byte>{});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto* before = fo4::find_record(plugin, fo4::FormKey::parse("012353:Fallout4.esm"));
  ASSERT_NE(before, nullptr);
  const auto original_subs = before->subrecords;
  ASSERT_EQ(original_subs.size(), 9u);
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* scen = fo4::find_record(reread, fo4::FormKey::parse("012353:Fallout4.esm"));
  ASSERT_NE(scen, nullptr);
  ASSERT_EQ(scen->subrecords.size(), original_subs.size());
  for (std::size_t i = 0; i < original_subs.size(); ++i) {
    EXPECT_EQ(scen->subrecords[i].signature, original_subs[i].signature);
    if (i == 1) {
      EXPECT_EQ(scen->subrecords[i].data, u32(0x01000042));
    } else if (i == 7) {
      EXPECT_EQ(scen->subrecords[i].data, u32(0x01000044));
    } else {
      EXPECT_EQ(scen->subrecords[i].data, original_subs[i].data);
    }
  }
}

TEST(RemapNonFormIdSlots, SoundAndFootstepStringSlotsStayUnchanged) {
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
  const auto sndr_group = fo4::test::begin_group(out, "SNDR", 0);
  const auto sndr = fo4::test::begin_record(out, "SNDR", 0, 0x00012349);
  fo4::test::write_subrecord(out, "ANAM", u32(0x02000ABC));
  fo4::test::write_subrecord(out, "NNAM", u32(0x02000ABC));
  fo4::test::write_subrecord(out, "FNAM", u32(0x02000ABC));
  fo4::test::end_record(out, sndr);
  fo4::test::end_group(out, sndr_group);
  const auto fstp_group = fo4::test::begin_group(out, "FSTP", 0);
  const auto fstp = fo4::test::begin_record(out, "FSTP", 0, 0x0001234A);
  fo4::test::write_subrecord(out, "ANAM", u32(0x02000ABC));
  fo4::test::end_record(out, fstp);
  fo4::test::end_group(out, fstp_group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* sndr_out = fo4::find_record(reread, fo4::FormKey::parse("012349:Fallout4.esm"));
  ASSERT_NE(sndr_out, nullptr);
  EXPECT_EQ(sub_u32(*sndr_out, "ANAM"), 0x02000ABCu);
  EXPECT_EQ(sub_u32(*sndr_out, "NNAM"), 0x02000ABCu);
  EXPECT_EQ(sub_u32(*sndr_out, "FNAM"), 0x02000ABCu);
  const auto* fstp_out = fo4::find_record(reread, fo4::FormKey::parse("01234A:Fallout4.esm"));
  ASSERT_NE(fstp_out, nullptr);
  EXPECT_EQ(sub_u32(*fstp_out, "ANAM"), 0x02000ABCu);
}

TEST(RemapNonFormIdSlots, SopmAndMusicTrackFourByteSlotsStayUnchanged) {
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
  const auto sopm_group = fo4::test::begin_group(out, "SOPM", 0);
  const auto sopm = fo4::test::begin_record(out, "SOPM", 0, 0x0001234B);
  fo4::test::write_subrecord(out, "MNAM", u32(0x02000ABC));
  fo4::test::end_record(out, sopm);
  fo4::test::end_group(out, sopm_group);
  const auto must_group = fo4::test::begin_group(out, "MUST", 0);
  const auto must = fo4::test::begin_record(out, "MUST", 0, 0x0001234C);
  fo4::test::write_subrecord(out, "ANAM", u32(0x02000ABC));
  fo4::test::write_subrecord(out, "BNAM", u32(0x02000ABC));
  fo4::test::write_subrecord(out, "FNAM", u32(0x02000ABC));
  fo4::test::end_record(out, must);
  fo4::test::end_group(out, must_group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("Other.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* sopm_out = fo4::find_record(reread, fo4::FormKey::parse("01234B:Fallout4.esm"));
  ASSERT_NE(sopm_out, nullptr);
  EXPECT_EQ(sub_u32(*sopm_out, "MNAM"), 0x02000ABCu);
  const auto* must_out = fo4::find_record(reread, fo4::FormKey::parse("01234C:Fallout4.esm"));
  ASSERT_NE(must_out, nullptr);
  EXPECT_EQ(sub_u32(*must_out, "ANAM"), 0x02000ABCu);
  EXPECT_EQ(sub_u32(*must_out, "BNAM"), 0x02000ABCu);
  EXPECT_EQ(sub_u32(*must_out, "FNAM"), 0x02000ABCu);
}

TEST(RemapNonFormIdSlots, InTableScalarsSurviveMasterReorder) {
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
  fo4::test::end_record(out, tes4);

  const auto scen_group = fo4::test::begin_group(out, "SCEN", 0);
  const auto scen = fo4::test::begin_record(out, "SCEN", 0, 0x00012348);
  fo4::test::ByteWriter width;
  width.u32(350);
  fo4::test::write_subrecord(out, "WNAM", width.bytes);
  fo4::test::end_record(out, scen);
  fo4::test::end_group(out, scen_group);
  const auto acti_group = fo4::test::begin_group(out, "ACTI", 0);
  const auto acti = fo4::test::begin_record(out, "ACTI", 0, 0x00012349);
  fo4::test::ByteWriter color;
  color.u32(0x00808080);
  fo4::test::write_subrecord(out, "PNAM", color.bytes);
  fo4::test::end_record(out, acti);
  fo4::test::end_group(out, acti_group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* scen_record = fo4::find_record(reread, fo4::FormKey::parse("012348:Fallout4.esm"));
  const auto* acti_record = fo4::find_record(reread, fo4::FormKey::parse("012349:Fallout4.esm"));
  ASSERT_NE(scen_record, nullptr);
  ASSERT_NE(acti_record, nullptr);
  EXPECT_EQ(sub_u32(*scen_record, "WNAM"), 350u);
  EXPECT_EQ(sub_u32(*acti_record, "PNAM"), 0x00808080u);
}

TEST(RemapNonFormIdSlots, LocalizedStringIdsSurviveMasterReorder) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
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
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "TERM", 0);
  const auto record = fo4::test::begin_record(out, "TERM", 0, 0x0001234A);
  fo4::test::ByteWriter welcome;
  welcome.u32(0x0000ABCD);
  fo4::test::write_subrecord(out, "WNAM", welcome.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* term = fo4::find_record(reread, fo4::FormKey::parse("01234A:Fallout4.esm"));
  ASSERT_NE(term, nullptr);
  EXPECT_EQ(sub_u32(*term, "WNAM"), 0x0000ABCDu);
}

TEST(RemapNonFormIdSlots, NoneSentinelAndUndecodedSlotsSurviveMasterReorder) {
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
  fo4::test::end_record(out, tes4);
  const auto u32 = [](std::uint32_t v) {
    fo4::test::ByteWriter w;
    w.u32(v);
    return w.bytes;
  };
  const auto one = [&](std::string_view signature, std::uint32_t id, std::string_view sub, std::uint32_t value) {
    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, id);
    fo4::test::write_subrecord(out, sub, u32(value));
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  };
  one("EQUP", 0x00012350, "ANAM", 0xFFFFFFFFu);
  one("WRLD", 0x00012351, "NAM4", 0x43E10000u);
  one("TACT", 0x00012352, "PNAM", 0x00000123u);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto value = [&](std::string_view key, std::string_view sub) {
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    return record == nullptr ? 0u : sub_u32(*record, sub);
  };
  EXPECT_EQ(value("012350:Fallout4.esm", "ANAM"), 0xFFFFFFFFu);
  EXPECT_EQ(value("012351:Fallout4.esm", "NAM4"), 0x43E10000u);
  EXPECT_EQ(value("012352:Fallout4.esm", "PNAM"), 0x00000123u);
}

TEST(RemapNonFormIdSlots, NoneSentinelIsNotALinkWhenCopyingAnOverride) {
  const auto plugin_bytes = [](bool with_record) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(with_record ? 1 : 0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    if (with_record) {
      fo4::test::ByteWriter anam;
      anam.u32(0xFFFFFFFFu);
      const auto group = fo4::test::begin_group(out, "EQUP", 0);
      const auto record = fo4::test::begin_record(out, "EQUP", 0, 0x00012350);
      fo4::test::write_subrecord(out, "ANAM", anam.bytes);
      fo4::test::end_record(out, record);
      fo4::test::end_group(out, group);
    }
    return out.bytes;
  };
  const auto source = fo4::read_plugin(plugin_bytes(true), "Source.esp");
  const auto key = fo4::FormKey::parse("012350:Fallout4.esm");

  const auto links = fo4::collect_form_links(source);
  const auto anam = std::find_if(links.begin(), links.end(), [](const fo4::FormLinkHit& hit) {
    return hit.subrecord.to_string() == "ANAM";
  });
  ASSERT_NE(anam, links.end());
  EXPECT_EQ(anam->raw.value(), 0xFFFFFFFFu);
  EXPECT_TRUE(anam->key.owner().empty());

  const fo4::Plugin* order[] = {&source};
  const auto graph = fo4::ConflictGraph::build(order);
  EXPECT_TRUE(graph.references_from(key).empty());

  auto patch = fo4::read_plugin(plugin_bytes(false), "Patch.esp");
  ASSERT_EQ(fo4::copy_record_as_override(patch, source, key, false), fo4::RecordCopyStatus::inserted);
  ASSERT_EQ(patch.header.masters.size(), 1u);
  EXPECT_EQ(patch.header.masters[0].key, fo4::PluginKey::from_filename("Fallout4.esm"));
}

TEST(RemapNonFormIdSlots, FactionRankNumberSurvivesMasterReorder) {
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
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "FACT", 0);
  const auto record = fo4::test::begin_record(out, "FACT", 0, 0x0001234B);
  fo4::test::ByteWriter rank;
  rank.u32(3);
  fo4::test::write_subrecord(out, "RNAM", rank.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* faction = fo4::find_record(reread, fo4::FormKey::parse("01234B:Fallout4.esm"));
  ASSERT_NE(faction, nullptr);
  EXPECT_EQ(sub_u32(*faction, "RNAM"), 3u);
  const auto links = fo4::collect_form_links(reread);
  EXPECT_TRUE(std::none_of(links.begin(), links.end(), [](const fo4::FormLinkHit& hit) {
    return hit.subrecord.to_string() == "RNAM";
  }));
}

TEST(RemapNonFormIdSlots, FloraActivateTextStringIdSurvivesMasterReorder) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
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
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "FLOR", 0);
  const auto record = fo4::test::begin_record(out, "FLOR", 0, 0x0001234C);
  fo4::test::ByteWriter activate_text;
  activate_text.u32(0x0000ABCD);
  fo4::test::write_subrecord(out, "RNAM", activate_text.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* flor = fo4::find_record(reread, fo4::FormKey::parse("01234C:Fallout4.esm"));
  ASSERT_NE(flor, nullptr);
  EXPECT_EQ(sub_u32(*flor, "RNAM"), 0x0000ABCDu);
}

TEST(RemapNonFormIdSlots, ClimateSunTexturePathSurvivesMasterReorder) {
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
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "CLMT", 0);
  const auto record = fo4::test::begin_record(out, "CLMT", 0, 0x0001234D);
  fo4::test::write_subrecord(out, "FNAM", std::string_view{"a.d\0", 4});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* climate = fo4::find_record(reread, fo4::FormKey::parse("01234D:Fallout4.esm"));
  ASSERT_NE(climate, nullptr);
  EXPECT_EQ(sub_u32(*climate, "FNAM"), 0x00642E61u);
}

TEST(RemapNonFormIdSlots, WaterNoiseTexturePathSurvivesMasterReorder) {
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
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "WATR", 0);
  const auto record = fo4::test::begin_record(out, "WATR", 0, 0x0001234E);
  fo4::test::write_subrecord(out, "NAM2", std::string_view{"a.d\0", 4});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("DLC01.esm"),
                                      fo4::PluginKey::from_filename("Fallout4.esm")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* water = fo4::find_record(reread, fo4::FormKey::parse("01234E:Fallout4.esm"));
  ASSERT_NE(water, nullptr);
  EXPECT_EQ(sub_u32(*water, "NAM2"), 0x00642E61u);
}
