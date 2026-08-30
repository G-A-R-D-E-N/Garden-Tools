#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <span>
#include <string_view>

namespace {

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t offset) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + offset, 4);
  return value;
}

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

void object_v2(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  out.u16(0);       // unused
  out.u16(0xFFFF);  // alias none
  out.u32(form_id);
}

void object_v1(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  out.u32(form_id);
  out.u16(0xFFFF);
  out.u16(0);
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

const fo4::Subrecord* find_vmad(const fo4::Plugin& plugin, fo4::FormKey key) {
  const auto* rec = fo4::find_record(plugin, key);
  if (rec == nullptr) {
    return nullptr;
  }
  for (const auto& sub : rec->subrecords) {
    if (sub.signature.to_string() == "VMAD") {
      return &sub;
    }
  }
  return nullptr;
}

fo4::Plugin remap_drop_dlc(std::vector<std::byte> bytes, std::string_view name) {
  auto plugin = fo4::read_plugin(bytes, name);
  return fo4::remap_plugin_masters(
      std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                          fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
}

}  // namespace

TEST(Vmad, ObjectFormat2PropertyRemapsFormId) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);  // version
  vmad.u16(2);  // object format
  vmad.u16(1);  // scripts
  wstring(vmad, "DemoScript");
  vmad.u8(0);
  vmad.u16(1);  // properties
  wstring(vmad, "LinkedRef");
  vmad.u8(1);  // object
  vmad.u8(1);  // edited
  object_v2(vmad, 0x0200009A);

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ACTI", 0);
  const auto rec = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(remap_drop_dlc(out.bytes, "Patch.esp")), "Patch.esp");
  const auto* sub = find_vmad(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, sub->data.size() - 4), 0x0100009Au);
}

TEST(Vmad, ObjectFormat1PropertyRemapsFormId) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(1);
  vmad.u16(1);
  wstring(vmad, "OldFormat");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Item");
  vmad.u8(1);
  vmad.u8(1);
  object_v1(vmad, 0x0200009A);

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ACTI", 0);
  const auto rec = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(remap_drop_dlc(out.bytes, "Patch.esp")), "Patch.esp");
  const auto* sub = find_vmad(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, 2 + 2 + 2 + 2 + 9 + 1 + 2 + 2 + 4 + 1 + 1), 0x0100009Au);
}

TEST(Vmad, IntPropertyIsNotAFormId) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "CountScript");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Count");
  vmad.u8(3);  // int
  vmad.u8(1);
  vmad.u32(0x0200009A);

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ACTI", 0);
  const auto rec = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(remap_drop_dlc(out.bytes, "Patch.esp")), "Patch.esp");
  const auto* sub = find_vmad(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, sub->data.size() - 4), 0x0200009Au);
}

TEST(Vmad, ObjectArrayRemapsEachElement) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "ListScript");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Refs");
  vmad.u8(11);  // array of object
  vmad.u8(1);
  vmad.u32(2);
  object_v2(vmad, 0x00000007);
  object_v2(vmad, 0x0200009A);

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ACTI", 0);
  const auto rec = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(remap_drop_dlc(out.bytes, "Patch.esp")), "Patch.esp");
  const auto* sub = find_vmad(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, sub->data.size() - 4), 0x0100009Au);
  EXPECT_EQ(read_u32(sub->data, sub->data.size() - 12), 0x00000007u);
}

TEST(Vmad, StructMemberObjectRemaps) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "StructScript");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Pair");
  vmad.u8(7);  // struct
  vmad.u8(1);
  vmad.u32(1);  // member count
  wstring(vmad, "Target");
  vmad.u8(1);
  vmad.u8(1);
  object_v2(vmad, 0x0200009A);

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ACTI", 0);
  const auto rec = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(remap_drop_dlc(out.bytes, "Patch.esp")), "Patch.esp");
  const auto* sub = find_vmad(reread, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, sub->data.size() - 4), 0x0100009Au);
}

TEST(Vmad, QuestAliasObjectRemaps) {
  fo4::test::ByteWriter vmad;
  vmad.u16(6);
  vmad.u16(2);
  vmad.u16(0);  // no scripts
  vmad.u8(3);   // extra bind
  vmad.u16(0);  // fragment count
  vmad.u16(0);  // empty fragment script name
  vmad.u16(1);  // one alias
  object_v2(vmad, 0x0200009A);
  vmad.u16(6);
  vmad.u16(2);
  vmad.u16(0);  // alias scripts

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "QUST", 0);
  const auto rec = fo4::test::begin_record(out, "QUST", 0, 0x03000810);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto reread = fo4::read_plugin(
      fo4::write_plugin(remap_drop_dlc(out.bytes, "Patch.esp")), "Patch.esp");
  const auto* sub = find_vmad(reread, fo4::FormKey::parse("000810:Patch.esp"));
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(read_u32(sub->data, 2 + 2 + 2 + 1 + 2 + 2 + 2 + 4), 0x0100009Au);
}

TEST(Vmad, QuestBindingsDetectStageFragmentsAndAliasObjects) {
  fo4::test::ByteWriter fragments;
  fragments.u16(6);
  fragments.u16(2);
  fragments.u16(0);
  fragments.u8(3);
  fragments.u16(1);
  fragments.u16(0);
  fragments.u16(10);
  fragments.u16(0);
  fragments.u32(0);
  fragments.u8(0);
  wstring(fragments, "QF_Test");
  wstring(fragments, "Fragment_10");
  fragments.u16(0);
  const auto fragment_bindings = fo4::quest_vmad_bindings(fragments.bytes);
  EXPECT_FALSE(fragment_bindings.unparseable);
  EXPECT_TRUE(fragment_bindings.binds_stages);
  EXPECT_FALSE(fragment_bindings.binds_aliases);

  fo4::test::ByteWriter aliases;
  aliases.u16(6);
  aliases.u16(2);
  aliases.u16(0);
  aliases.u8(3);
  aliases.u16(0);
  aliases.u16(0);
  aliases.u16(1);
  object_v2(aliases, 0x00000014);
  aliases.u16(6);
  aliases.u16(2);
  aliases.u16(0);
  const auto alias_bindings = fo4::quest_vmad_bindings(aliases.bytes);
  EXPECT_FALSE(alias_bindings.unparseable);
  EXPECT_FALSE(alias_bindings.binds_stages);
  EXPECT_TRUE(alias_bindings.binds_aliases);

  fo4::test::ByteWriter empty_extras;
  empty_extras.u16(6);
  empty_extras.u16(2);
  empty_extras.u16(0);
  empty_extras.u8(3);
  empty_extras.u16(0);
  empty_extras.u16(0);
  empty_extras.u16(0);
  const auto empty_bindings = fo4::quest_vmad_bindings(empty_extras.bytes);
  EXPECT_FALSE(empty_bindings.unparseable);
  EXPECT_FALSE(empty_bindings.binds_stages);
  EXPECT_FALSE(empty_bindings.binds_aliases);

  fo4::test::ByteWriter truncated;
  truncated.u16(6);
  truncated.u16(2);
  truncated.u16(1);
  truncated.u16(20);
  truncated.raw(std::string_view{"x"});
  const auto truncated_bindings = fo4::quest_vmad_bindings(truncated.bytes);
  EXPECT_TRUE(truncated_bindings.unparseable);
  EXPECT_FALSE(truncated_bindings.binds_stages);
  EXPECT_FALSE(truncated_bindings.binds_aliases);

  empty_extras.u8(0);
  const auto trailing_bindings = fo4::quest_vmad_bindings(empty_extras.bytes);
  EXPECT_TRUE(trailing_bindings.unparseable);
  EXPECT_FALSE(trailing_bindings.binds_stages);
  EXPECT_FALSE(trailing_bindings.binds_aliases);
}

TEST(Vmad, InspectQuestExtrasReportsAliasAndFragmentOffsets) {
  fo4::test::ByteWriter aliases;
  aliases.u16(6);
  aliases.u16(2);
  aliases.u16(0);
  aliases.u8(3);
  aliases.u16(0);
  aliases.u16(0);
  aliases.u16(1);
  aliases.u16(0);
  aliases.u16(1);
  aliases.u32(0x00000014);
  aliases.u16(6);
  aliases.u16(2);
  aliases.u16(0);
  const auto alias_layout = fo4::inspect_quest_vmad(aliases.bytes);
  ASSERT_FALSE(alias_layout.unparseable);
  ASSERT_EQ(alias_layout.aliases.size(), 1u);
  EXPECT_EQ(alias_layout.aliases[0].alias_id, 1);
  std::int16_t alias_id = 0;
  std::memcpy(&alias_id, aliases.bytes.data() + alias_layout.aliases[0].alias_id_offset, 2);
  EXPECT_EQ(alias_id, 1);

  fo4::test::ByteWriter fragments;
  fragments.u16(6);
  fragments.u16(2);
  fragments.u16(0);
  fragments.u8(3);
  fragments.u16(1);
  fragments.u16(0);
  fragments.u16(10);
  fragments.u16(0);
  fragments.u32(0);
  fragments.u8(0);
  wstring(fragments, "QF_Test");
  wstring(fragments, "Fragment_10");
  fragments.u16(0);
  const auto fragment_layout = fo4::inspect_quest_vmad(fragments.bytes);
  ASSERT_FALSE(fragment_layout.unparseable);
  ASSERT_EQ(fragment_layout.fragments.size(), 1u);
  EXPECT_EQ(fragment_layout.fragments[0].stage, 10u);
  EXPECT_EQ(fragment_layout.fragments[0].stage_index, 0);
}

TEST(Vmad, TruncatedPayloadDoesNotThrow) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  vmad.u16(20);  // claims a long name
  vmad.raw(std::string_view{"x"});

  auto out = plugin_with_masters();
  const auto grup = fo4::test::begin_group(out, "ACTI", 0);
  const auto rec = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  EXPECT_NO_THROW({
    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    plugin = fo4::remap_plugin_masters(
        std::move(plugin), {fo4::PluginKey::from_filename("Fallout4.esm"),
                            fo4::PluginKey::from_filename("RadiationOverhaul.esp")});
    (void)fo4::write_plugin(plugin);
  });
}
