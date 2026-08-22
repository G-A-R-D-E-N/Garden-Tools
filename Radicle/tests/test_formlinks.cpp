#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <span>

namespace {

void write_u32(fo4::test::ByteWriter& out, std::uint32_t value) { out.u32(value); }

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

std::vector<std::byte> make_ctda(std::uint16_t function, std::uint32_t param1, float compare) {
  fo4::test::ByteWriter ctda;
  ctda.u8(0);  // type
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  std::uint32_t bits = 0;
  std::memcpy(&bits, &compare, 4);
  ctda.u32(bits);
  ctda.u16(function);
  ctda.u16(0);
  ctda.u32(param1);
  ctda.u32(0);
  ctda.u32(0);  // run on subject
  ctda.u32(0);
  ctda.u32(static_cast<std::uint32_t>(-1));
  return ctda.bytes;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) {
      return &sub;
    }
  }
  return nullptr;
}

fo4::test::ByteWriter plugin_with_masters() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"RadiationOverhaul.esp\0", 22});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  return out;
}

}  // namespace

TEST(FormLink, KwdaRemapsWhenMasterRemoved) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "WEAP", 0);
  const auto rec = fo4::test::begin_record(out, "WEAP", 0, 0x03000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Gun\0", 4});
  fo4::test::ByteWriter kwda;
  write_u32(kwda, 0x0000965E);  // Fallout4.esm
  write_u32(kwda, 0x0200009A);  // RadiationOverhaul.esp
  fo4::test::write_subrecord(out, "KWDA", kwda.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  plugin = fo4::remap_plugin_masters(std::move(plugin),
                                     {fo4::PluginKey::from_filename("Fallout4.esm"),
                                      fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* weap = fo4::find_record(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(weap, nullptr);
  const fo4::Subrecord* kwda_sub = nullptr;
  for (const auto& sub : weap->subrecords) {
    if (sub.signature.to_string() == "KWDA") {
      kwda_sub = &sub;
    }
  }
  ASSERT_NE(kwda_sub, nullptr);
  ASSERT_EQ(kwda_sub->data.size(), 8u);
  EXPECT_EQ(read_u32(kwda_sub->data, 0), 0x0000965Eu);
  EXPECT_EQ(read_u32(kwda_sub->data, 4), 0x0100009Au);
}

TEST(FormLink, CtdaGetBaseValueActorValueUsesMastOrdinal) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "COBJ", 0);
  const auto rec = fo4::test::begin_record(out, "COBJ", 0, 0x03000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Recipe\0", 7});
  constexpr std::uint16_t get_base_value = 277;
  fo4::test::write_subrecord(out, "CTDA", make_ctda(get_base_value, 0x02000812, 10.0f));
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "ConditionPatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "ConditionPatch.esp");
  const auto* cobj = fo4::find_record(reread, fo4::FormKey::parse("000801:ConditionPatch.esp"));
  ASSERT_NE(cobj, nullptr);
  const fo4::Subrecord* ctda = nullptr;
  for (const auto& sub : cobj->subrecords) {
    if (sub.signature.to_string() == "CTDA") {
      ctda = &sub;
    }
  }
  ASSERT_NE(ctda, nullptr);
  ASSERT_GE(ctda->data.size(), 16u);
  EXPECT_EQ(read_u32(ctda->data, 12), 0x01000812u);
}

TEST(FormLink, CtdaGetLockedLeavesComparisonFloatAlone) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "COBJ", 0);
  const auto rec = fo4::test::begin_record(out, "COBJ", 0, 0x03000801);
  constexpr std::uint16_t get_locked = 5;
  const auto ctda_bytes = make_ctda(get_locked, 0, 10.0f);
  fo4::test::write_subrecord(out, "CTDA", ctda_bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "LockCheck.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "LockCheck.esp");
  const auto* cobj = fo4::find_record(reread, fo4::FormKey::parse("000801:LockCheck.esp"));
  ASSERT_NE(cobj, nullptr);
  const fo4::Subrecord* ctda = nullptr;
  for (const auto& sub : cobj->subrecords) {
    if (sub.signature.to_string() == "CTDA") {
      ctda = &sub;
    }
  }
  ASSERT_NE(ctda, nullptr);
  EXPECT_EQ(read_u32(ctda->data, 4), read_u32(ctda_bytes, 4));
  EXPECT_EQ(read_u32(ctda->data, 12), 0u);
}

TEST(FormLink, NameSubrecordRemaps) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "REFR", 0);
  const auto rec = fo4::test::begin_record(out, "REFR", 0, 0x03000810);
  fo4::test::ByteWriter name;
  write_u32(name, 0x0200009A);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Refs.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Refs.esp");
  const auto* refr = fo4::find_record(reread, fo4::FormKey::parse("000810:Refs.esp"));
  ASSERT_NE(refr, nullptr);
  const fo4::Subrecord* name_sub = nullptr;
  for (const auto& sub : refr->subrecords) {
    if (sub.signature.to_string() == "NAME") {
      name_sub = &sub;
    }
  }
  ASSERT_NE(name_sub, nullptr);
  EXPECT_EQ(read_u32(name_sub->data, 0), 0x0100009Au);
}

TEST(FormLink, LightMasterCtdaUsesMastOrdinalNotFe) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"FullDependency.esm\0", 19});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"LightDependency.esl\0", 20});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "COBJ", 0);
  const auto rec = fo4::test::begin_record(out, "COBJ", 0, 0x03000801);
  constexpr std::uint16_t get_base_value = 277;
  fo4::test::write_subrecord(out, "CTDA", make_ctda(get_base_value, 0x02000812, 10.0f));
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "ConditionPatch.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("LightDependency.esl")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ConditionPatch.esp");
  const auto* cobj = fo4::find_record(reread, fo4::FormKey::parse("000801:ConditionPatch.esp"));
  ASSERT_NE(cobj, nullptr);
  const auto* ctda = find_sub(*cobj, "CTDA");
  ASSERT_NE(ctda, nullptr);
  EXPECT_EQ(read_u32(ctda->data, 12), 0x01000812u);
  EXPECT_NE(read_u32(ctda->data, 12), 0xFE000812u);
}

TEST(FormLink, Tes4OnamOverridesRemap) {
  auto out = plugin_with_masters();
  // Rebuild TES4 with ONAM by appending after the existing TES4 is awkward; write a
  // dedicated header.
  out = {};
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DLC01.esm\0", 10});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"RadiationOverhaul.esp\0", 22});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::ByteWriter onam;
  write_u32(onam, 0x0200009A);
  write_u32(onam, 0x00000007);
  fo4::test::write_subrecord(out, "ONAM", onam.bytes);
  fo4::test::end_record(out, tes4);

  auto plugin = fo4::read_plugin(out.bytes, "Onam.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Onam.esp");
  const auto* onam_sub = find_sub(reread.tes4, "ONAM");
  ASSERT_NE(onam_sub, nullptr);
  ASSERT_EQ(onam_sub->data.size(), 8u);
  EXPECT_EQ(read_u32(onam_sub->data, 0), 0x0100009Au);
  EXPECT_EQ(read_u32(onam_sub->data, 4), 0x00000007u);
}

TEST(FormLink, CtdaUseGlobalRemapsComparisonValue) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "COBJ", 0);
  const auto rec = fo4::test::begin_record(out, "COBJ", 0, 0x03000801);
  auto ctda = make_ctda(5, 0, 0.0f);
  ctda[0] = std::byte{0x04};  // Use Global
  const std::uint32_t glob = 0x0200009A;
  std::memcpy(ctda.data() + 4, &glob, 4);
  fo4::test::write_subrecord(out, "CTDA", ctda);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Glob.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Glob.esp");
  const auto* cobj = fo4::find_record(reread, fo4::FormKey::parse("000801:Glob.esp"));
  ASSERT_NE(cobj, nullptr);
  const auto* sub = find_sub(*cobj, "CTDA");
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, 4), 0x0100009Au);
}

TEST(FormLink, CntoRemapsItemLeavesCount) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "CONT", 0);
  const auto rec = fo4::test::begin_record(out, "CONT", 0, 0x03000820);
  fo4::test::ByteWriter cnto;
  write_u32(cnto, 0x0200009A);
  write_u32(cnto, 5);
  fo4::test::write_subrecord(out, "CNTO", cnto.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Chest.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Chest.esp");
  const auto* cont = fo4::find_record(reread, fo4::FormKey::parse("000820:Chest.esp"));
  ASSERT_NE(cont, nullptr);
  const auto* cnto_sub = find_sub(*cont, "CNTO");
  ASSERT_NE(cnto_sub, nullptr);
  ASSERT_EQ(cnto_sub->data.size(), 8u);
  EXPECT_EQ(read_u32(cnto_sub->data, 0), 0x0100009Au);
  EXPECT_EQ(read_u32(cnto_sub->data, 4), 5u);
}

TEST(FormLink, LvloRemapsReferenceLeavesLevelAndCount) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "LVLI", 0);
  const auto rec = fo4::test::begin_record(out, "LVLI", 0, 0x03000821);
  fo4::test::ByteWriter lvlo;
  lvlo.u16(10);
  lvlo.u16(0);
  write_u32(lvlo, 0x0200009A);
  lvlo.u16(2);
  lvlo.u8(0);
  lvlo.u8(0);
  fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Loot.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Loot.esp");
  const auto* lvli = fo4::find_record(reread, fo4::FormKey::parse("000821:Loot.esp"));
  ASSERT_NE(lvli, nullptr);
  const auto* lvlo_sub = find_sub(*lvli, "LVLO");
  ASSERT_NE(lvlo_sub, nullptr);
  ASSERT_EQ(lvlo_sub->data.size(), 12u);
  EXPECT_EQ(read_u32(lvlo_sub->data, 0) & 0xFFFFu, 10u);
  EXPECT_EQ(read_u32(lvlo_sub->data, 4), 0x0100009Au);
  EXPECT_EQ(read_u32(lvlo_sub->data, 8) & 0xFFFFu, 2u);
}

TEST(FormLink, XtelRemapsDoorLeavesPosition) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "REFR", 0);
  const auto rec = fo4::test::begin_record(out, "REFR", 0, 0x03000822);
  fo4::test::ByteWriter xtel;
  write_u32(xtel, 0x0200009A);
  xtel.f32(1.5f);
  xtel.f32(2.5f);
  xtel.f32(3.5f);
  xtel.f32(0.0f);
  xtel.f32(0.0f);
  xtel.f32(90.0f);
  write_u32(xtel, 0);
  fo4::test::write_subrecord(out, "XTEL", xtel.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Door.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Door.esp");
  const auto* refr = fo4::find_record(reread, fo4::FormKey::parse("000822:Door.esp"));
  ASSERT_NE(refr, nullptr);
  const auto* xtel_sub = find_sub(*refr, "XTEL");
  ASSERT_NE(xtel_sub, nullptr);
  ASSERT_EQ(xtel_sub->data.size(), 32u);
  EXPECT_EQ(read_u32(xtel_sub->data, 0), 0x0100009Au);
  EXPECT_EQ(read_u32(xtel_sub->data, 4), read_u32(xtel.bytes, 4));
  EXPECT_EQ(read_u32(xtel_sub->data, 8), read_u32(xtel.bytes, 8));
}

TEST(FormLink, XlocRemapsKeyLeavesLockLevel) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "REFR", 0);
  const auto rec = fo4::test::begin_record(out, "REFR", 0, 0x03000823);
  fo4::test::ByteWriter xloc;
  xloc.u8(100);
  xloc.u8(0);
  xloc.u8(0);
  xloc.u8(0);
  write_u32(xloc, 0x0200009A);
  write_u32(xloc, 0);
  fo4::test::write_subrecord(out, "XLOC", xloc.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Lock.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Lock.esp");
  const auto* refr = fo4::find_record(reread, fo4::FormKey::parse("000823:Lock.esp"));
  ASSERT_NE(refr, nullptr);
  const auto* xloc_sub = find_sub(*refr, "XLOC");
  ASSERT_NE(xloc_sub, nullptr);
  ASSERT_EQ(xloc_sub->data.size(), 12u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(xloc_sub->data[0]), 100);
  EXPECT_EQ(read_u32(xloc_sub->data, 4), 0x0100009Au);
  EXPECT_EQ(read_u32(xloc_sub->data, 8), 0u);
}

TEST(FormLink, XlkrRemapsKeywordAndReference) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "REFR", 0);
  const auto rec = fo4::test::begin_record(out, "REFR", 0, 0x03000824);
  fo4::test::ByteWriter xlkr;
  write_u32(xlkr, 0x0000965E);
  write_u32(xlkr, 0x0200009A);
  fo4::test::write_subrecord(out, "XLKR", xlkr.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Link.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Link.esp");
  const auto* refr = fo4::find_record(reread, fo4::FormKey::parse("000824:Link.esp"));
  ASSERT_NE(refr, nullptr);
  const auto* xlkr_sub = find_sub(*refr, "XLKR");
  ASSERT_NE(xlkr_sub, nullptr);
  ASSERT_EQ(xlkr_sub->data.size(), 8u);
  EXPECT_EQ(read_u32(xlkr_sub->data, 0), 0x0000965Eu);
  EXPECT_EQ(read_u32(xlkr_sub->data, 4), 0x0100009Au);
}

TEST(FormLink, ContextUsesStoredMasterKindNotFilename) {
  auto out = plugin_with_masters();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  ASSERT_EQ(plugin.header.masters.size(), 3u);
  EXPECT_EQ(plugin.header.masters[2].kind, fo4::PluginKind::full);
  EXPECT_FALSE(plugin.header.masters[2].key.looks_light());

  plugin.header.masters[2].kind = fo4::PluginKind::light;
  const auto ctx = fo4::plugin_form_id_context(plugin);
  ASSERT_EQ(ctx.masters.size(), 3u);
  EXPECT_EQ(ctx.masters[2].kind, fo4::PluginKind::light);
  EXPECT_EQ(ctx.masters[2].key, fo4::PluginKey::from_filename("RadiationOverhaul.esp"));
}

TEST(FormLink, RemapKeepsEspFlaggedLightMasterKind) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"FlaggedLight.esp\0", 17});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "COBJ", 0);
  const auto rec = fo4::test::begin_record(out, "COBJ", 0, 0x02000801);
  fo4::test::ByteWriter name;
  write_u32(name, 0x01000812);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "ConditionPatch.esp");
  EXPECT_EQ(fo4::plugin_form_id_context(plugin).masters[0].kind, fo4::PluginKind::full);
  EXPECT_EQ(fo4::plugin_form_id_context(plugin).masters[1].kind, fo4::PluginKind::full);

  plugin = fo4::remap_plugin_masters(
      std::move(plugin),
      {fo4::MasterDescriptor{fo4::PluginKey::from_filename("Fallout4.esm"),
                             fo4::PluginKind::full},
       fo4::MasterDescriptor{fo4::PluginKey::from_filename("FlaggedLight.esp"),
                             fo4::PluginKind::light}});

  const auto ctx = fo4::plugin_form_id_context(plugin);
  ASSERT_EQ(ctx.masters.size(), 2u);
  EXPECT_EQ(ctx.masters[1].key, fo4::PluginKey::from_filename("FlaggedLight.esp"));
  EXPECT_EQ(ctx.masters[1].kind, fo4::PluginKind::light);
  EXPECT_FALSE(ctx.masters[1].key.looks_light());

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ConditionPatch.esp");
  const auto* cobj =
      fo4::find_record(reread, fo4::FormKey::parse("000801:ConditionPatch.esp"));
  ASSERT_NE(cobj, nullptr);
  const auto* name_sub = find_sub(*cobj, "NAME");
  ASSERT_NE(name_sub, nullptr);
  EXPECT_EQ(read_u32(name_sub->data, 0), 0x01000812u);
}

TEST(FormLink, XespRemapsParentLeavesFlags) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "REFR", 0);
  const auto rec = fo4::test::begin_record(out, "REFR", 0, 0x03000825);
  fo4::test::ByteWriter xesp;
  write_u32(xesp, 0x0200009A);
  write_u32(xesp, 0x00000001);
  fo4::test::write_subrecord(out, "XESP", xesp.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Enable.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Enable.esp");
  const auto* refr = fo4::find_record(reread, fo4::FormKey::parse("000825:Enable.esp"));
  ASSERT_NE(refr, nullptr);
  const auto* xesp_sub = find_sub(*refr, "XESP");
  ASSERT_NE(xesp_sub, nullptr);
  ASSERT_EQ(xesp_sub->data.size(), 8u);
  EXPECT_EQ(read_u32(xesp_sub->data, 0), 0x0100009Au);
  EXPECT_EQ(read_u32(xesp_sub->data, 4), 1u);
}

TEST(FormLink, CoedRemapsOwnerLeavesCondition) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "CONT", 0);
  const auto rec = fo4::test::begin_record(out, "CONT", 0, 0x03000826);
  fo4::test::ByteWriter cnto;
  write_u32(cnto, 0x0000000F);
  write_u32(cnto, 1);
  fo4::test::write_subrecord(out, "CNTO", cnto.bytes);
  fo4::test::ByteWriter coed;
  write_u32(coed, 0x0200009A);
  write_u32(coed, 0);
  coed.f32(1.0f);
  fo4::test::write_subrecord(out, "COED", coed.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Owned.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Owned.esp");
  const auto* cont = fo4::find_record(reread, fo4::FormKey::parse("000826:Owned.esp"));
  ASSERT_NE(cont, nullptr);
  const auto* coed_sub = find_sub(*cont, "COED");
  ASSERT_NE(coed_sub, nullptr);
  ASSERT_EQ(coed_sub->data.size(), 12u);
  EXPECT_EQ(read_u32(coed_sub->data, 0), 0x0100009Au);
  EXPECT_EQ(read_u32(coed_sub->data, 4), 0u);
  EXPECT_EQ(read_u32(coed_sub->data, 8), read_u32(coed.bytes, 8));
}

TEST(FormLink, ApplyMasterKindsStampsEspFlaggedLight) {
  auto out = plugin_with_masters();
  auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  ASSERT_EQ(plugin.header.masters.size(), 3u);
  EXPECT_EQ(plugin.header.masters[2].kind, fo4::PluginKind::full);

  const fo4::MasterDescriptor known[] = {
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"), fo4::PluginKind::light},
  };
  fo4::apply_master_kinds(plugin, known);
  EXPECT_EQ(plugin.header.masters[0].kind, fo4::PluginKind::full);
  EXPECT_EQ(plugin.header.masters[2].kind, fo4::PluginKind::light);
  EXPECT_EQ(fo4::plugin_form_id_context(plugin).masters[2].kind, fo4::PluginKind::light);
}

TEST(FormLink, XndpRemapsNavmeshLeavesTriangle) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "REFR", 0);
  const auto rec = fo4::test::begin_record(out, "REFR", 0, 0x03000828);
  fo4::test::ByteWriter xndp;
  write_u32(xndp, 0x0200009A);
  xndp.u16(12);
  xndp.u16(0);
  fo4::test::write_subrecord(out, "XNDP", xndp.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Portal.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Portal.esp");
  const auto* refr = fo4::find_record(reread, fo4::FormKey::parse("000828:Portal.esp"));
  ASSERT_NE(refr, nullptr);
  const auto* xndp_sub = find_sub(*refr, "XNDP");
  ASSERT_NE(xndp_sub, nullptr);
  ASSERT_EQ(xndp_sub->data.size(), 8u);
  EXPECT_EQ(read_u32(xndp_sub->data, 0), 0x0100009Au);
  EXPECT_EQ(read_u32(xndp_sub->data, 4) & 0xFFFFu, 12u);
}

TEST(FormLink, StampMasterKindsReadsTes4SmallFile) {
  fo4::test::ByteWriter light;
  const auto tes4 =
      fo4::test::begin_record(light, "TES4", fo4::RecordFlags::small_file, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(light, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(light, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(light, "DATA", data.bytes);
  fo4::test::end_record(light, tes4);
  const auto flagged = fo4::read_plugin(light.bytes, "FlaggedLight.esp");

  auto out = plugin_with_masters();
  auto patch = fo4::read_plugin(out.bytes, "Patch.esp");
  EXPECT_EQ(patch.header.masters[2].kind, fo4::PluginKind::full);

  const fo4::Plugin loaded[] = {flagged};
  fo4::stamp_master_kinds(patch, loaded);
  EXPECT_EQ(patch.header.masters[2].kind, fo4::PluginKind::full);

  fo4::test::ByteWriter named;
  const auto tes4b =
      fo4::test::begin_record(named, "TES4", fo4::RecordFlags::small_file, 0);
  fo4::test::write_subrecord(named, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(named, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(named, "DATA", data.bytes);
  fo4::test::end_record(named, tes4b);
  auto radiation = fo4::read_plugin(named.bytes, "RadiationOverhaul.esp");
  const fo4::Plugin masters[] = {radiation};
  fo4::stamp_master_kinds(patch, masters);
  EXPECT_EQ(patch.header.masters[2].kind, fo4::PluginKind::light);
  EXPECT_EQ(fo4::plugin_form_id_context(patch).masters[2].kind, fo4::PluginKind::light);
}

TEST(FormLink, DstdRemapsExplosionAndDebris) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "STAT", 0);
  const auto rec = fo4::test::begin_record(out, "STAT", 0, 0x03000827);
  fo4::test::ByteWriter dstd;
  dstd.u8(50);
  dstd.u8(1);
  dstd.u8(1);
  dstd.u8(0);
  write_u32(dstd, 0);
  write_u32(dstd, 0x0200009A);
  write_u32(dstd, 0x00000007);
  write_u32(dstd, 3);
  fo4::test::write_subrecord(out, "DSTD", dstd.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Break.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Break.esp");
  const auto* stat = fo4::find_record(reread, fo4::FormKey::parse("000827:Break.esp"));
  ASSERT_NE(stat, nullptr);
  const auto* dstd_sub = find_sub(*stat, "DSTD");
  ASSERT_NE(dstd_sub, nullptr);
  ASSERT_EQ(dstd_sub->data.size(), 20u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dstd_sub->data[0]), 50);
  EXPECT_EQ(read_u32(dstd_sub->data, 4), 0u);
  EXPECT_EQ(read_u32(dstd_sub->data, 8), 0x0100009Au);
  EXPECT_EQ(read_u32(dstd_sub->data, 12), 0x00000007u);
  EXPECT_EQ(read_u32(dstd_sub->data, 16), 3u);
}

TEST(FormLink, EfidRemapsBaseEffect) {
  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ALCH", 0);
  const auto rec = fo4::test::begin_record(out, "ALCH", 0, 0x03000802);
  fo4::test::ByteWriter efid;
  write_u32(efid, 0x0200009A);
  fo4::test::write_subrecord(out, "EFID", efid.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  auto plugin = fo4::read_plugin(out.bytes, "Potion.esp");
  plugin = fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Potion.esp");
  const auto* alch = fo4::find_record(reread, fo4::FormKey::parse("000802:Potion.esp"));
  ASSERT_NE(alch, nullptr);
  const auto* efid_sub = find_sub(*alch, "EFID");
  ASSERT_NE(efid_sub, nullptr);
  EXPECT_EQ(read_u32(efid_sub->data, 0), 0x0100009Au);
}
