#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t k_get_distance = 1;
constexpr std::uint16_t k_get_faction_rank_difference = 60;
constexpr std::uint16_t k_get_is_current_package = 161;
constexpr std::uint16_t k_get_in_worldspace = 310;
constexpr std::uint32_t k_run_on_subject = 0;
constexpr std::uint32_t k_run_on_reference = 2;
constexpr std::uint32_t k_run_on_quest_alias = 5;
constexpr std::uint16_t k_get_seconds_passed = 12;
constexpr std::uint32_t k_missing = 0xFFFFFFFFu;

std::vector<std::byte> make_ctda(std::uint8_t type, std::uint16_t function, std::uint32_t param1,
                                 std::uint32_t param2, std::uint32_t run_on,
                                 std::uint32_t reference = 0) {
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
  ctda.u32(reference);
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

struct CtdaCase {
  const char* name;
  bool masterless;
  std::uint32_t form_id;
  std::uint8_t type;
  std::uint16_t function;
  std::uint32_t param1;
  std::uint32_t param2;
  std::uint32_t run_on;
  std::uint32_t expected_param1;
  std::optional<std::uint32_t> expected_param2;
};

class CtdaReferenceParams : public testing::TestWithParam<CtdaCase> {};

}  // namespace

TEST_P(CtdaReferenceParams, RemapsParamsPerFlagsAndRunOn) {
  const auto& param = GetParam();
  SCOPED_TRACE(param.name);
  const std::string plugin_name = std::string(param.name) + ".esp";
  auto out = param.masterless ? masterless_plugin() : plugin_with_masters();
  write_cobj(out, param.form_id,
             make_ctda(param.type, param.function, param.param1, param.param2, param.run_on));
  const auto masters =
      param.masterless
          ? std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("Fallout4.esm")}
          : std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
                                        fo4::PluginKey::from_filename("Fallout4.esm")};
  const auto ctda = remapped_ctda(out, plugin_name, masters);
  EXPECT_EQ(read_u32(ctda, 12), param.expected_param1);
  if (param.expected_param2) {
    EXPECT_EQ(read_u32(ctda, 16), *param.expected_param2);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Rows, CtdaReferenceParams,
    testing::Values(
        CtdaCase{"AliasParamSurvivesMasterZeroMoving", false, 0x03000801, fo4::k_ctda_flag_aliases,
                 k_get_distance, 3, 0, k_run_on_subject, 3u, std::nullopt},
        CtdaCase{"AliasParamSurvivesMasterlessPluginGainingMaster", true, 0x00000801,
                 fo4::k_ctda_flag_aliases, k_get_distance, 3, 0, k_run_on_subject, 3u,
                 std::nullopt},
        CtdaCase{"ReferenceParamWithoutAliasFlagIsStillRemapped", false, 0x03000801, 0,
                 k_get_distance, 3, 0, k_run_on_subject, 0x01000003u, std::nullopt},
        CtdaCase{"GetInWorldspaceParamOneIsRemappedOnMasterReorder", false, 0x03000801, 0,
                 k_get_in_worldspace, 3, 0, k_run_on_subject, 0x01000003u, std::nullopt},
        CtdaCase{"PackdataFlagLeavesPackageParamAlone", false, 0x03000801,
                 fo4::k_ctda_flag_pack_data, k_get_is_current_package, 3, 0, k_run_on_subject, 3u,
                 std::nullopt},
        CtdaCase{"PackdataFlagLeavesReferenceParamAlone", false, 0x03000801,
                 fo4::k_ctda_flag_pack_data, k_get_distance, 3, 0, k_run_on_subject, 3u,
                 std::nullopt},
        CtdaCase{"CurrentPackageOnQuestAliasStillRemapsPackage", false, 0x03000801,
                 fo4::k_ctda_flag_aliases, k_get_is_current_package, 0x00000812, 0,
                 k_run_on_quest_alias, 0x01000812u, std::nullopt},
        CtdaCase{"CurrentPackageOnOtherRunOnWithAliasFlagIsLeftAlone", false, 0x03000801,
                 fo4::k_ctda_flag_aliases, k_get_is_current_package, 3, 0, k_run_on_subject, 3u,
                 std::nullopt},
        CtdaCase{"AliasParamTwoLeftAloneWhileParamOneRemaps", false, 0x03000801,
                 fo4::k_ctda_flag_aliases, k_get_faction_rank_difference, 0x00000812, 3,
                 k_run_on_subject, 0x01000812u, 3u},
        CtdaCase{"PackdataFlagLeavesReferenceParamTwoAloneWhileParamOneRemaps", false, 0x03000801,
                 fo4::k_ctda_flag_pack_data, k_get_faction_rank_difference, 0x00000812, 3,
                 k_run_on_subject, 0x01000812u, 3u},
        CtdaCase{"AliasAndPackdataOnQuestAliasPackageStillRemapsParamOne", false, 0x03000801,
                 fo4::k_ctda_flag_aliases | fo4::k_ctda_flag_pack_data,
                 k_get_is_current_package, 0x00000812, 0, k_run_on_quest_alias, 0x01000812u,
                 std::nullopt}),
    [](const testing::TestParamInfo<CtdaCase>& info) { return std::string(info.param.name); });

TEST(CtdaReferenceRunOn, ReferenceSlotIsAFormIdOnlyOnReferenceRunOn) {
  const std::string plugin_name = "ReferenceRunOn.esp";
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(0, k_get_seconds_passed, 0, 0, k_run_on_reference, 0x00000812));
  const auto masters = std::vector<fo4::PluginKey>{
      fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
      fo4::PluginKey::from_filename("Fallout4.esm")};
  const auto ctda = remapped_ctda(out, plugin_name, masters);
  ASSERT_EQ(ctda.size(), 32u);
  EXPECT_EQ(read_u32(ctda, 24), 0x01000812u);
}

TEST(CtdaReferenceRunOn, ReferenceSlotIsLeftAloneOnOtherRunOn) {
  const std::string plugin_name = "StaleReference.esp";
  auto out = plugin_with_masters();
  write_cobj(out, 0x03000801,
             make_ctda(0, k_get_seconds_passed, 0, 0, k_run_on_subject, 0x00000812));
  const auto masters = std::vector<fo4::PluginKey>{
      fo4::PluginKey::from_filename("RadiationOverhaul.esp"),
      fo4::PluginKey::from_filename("Fallout4.esm")};
  const auto ctda = remapped_ctda(out, plugin_name, masters);
  ASSERT_EQ(ctda.size(), 32u);
  EXPECT_EQ(read_u32(ctda, 24), 0x00000812u);
}
