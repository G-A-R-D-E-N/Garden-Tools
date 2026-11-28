#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(bool with_edid) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  if (with_edid) {
    fo4::test::write_subrecord(out, "EDID", std::string_view{"OldEditorId\0", 12});
  }
  fo4::test::write_subrecord(out, "FULL", std::string_view{"Preserved\0", 10});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> edid_data(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::sig_edid) return subrecord.data;
  }
  return {};
}

}  // namespace

TEST(EditorIdText, RefusesEmbeddedNul) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_THROW(fo4::set_record_editor_id(plugin, key, std::string_view{"abc\0def", 7}),
               fo4::FormIdError);
}

TEST(EditorIdText, StoresUtf8AsWindows1252WhenReplacing) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "Caf\xC3\xA9"));
  const std::vector<std::byte> expected{
      std::byte{0x43}, std::byte{0x61}, std::byte{0x66}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(edid_data(plugin, key), expected);
}

TEST(EditorIdText, StoresUtf8AsWindows1252WhenInserting) {
  auto plugin = fo4::read_plugin(make_plugin(false), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "Caf\xC3\xA9"));
  const std::vector<std::byte> expected{
      std::byte{0x43}, std::byte{0x61}, std::byte{0x66}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(edid_data(plugin, key), expected);
}

TEST(EditorIdText, RefusesCharactersOutsideWindows1252) {
  auto plugin = fo4::read_plugin(make_plugin(false), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_THROW(fo4::set_record_editor_id(plugin, key, "Snow\xE2\x98\x83"), fo4::FormIdError);
  EXPECT_TRUE(edid_data(plugin, key).empty());
}
