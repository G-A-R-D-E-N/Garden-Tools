#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t k_get_distance = 1;
constexpr std::uint16_t k_get_faction_rank_difference = 60;
constexpr std::uint16_t k_get_is_current_package = 161;
constexpr std::uint16_t k_get_in_worldspace = 310;
constexpr std::uint32_t k_run_on_subject = 0;
constexpr std::uint32_t k_run_on_quest_alias = 5;
constexpr std::uint32_t k_missing = 0xFFFFFFFFu;

std::vector<std::byte> make_ctda(std::uint8_t type, std::uint16_t function, std::uint32_t param1,
                                 std::uint32_t param2, std::uint32_t run_on) {
  fo4::test::ByteWriter ctda;
  ctda.u8(type);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u8(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.u32(param1);
  ctda.u32(param2);
  ctda.u32(run_on);
  ctda.u32(0);
  ctda.u32(static_cast<std::uint32_t>(-1));
  return ctda.bytes;
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  if (offset + 4 > data.size()) {
    return k_missing;
  }
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
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

fo4::test::ByteWriter masterless_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out;
}

void write_cobj(fo4::test::ByteWriter& out, std::uint32_t form_id,
                const std::vector<std::byte>& ctda) {
  const auto grup = fo4::test::begin_group(out, "COBJ", 0);
  const auto rec = fo4::test::begin_record(out, "COBJ", 0, form_id);
  fo4::test::write_subrecord(out, "CTDA", ctda);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
}

std::vector<std::byte> remapped_ctda(const fo4::test::ByteWriter& out, const std::string& plugin_name,
                                     std::vector<fo4::PluginKey> masters) {
  auto plugin = fo4::read_plugin(out.bytes, plugin_name);
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(masters));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), plugin_name);
  const auto* cobj = fo4::find_record(reread, fo4::FormKey::parse("000801:" + plugin_name));
  if (cobj == nullptr) {
    return {};
  }
  for (const auto& sub : cobj->subrecords) {
    if (sub.signature.to_string() == "CTDA") {
      return sub.data;
    }
  }
  return {};
}

}  // namespace

TEST(CtdaReferenceParams, AliasParamSurvivesMasterZeroMoving) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_aliases, k_get_distance, 3, 0, k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "AliasMove.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 3u);
}

TEST(CtdaReferenceParams, AliasParamSurvivesMasterlessPluginGainingMaster) {
  auto out = masterless_plugin();
  write_cobj(out, 0x00000801,
             make_ctda(fo4::k_ctda_flag_aliases, k_get_distance, 3, 0, k_run_on_subject));
  const auto ctda = remapped_ctda(out, "AliasGain.esp",
                                  {fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 3u);
}

TEST(CtdaReferenceParams, ReferenceParamWithoutAliasFlagIsStillRemapped) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801, make_ctda(0, k_get_distance, 3, 0, k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "PlainMove.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 0x01000003u);
}

TEST(CtdaReferenceParams, GetInWorldspaceParamOneIsRemappedOnMasterReorder) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801, make_ctda(0, k_get_in_worldspace, 3, 0, k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "WorldspaceMove.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 0x01000003u);
}

TEST(CtdaReferenceParams, PackdataFlagLeavesPackageParamAlone) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_pack_data, k_get_is_current_package, 3, 0,
                       k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "PackdataMove.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 3u);
}

TEST(CtdaReferenceParams, PackdataFlagLeavesReferenceParamAlone) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_pack_data, k_get_distance, 3, 0, k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "PackdataRef.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 3u);
}

TEST(CtdaReferenceParams, CurrentPackageOnQuestAliasStillRemapsPackage) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_aliases, k_get_is_current_package, 0x00000812, 0,
                       k_run_on_quest_alias));
  const auto ctda = remapped_ctda(
      out, "QuestAliasPackage.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 0x01000812u);
}

TEST(CtdaReferenceParams, CurrentPackageOnOtherRunOnWithAliasFlagIsLeftAlone) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_aliases, k_get_is_current_package, 3, 0,
                       k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "SubjectPackage.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 3u);
}

TEST(CtdaReferenceParams, AliasParamTwoLeftAloneWhileParamOneRemaps) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_aliases, k_get_faction_rank_difference, 0x00000812, 3,
                       k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "AliasParamTwo.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 0x01000812u);
  EXPECT_EQ(read_u32(ctda, 16), 3u);
}

TEST(CtdaReferenceParams, PackdataFlagLeavesReferenceParamTwoAloneWhileParamOneRemaps) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_pack_data, k_get_faction_rank_difference, 0x00000812, 3,
                       k_run_on_subject));
  const auto ctda = remapped_ctda(
      out, "PackdataParamTwo.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 0x01000812u);
  EXPECT_EQ(read_u32(ctda, 16), 3u);
}

TEST(CtdaReferenceParams, AliasAndPackdataOnQuestAliasPackageStillRemapsParamOne) {
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(fo4::k_ctda_flag_aliases | fo4::k_ctda_flag_pack_data,
                       k_get_is_current_package, 0x00000812, 0, k_run_on_quest_alias));
  const auto ctda = remapped_ctda(
      out, "AliasPackdataPackage.esp",
      {fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
       fo4::PluginKey::from_filename("Fallout4.esm")});
  EXPECT_EQ(read_u32(ctda, 12), 0x01000812u);
}
