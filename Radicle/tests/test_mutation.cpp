#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
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

std::vector<std::byte> make_source_override(std::string_view editor_id = "SourceOverride") {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
  std::vector<std::byte> edid;
  for (const char ch : editor_id) edid.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  edid.push_back(std::byte{0});
  fo4::test::write_subrecord(out, "EDID", edid);
  fo4::test::ByteWriter name;
  name.u32(0x00000801);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_target_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x811);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TargetLocal\0", 12});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string editor_id(const fo4::Record& record) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != fo4::sig_edid) continue;
    const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
    const auto* end = begin + subrecord.data.size();
    const auto* nul = std::find(begin, end, '\0');
    return std::string(begin, nul);
  }
  return {};
}

}  // namespace

TEST(Mutation, ReplacesExistingEditorIdAndPreservesOtherSubrecords) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");

  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "NewEditorId"));
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reloaded, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(editor_id(*record), "NewEditorId");
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "FULL");
}

TEST(Mutation, InsertsMissingEditorId) {
  auto plugin = fo4::read_plugin(make_plugin(false), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");

  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "InsertedId"));
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reloaded, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(editor_id(*record), "InsertedId");
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
}

TEST(Mutation, NoOpStillReportsRecordFound) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");
  const auto original = plugin.original_bytes;
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "OldEditorId"));
  // A same-value write must not dirty the serialized payload.
  EXPECT_EQ(plugin.original_bytes, original);
}

TEST(Mutation, MissingRecordReturnsFalseAndDoesNotDirtyPlugin) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto original = plugin.original_bytes;
  EXPECT_FALSE(fo4::set_record_editor_id(
      plugin, fo4::FormKey::parse("000999:Patch.esp"), "Nope"));
  EXPECT_EQ(plugin.original_bytes, original);
}

TEST(Mutation, CopyOverrideAddsMasterAndReencodesDestinationSafely) {
  const auto source = fo4::read_plugin(make_source_override(), "Source.esp");
  auto target = fo4::read_plugin(make_target_plugin(), "Target.esp");
  const auto override_key = fo4::FormKey::parse("000800:Master.esm");
  const auto local_key = fo4::FormKey::parse("000810:Target.esp");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, override_key, false),
            fo4::RecordCopyStatus::inserted);
  ASSERT_EQ(target.header.masters.size(), 1u);
  EXPECT_EQ(target.header.masters[0].key.filename(), "Master.esm");

  const auto bytes = fo4::write_plugin(target);
  const auto reloaded = fo4::read_plugin(bytes, "Target.esp");
  ASSERT_EQ(reloaded.header.masters.size(), 1u);
  EXPECT_EQ(reloaded.header.masters[0].key.filename(), "Master.esm");

  const auto* copied = fo4::find_record(reloaded, override_key);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->header.form_id, fo4::RawFormId{0x00000800});
  EXPECT_EQ(editor_id(*copied), "SourceOverride");

  const auto* local = fo4::find_record(reloaded, local_key);
  ASSERT_NE(local, nullptr);
  EXPECT_EQ(local->header.form_id, fo4::RawFormId{0x01000810});

  const auto links = fo4::collect_form_links(reloaded);
  const auto copied_link = std::find_if(links.begin(), links.end(), [&](const auto& link) {
    return link.record == override_key && link.subrecord.to_string() == "NAME";
  });
  ASSERT_NE(copied_link, links.end());
  EXPECT_EQ(copied_link->key, fo4::FormKey::parse("000801:Master.esm"));
  EXPECT_EQ(copied_link->raw, fo4::RawFormId{0x00000801});
}

TEST(Mutation, CopyOverrideRequiresExplicitOverwriteForExistingTarget) {
  const auto source = fo4::read_plugin(make_source_override("FirstVersion"), "Source.esp");
  auto target = fo4::read_plugin(make_target_plugin(), "Target.esp");
  const auto key = fo4::FormKey::parse("000800:Master.esm");

  ASSERT_EQ(fo4::copy_record_as_override(target, source, key, false),
            fo4::RecordCopyStatus::inserted);
  const auto before = fo4::write_plugin(target);

  const auto second = fo4::read_plugin(make_source_override("SecondVersion"), "Source.esp");
  EXPECT_EQ(fo4::copy_record_as_override(target, second, key, false),
            fo4::RecordCopyStatus::already_exists);
  EXPECT_EQ(fo4::write_plugin(target), before);

  ASSERT_EQ(fo4::copy_record_as_override(target, second, key, true),
            fo4::RecordCopyStatus::replaced);
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(target), "Target.esp");
  const auto* replaced = fo4::find_record(reloaded, key);
  ASSERT_NE(replaced, nullptr);
  EXPECT_EQ(editor_id(*replaced), "SecondVersion");
}
