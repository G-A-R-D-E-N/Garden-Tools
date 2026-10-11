#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void wstring(fo4::test::ByteWriter& out, std::string_view text) {
  out.u16(static_cast<std::uint16_t>(text.size()));
  out.raw(text);
}

void object_v2(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  out.u16(0);
  out.u16(0xFFFF);
  out.u32(form_id);
}

fo4::test::ByteWriter plugin_with_masters() {
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
  fo4::test::write_subrecord(out, "MAST", std::string_view{"DropMe.esm\0", 11});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"KeepMe.esm\0", 11});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  return out;
}

std::vector<std::byte> make_plugin(std::uint8_t unsupported_type) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "VariableScript");
  vmad.u8(0);
  vmad.u16(2);

  wstring(vmad, "KnownRef");
  vmad.u8(1);
  vmad.u8(1);
  object_v2(vmad, 0x0200009A);

  wstring(vmad, "DynamicValue");
  vmad.u8(unsupported_type);
  vmad.u8(1);
  // Deliberately include FormID-looking bytes after the unsupported type. The
  // scanner must not guess their meaning or retain the preceding partial hit.
  vmad.u32(0x0200009B);
  vmad.u32(0x0200009C);

  auto out = plugin_with_masters();
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

void expect_remap_refused(std::uint8_t type) {
  auto plugin = fo4::read_plugin(make_plugin(type), "VariablePatch.esp");

  const auto links = fo4::collect_form_links(plugin);
  for (const auto& link : links) {
    EXPECT_NE(link.subrecord.to_string(), "VMAD");
  }

  EXPECT_THROW(
      {
        plugin = fo4::remap_plugin_masters(
            std::move(plugin),
            {fo4::PluginKey::from_filename("Fallout4.esm"),
             fo4::PluginKey::from_filename("KeepMe.esm")});
      },
      fo4::FormIdError);
}

fo4::test::ByteWriter object_word_vmad(std::uint16_t format, std::uint16_t word) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(format);
  vmad.u16(1);
  wstring(vmad, "WordScript");
  vmad.u8(0);
  vmad.u16(3);
  wstring(vmad, "Before");
  vmad.u8(3);
  vmad.u8(1);
  vmad.u32(7);
  wstring(vmad, "Target");
  vmad.u8(1);
  vmad.u8(1);
  if (format == 2) vmad.u16(word);
  if (format == 1) vmad.u32(0x0200009A);
  vmad.u16(0xFFFF);
  if (format == 1) vmad.u16(word);
  if (format == 2) vmad.u32(0x0200009A);
  wstring(vmad, "After");
  vmad.u8(3);
  vmad.u8(1);
  vmad.u32(9);
  return vmad;
}

}

TEST(Vmad, UnexposedScalarObjectWordsStopTheTypedReader) {
  for (const auto format : {std::uint16_t{1}, std::uint16_t{2}}) {
    for (const auto word : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0xFFFF}}) {
      SCOPED_TRACE("format=" + std::to_string(format) + " word=" + std::to_string(word));
      const auto vmad = object_word_vmad(format, word);
      const auto scripts = fo4::read_vmad_scripts(vmad.bytes);
      ASSERT_EQ(scripts.scripts.size(), 1u);
      EXPECT_EQ(scripts.unsupported, word != 0);
      const auto& properties = scripts.scripts[0].properties;
      EXPECT_EQ(properties.size(), word == 0 ? 3u : 1u);
      ASSERT_FALSE(properties.empty());
      EXPECT_EQ(properties[0].name, "Before");
      EXPECT_EQ(properties[0].integer, 7);
      if (word == 0) {
        ASSERT_EQ(properties.size(), 3u);
        EXPECT_EQ(properties[1].object->value(), 0x0200009Au);
        EXPECT_EQ(properties[1].alias, -1);
        EXPECT_EQ(properties[2].integer, 9);
      }
      auto out = plugin_with_masters();
      const auto group = fo4::test::begin_group(out, "REFR", 0);
      const auto record = fo4::test::begin_record(out, "REFR", 0, 0x03000800);
      fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
      fo4::test::end_record(out, record);
      fo4::test::end_group(out, group);
      const auto plugin = fo4::read_plugin(out.bytes, "WordPatch.esp");
      EXPECT_EQ(fo4::write_plugin(plugin), out.bytes);
    }
  }
}

TEST(Vmad, UnexposedScalarObjectWordsStopTheScanPath) {
  for (const auto format : {std::uint16_t{1}, std::uint16_t{2}}) {
    for (const auto word : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0xFFFF}}) {
      SCOPED_TRACE("format=" + std::to_string(format) + " word=" + std::to_string(word));
      const auto vmad = object_word_vmad(format, word);
      const auto refr = fo4::Signature::from_string("REFR");
      const auto coverage = fo4::vmad_coverage(vmad.bytes, refr);
      EXPECT_EQ(coverage.unsupported, word != 0);
      EXPECT_EQ(coverage.complete(), word == 0);
      EXPECT_EQ(fo4::vmad_formid_offsets_for_remap(vmad.bytes, refr).has_value(), word == 0);
    }
  }
}

TEST(Vmad, VariablePropertyFailsClosedForMasterRemap) {
  expect_remap_refused(6);
}

TEST(Vmad, VariableArrayPropertyFailsClosedForMasterRemap) {
  expect_remap_refused(16);
}

TEST(Vmad, FragmentCountsArePublishedOnlyForCompleteCoverage) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(0);
  vmad.u8(3);
  wstring(vmad, "Fragments");
  vmad.u8(0);
  vmad.u16(0);
  vmad.u16(1);
  vmad.u16(0);
  vmad.u16(0);
  vmad.u8(0);
  wstring(vmad, "Fragments");
  wstring(vmad, "Fragment");
  const auto complete = fo4::vmad_coverage(vmad.bytes, fo4::sig_term);
  ASSERT_TRUE(complete.complete());
  EXPECT_EQ(complete.fragment_count, 1u);
  EXPECT_EQ(complete.fragment_script_count, 1u);
  auto truncated = vmad.bytes;
  truncated.pop_back();
  auto trailing = vmad.bytes;
  trailing.push_back(std::byte{0});
  for (const auto& data : {truncated, trailing}) {
    const auto incomplete = fo4::vmad_coverage(data, fo4::sig_term);
    EXPECT_FALSE(incomplete.complete());
    EXPECT_EQ(incomplete.fragment_count, 0u);
    EXPECT_EQ(incomplete.fragment_script_count, 0u);
  }
}

TEST(Vmad, InfoFragmentCensusCountsOnlyDefinedFlagBits) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(0);
  vmad.u8(0);
  vmad.u8(0x04);
  wstring(vmad, "Fragments");
  vmad.u8(0);
  vmad.u16(0);
  const auto coverage = fo4::vmad_coverage(vmad.bytes, fo4::sig_info);
  ASSERT_TRUE(coverage.complete());
  EXPECT_EQ(coverage.fragment_count, 0u);
  EXPECT_EQ(coverage.fragment_script_count, 1u);
}
