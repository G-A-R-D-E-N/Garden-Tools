#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

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
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "OldEditorId"));
  EXPECT_FALSE(plugin.original_bytes.empty());
}

TEST(Mutation, MissingRecordReturnsFalseAndDoesNotDirtyPlugin) {
  auto plugin = fo4::read_plugin(make_plugin(true), "Patch.esp");
  const auto original = plugin.original_bytes;
  EXPECT_FALSE(fo4::set_record_editor_id(
      plugin, fo4::FormKey::parse("000999:Patch.esp"), "Nope"));
  EXPECT_EQ(plugin.original_bytes, original);
}
