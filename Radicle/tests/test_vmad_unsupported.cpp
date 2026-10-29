#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "fo4/schema/vmad.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
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

std::vector<std::byte> make_plugin(std::uint8_t unsupported_type,
                                   bool nested = false,
                                   std::uint32_t array_count = 1,
                                   bool following_reference = false) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "VariableScript");
  vmad.u8(0);
  vmad.u16(following_reference ? 3 : 2);

  wstring(vmad, "KnownRef");
  vmad.u8(1);
  vmad.u8(1);
  object_v2(vmad, 0x0200009A);

  if (nested) {
    wstring(vmad, "Container");
    vmad.u8(7);
    vmad.u8(1);
    vmad.u32(1);
  }
  wstring(vmad, "DynamicValue");
  vmad.u8(unsupported_type);
  vmad.u8(1);
  if (unsupported_type == 16) vmad.u32(array_count);
  if (unsupported_type != 16 || array_count != 0) {
    vmad.u32(0x0200009B);
    vmad.u32(0x0200009C);
  }
  if (following_reference) {
    wstring(vmad, "FollowingRef");
    vmad.u8(1);
    vmad.u8(1);
    object_v2(vmad, 0x0200009D);
  }

  auto out = plugin_with_masters();
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

void expect_remap_refused(std::uint8_t type, bool nested = false,
                          std::uint32_t array_count = 1) {
  const auto bytes = make_plugin(type, nested, array_count);
  const auto plugin = fo4::read_plugin(bytes, "VariablePatch.esp");
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
  const auto* record = fo4::find_record(
      plugin, fo4::FormKey::parse("000800:VariablePatch.esp"));
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_TRUE(fo4::vmad_has_unsupported_properties(
      record->subrecords[0].data, record->header.signature));

  const auto links = fo4::collect_form_links(plugin);
  ASSERT_EQ(links.size(), 1u);
  EXPECT_EQ(links[0].subrecord.to_string(), "VMAD");

  EXPECT_THROW(
      {
        fo4::remap_plugin_masters(
            plugin,
            {fo4::PluginKey::from_filename("Fallout4.esm"),
             fo4::PluginKey::from_filename("KeepMe.esm")});
      },
      fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
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

fo4::test::ByteWriter quest_alias_word_vmad(std::uint16_t format, std::uint16_t word) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(format);
  vmad.u16(0);
  vmad.u8(0);
  vmad.u16(0);
  vmad.u16(0);
  vmad.u16(1);
  if (format == 2) vmad.u16(word);
  if (format == 1) vmad.u32(0x0200009A);
  vmad.u16(3);
  if (format == 1) vmad.u16(word);
  if (format == 2) vmad.u32(0x0200009A);
  vmad.u16(0xFFFF);
  vmad.u16(format);
  vmad.u16(0);
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

TEST(Vmad, UnknownPropertyTypesStopTheTypedReaderLikeVariables) {
  for (const auto type : {std::uint8_t{8}, std::uint8_t{9}, std::uint8_t{10}, std::uint8_t{18},
                          std::uint8_t{255}}) {
    SCOPED_TRACE("type=" + std::to_string(type));
    fo4::test::ByteWriter vmad;
    vmad.u16(5);
    vmad.u16(2);
    vmad.u16(1);
    wstring(vmad, "UnknownScript");
    vmad.u8(0);
    vmad.u16(3);
    wstring(vmad, "Before");
    vmad.u8(3);
    vmad.u8(1);
    vmad.u32(7);
    wstring(vmad, "Unknown");
    vmad.u8(type);
    vmad.u8(1);
    wstring(vmad, "After");
    vmad.u8(3);
    vmad.u8(1);
    vmad.u32(9);
    const auto scripts = fo4::read_vmad_scripts(vmad.bytes);
    ASSERT_EQ(scripts.scripts.size(), 1u);
    EXPECT_EQ(scripts.scripts[0].name, "UnknownScript");
    EXPECT_TRUE(scripts.unsupported);
    const auto& properties = scripts.scripts[0].properties;
    ASSERT_EQ(properties.size(), 1u);
    EXPECT_EQ(properties[0].name, "Before");
    EXPECT_EQ(properties[0].integer, 7);
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

TEST(Vmad, UnsupportedPropertyTypesRefuseMasterRemap) {
  struct Row {
    std::uint8_t type;
    bool nested;
  };
  const Row rows[] = {{6, false}, {16, false}, {8, false}, {6, true}, {16, true}};
  for (const auto& row : rows) {
    SCOPED_TRACE(std::to_string(row.type) + (row.nested ? " nested" : " top level"));
    expect_remap_refused(row.type, row.nested);
  }
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

TEST(Vmad, EmptyLeadingScriptNameKeepsFlagsPropertiesAndOffsets) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(2);
  wstring(vmad, "");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Count");
  vmad.u8(3);
  vmad.u8(0);
  vmad.u32(42);
  wstring(vmad, "Named");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Target");
  vmad.u8(1);
  vmad.u8(0);
  object_v2(vmad, 0x0200009A);
  const auto scripts = fo4::read_vmad_scripts(vmad.bytes);
  EXPECT_FALSE(scripts.unsupported);
  EXPECT_EQ(scripts.trailing_bytes, 0u);
  ASSERT_EQ(scripts.scripts.size(), 2u);
  EXPECT_EQ(scripts.scripts[0].name, "");
  ASSERT_EQ(scripts.scripts[0].properties.size(), 1u);
  EXPECT_EQ(scripts.scripts[0].properties[0].name, "Count");
  EXPECT_EQ(scripts.scripts[0].properties[0].integer, 42);
  EXPECT_EQ(scripts.scripts[1].name, "Named");
  ASSERT_EQ(scripts.scripts[1].properties.size(), 1u);
  EXPECT_EQ(scripts.scripts[1].properties[0].object->value(), 0x0200009Au);
  const auto refr = fo4::Signature::from_string("REFR");
  EXPECT_TRUE(fo4::vmad_coverage(vmad.bytes, refr).complete());
  const auto offsets = fo4::vmad_formid_offsets(vmad.bytes, refr);
  ASSERT_EQ(offsets.size(), 1u);
  EXPECT_EQ(offsets[0], vmad.bytes.size() - 4);
}

TEST(Vmad, EmptyQuestFragmentHeaderHasNoFlagsOrProperties) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(0);
  vmad.u8(0);
  vmad.u16(1);
  vmad.u16(0);
  vmad.u16(0);
  vmad.u16(0);
  vmad.u32(0);
  vmad.u8(0);
  wstring(vmad, "");
  wstring(vmad, "Fragment");
  vmad.u16(0);
  const auto extras =
      fo4::read_quest_vmad_extras(std::span<const std::byte>(vmad.bytes).subspan(6), 2);
  EXPECT_FALSE(extras.unsupported);
  EXPECT_TRUE(extras.fully_consumed);
  EXPECT_EQ(extras.script_name, "");
  EXPECT_TRUE(extras.properties.empty());
  ASSERT_EQ(extras.fragments.size(), 1u);
  EXPECT_EQ(extras.fragments[0].function, "Fragment");
  EXPECT_TRUE(extras.aliases.empty());
  const auto coverage = fo4::vmad_coverage(vmad.bytes, fo4::sig_qust);
  EXPECT_TRUE(coverage.complete());
  EXPECT_EQ(coverage.fragment_count, 1u);
  EXPECT_EQ(coverage.fragment_script_count, 0u);
}

TEST(Vmad, EmptyVariableArrayPreservesValuesAndRemapsFollowingReferences) {
  for (const bool nested : {false, true}) {
    const auto bytes = make_plugin(16, nested, 0, true);
    const auto plugin = fo4::read_plugin(bytes, "VariablePatch.esp");
    EXPECT_EQ(fo4::write_plugin(plugin), bytes);
    const auto key = fo4::FormKey::parse("000800:VariablePatch.esp");
    const auto* record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr);
    const auto& original = record->subrecords[0].data;
    ASSERT_TRUE(fo4::vmad_coverage(original, record->header.signature).complete());
    const auto scripts = fo4::read_vmad_scripts(original);
    EXPECT_FALSE(scripts.unsupported);
    ASSERT_EQ(scripts.scripts.size(), 1u);
    ASSERT_EQ(scripts.scripts[0].properties.size(), 3u);
    EXPECT_EQ(scripts.scripts[0].properties[1].element_count, nested ? 1u : 0u);
    EXPECT_EQ(scripts.scripts[0].properties[2].object, fo4::RawFormId{0x0200009D});
    const auto offsets = fo4::vmad_formid_offsets_for_remap(original, record->header.signature);
    ASSERT_TRUE(offsets.has_value());
    ASSERT_EQ(offsets->size(), 2u);
    const auto remapped = fo4::remap_plugin_masters(
        plugin, {fo4::PluginKey::from_filename("Fallout4.esm"),
                 fo4::PluginKey::from_filename("KeepMe.esm")});
    const auto reparsed = fo4::read_plugin(fo4::write_plugin(remapped), "VariablePatch.esp");
    const auto* edited = fo4::find_record(reparsed, key);
    ASSERT_NE(edited, nullptr);
    auto expected = original;
    for (const auto offset : *offsets) expected[offset + 3] = std::byte{1};
    EXPECT_EQ(edited->subrecords[0].data, expected);
    const auto decoded = fo4::read_vmad_scripts(edited->subrecords[0].data);
    EXPECT_EQ(decoded.scripts[0].properties[0].object, fo4::RawFormId{0x0100009A});
    EXPECT_EQ(decoded.scripts[0].properties[2].object, fo4::RawFormId{0x0100009D});
  }
}

TEST(Vmad, EmptyVariableArrayDoesNotPermitUnknownTailOrTruncatedCountRemap) {
  const auto bytes = make_plugin(16, false, 0);
  const auto plugin = fo4::read_plugin(bytes, "VariablePatch.esp");
  const auto* record = fo4::find_record(plugin, fo4::FormKey::parse("000800:VariablePatch.esp"));
  ASSERT_NE(record, nullptr);
  const auto& payload = record->subrecords[0].data;
  auto tail = payload;
  tail.push_back(std::byte{0xEF});
  EXPECT_TRUE(fo4::vmad_coverage(tail, record->header.signature).trailing_data);
  EXPECT_FALSE(fo4::vmad_formid_offsets_for_remap(tail, record->header.signature).has_value());
  for (std::size_t missing = 1; missing <= 4; ++missing) {
    const auto truncated = std::span<const std::byte>(payload).first(payload.size() - missing);
    EXPECT_TRUE(fo4::vmad_coverage(truncated, record->header.signature).failed);
    EXPECT_FALSE(fo4::vmad_formid_offsets_for_remap(truncated, record->header.signature).has_value());
  }
}

TEST(Vmad, QuestAliasUnusedWordsStopTheScanPath) {
  for (const auto format : {std::uint16_t{1}, std::uint16_t{2}}) {
    for (const auto word : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0xFFFF}}) {
      SCOPED_TRACE("format=" + std::to_string(format) + " word=" + std::to_string(word));
      const auto vmad = quest_alias_word_vmad(format, word);
      const auto coverage = fo4::vmad_coverage(vmad.bytes, fo4::sig_qust);
      EXPECT_EQ(coverage.unsupported, word != 0);
      EXPECT_EQ(coverage.complete(), word == 0);
      const auto offsets = fo4::vmad_formid_offsets_for_remap(vmad.bytes, fo4::sig_qust);
      EXPECT_EQ(offsets.has_value(), word == 0);
      if (word == 0) {
        ASSERT_TRUE(offsets.has_value());
        ASSERT_EQ(offsets->size(), 1u);
        EXPECT_EQ((*offsets)[0], vmad.bytes.size() - (format == 2 ? 10u : 14u));
      }
    }
  }
}

TEST(Vmad, QuestAliasUnusedWordsStopTheTypedReader) {
  for (const auto format : {std::uint16_t{1}, std::uint16_t{2}}) {
    for (const auto word : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{0xFFFF}}) {
      SCOPED_TRACE("format=" + std::to_string(format) + " word=" + std::to_string(word));
      const auto vmad = quest_alias_word_vmad(format, word);
      const auto extras = fo4::read_quest_vmad_extras(
          std::span<const std::byte>(vmad.bytes).subspan(6), static_cast<std::int16_t>(format));
      EXPECT_EQ(extras.unsupported, word != 0);
      if (word == 0) {
        EXPECT_TRUE(extras.fully_consumed);
        ASSERT_EQ(extras.aliases.size(), 1u);
        EXPECT_EQ(extras.aliases[0].alias_id, 3);
      }
    }
  }
}

TEST(Vmad, VariablePropertyStopsTheScriptAndLeavesFollowingBytesUninterpreted) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "VariableScript");
  vmad.u8(0);
  vmad.u16(3);
  wstring(vmad, "Before");
  vmad.u8(3);
  vmad.u8(1);
  vmad.u32(7);
  wstring(vmad, "Dynamic");
  vmad.u8(6);
  vmad.u8(1);
  wstring(vmad, "After");
  vmad.u8(3);
  vmad.u8(1);
  vmad.u32(9);
  const auto scripts = fo4::read_vmad_scripts(vmad.bytes);
  EXPECT_TRUE(scripts.unsupported);
  EXPECT_EQ(scripts.trailing_bytes, 0u);
  ASSERT_EQ(scripts.scripts.size(), 1u);
  ASSERT_EQ(scripts.scripts[0].properties.size(), 1u);
  EXPECT_EQ(scripts.scripts[0].properties[0].name, "Before");
  EXPECT_EQ(scripts.scripts[0].properties[0].integer, 7);
  const auto refr = fo4::Signature::from_string("REFR");
  EXPECT_TRUE(fo4::vmad_coverage(vmad.bytes, refr).unsupported);
  EXPECT_FALSE(fo4::vmad_formid_offsets_for_remap(vmad.bytes, refr).has_value());
}

TEST(Vmad, VariablePropertyRefusesTheWholeVmadIncludingLaterScriptsAndTail) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(2);
  wstring(vmad, "FirstScript");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Dynamic");
  vmad.u8(6);
  vmad.u8(1);
  wstring(vmad, "SecondScript");
  vmad.u8(0);
  vmad.u16(1);
  wstring(vmad, "Target");
  vmad.u8(1);
  vmad.u8(1);
  object_v2(vmad, 0x0200009A);
  vmad.u8(0xAB);
  vmad.u8(0xCD);
  const auto scripts = fo4::read_vmad_scripts(vmad.bytes);
  EXPECT_TRUE(scripts.unsupported);
  ASSERT_EQ(scripts.scripts.size(), 1u);
  EXPECT_EQ(scripts.scripts[0].name, "FirstScript");
  EXPECT_TRUE(scripts.scripts[0].properties.empty());
  const auto refr = fo4::Signature::from_string("REFR");
  EXPECT_TRUE(fo4::vmad_coverage(vmad.bytes, refr).unsupported);
  EXPECT_FALSE(fo4::vmad_formid_offsets_for_remap(vmad.bytes, refr).has_value());

  auto out = plugin_with_masters();
  const auto group = fo4::test::begin_group(out, "ACTI", 0);
  const auto record = fo4::test::begin_record(out, "ACTI", 0, 0x03000800);
  fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto plugin = fo4::read_plugin(out.bytes, "VariablePatch.esp");
  EXPECT_THROW(
      {
        fo4::remap_plugin_masters(
            plugin,
            {fo4::PluginKey::from_filename("Fallout4.esm"),
             fo4::PluginKey::from_filename("KeepMe.esm")});
      },
      fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), out.bytes);
}

TEST(Vmad, EmptyVariableArrayThenTailParsesTheTailAsTrailingBytes) {
  fo4::test::ByteWriter vmad;
  vmad.u16(5);
  vmad.u16(2);
  vmad.u16(1);
  wstring(vmad, "VariableScript");
  vmad.u8(0);
  vmad.u16(2);
  wstring(vmad, "Before");
  vmad.u8(3);
  vmad.u8(1);
  vmad.u32(7);
  wstring(vmad, "Empty");
  vmad.u8(16);
  vmad.u8(1);
  vmad.u32(0);
  vmad.u8(0xEF);
  vmad.u8(0x01);
  const auto scripts = fo4::read_vmad_scripts(vmad.bytes);
  EXPECT_FALSE(scripts.unsupported);
  EXPECT_EQ(scripts.trailing_bytes, 2u);
  ASSERT_EQ(scripts.scripts.size(), 1u);
  ASSERT_EQ(scripts.scripts[0].properties.size(), 2u);
  EXPECT_EQ(scripts.scripts[0].properties[0].integer, 7);
  EXPECT_EQ(scripts.scripts[0].properties[1].type, 16);
  EXPECT_EQ(scripts.scripts[0].properties[1].element_count, 0u);
  const auto actor = fo4::Signature::from_string("ACTI");
  EXPECT_TRUE(fo4::vmad_coverage(vmad.bytes, actor).trailing_data);
  EXPECT_FALSE(fo4::vmad_formid_offsets_for_remap(vmad.bytes, actor).has_value());
}
