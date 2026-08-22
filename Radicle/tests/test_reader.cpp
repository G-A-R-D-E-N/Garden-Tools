#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>
#include <variant>

TEST(Reader, ReadsTes4AndTopLevelGroups) {
  const auto bytes = fo4::test::make_minimal_plugin();
  const auto plugin = fo4::read_plugin(bytes, "minimal.esp");

  EXPECT_EQ(plugin.tes4.header.signature, fo4::sig_tes4);
  EXPECT_EQ(plugin.tes4.header.form_version, 131);
  EXPECT_FLOAT_EQ(plugin.header.version, 1.0f);
  EXPECT_EQ(plugin.header.record_count, 1u);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_EQ(plugin.header.author, "fo4sdk");
  ASSERT_EQ(plugin.header.masters.size(), 1u);
  EXPECT_EQ(plugin.header.masters[0].key, fo4::PluginKey::from_filename("Fallout4.esm"));
  ASSERT_EQ(plugin.groups.size(), 1u);
  EXPECT_EQ(plugin.groups[0].header.top_label().to_string(), "GMST");
  ASSERT_EQ(plugin.groups[0].children.size(), 1u);

  const auto* record = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature.to_string(), "GMST");
  EXPECT_EQ(record->header.form_id, fo4::RawFormId{0x01000800});
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
}

TEST(Reader, PreservesUnknownSubrecords) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "ZZZZ", std::string_view{"keepme"});
  fo4::test::end_record(out, tes4);

  const auto plugin = fo4::read_plugin(out.bytes);
  ASSERT_EQ(plugin.tes4.subrecords.size(), 2u);
  EXPECT_EQ(plugin.tes4.subrecords[1].signature.to_string(), "ZZZZ");
  EXPECT_EQ(plugin.tes4.subrecords[1].data.size(), 6u);
}

TEST(Reader, NestedGroupsAreEnumerated) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
  const auto world = fo4::test::begin_record(out, "WRLD", 0, 0x0000003C);
  fo4::test::end_record(out, world);
  const auto children = fo4::test::begin_group(out, "????", 1);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x0000003D);
  fo4::test::end_record(out, cell);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, wrld);

  const auto plugin = fo4::read_plugin(out.bytes);
  const auto groups = fo4::collect_groups(plugin);
  EXPECT_EQ(groups.size(), 2u);
  const auto records = fo4::collect_records(plugin);
  EXPECT_EQ(records.size(), 3u);
}

TEST(Reader, TruncatedInputFailsSafely) {
  std::vector<std::byte> bytes{std::byte{'T'}, std::byte{'E'}, std::byte{'S'}, std::byte{'4'}};
  EXPECT_THROW(fo4::read_plugin(bytes), fo4::parse_error);
}
