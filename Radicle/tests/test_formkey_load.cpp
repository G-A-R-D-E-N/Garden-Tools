#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

std::filesystem::path testdata(const char* name) {
  return std::filesystem::path{FO4_TESTDATA_DIR} / name;
}

}  // namespace

TEST(FormKeyLoad, LocalRecordResolvesAgainstThePluginItself) {
  auto plugin = fo4::read_plugin_file(testdata("FW_HeatherSurvivalPatch.esp"));
  const auto* mgef = fo4::find_record(plugin, fo4::RawFormId{0x03000800});
  ASSERT_NE(mgef, nullptr);
  EXPECT_EQ(mgef->form_key, fo4::FormKey::parse("000800:FW_HeatherSurvivalPatch.esp"));
}

TEST(FormKeyLoad, OverrideResolvesAgainstTheOwningMaster) {
  auto plugin = fo4::read_plugin_file(testdata("FW_HeatherSurvivalPatch.esp"));
  const auto* tea = fo4::find_record(plugin, fo4::RawFormId{0x010EC5B7});
  ASSERT_NE(tea, nullptr);
  EXPECT_EQ(tea->form_key, fo4::FormKey::parse("0EC5B7:llamaCompanionHeatherv2.esp"));
}

TEST(FormKeyLoad, EspSmallFileSetsCurrentKindLight) {
  fo4::test::ByteWriter out;
  const auto tes4 =
      fo4::test::begin_record(out, "TES4", fo4::RecordFlags::small_file, 0);
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
  fo4::test::end_record(out, tes4);

  auto plugin = fo4::read_plugin(out.bytes, "FlaggedLight.esp");
  EXPECT_FALSE(plugin.key.looks_light());
  EXPECT_TRUE(plugin.header.tes4_flags.is_small_file());
  const auto ctx = fo4::plugin_form_id_context(plugin);
  EXPECT_EQ(ctx.current_kind, fo4::PluginKind::light);
  EXPECT_EQ(ctx.masters[0].kind, fo4::PluginKind::full);
}

TEST(FormKeyLoad, EslExtensionWithoutLightFlagSetsCurrentKindLight) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  auto plugin = fo4::read_plugin(out.bytes, "Extension.esl");
  EXPECT_FALSE(plugin.header.tes4_flags.is_small_file());
  EXPECT_EQ(fo4::plugin_form_id_context(plugin).current_kind, fo4::PluginKind::light);
}

TEST(FormKeyLoad, FindRecordByFormKey) {
  auto plugin = fo4::read_plugin_file(testdata("FW_HeatherSurvivalPatch.esp"));
  const auto* spell =
      fo4::find_record(plugin, fo4::FormKey::parse("000801:FW_HeatherSurvivalPatch.esp"));
  ASSERT_NE(spell, nullptr);
  EXPECT_EQ(spell->header.signature.to_string(), "SPEL");
}
