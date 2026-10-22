#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>
#include <cstddef>
#include <string_view>
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

namespace {

std::vector<std::byte> duplicate_form_id_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
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

  const auto gmst = fo4::test::begin_group(out, "GMST", 0);
  const auto master_owned = fo4::test::begin_record(out, "GMST", 0, 0x00000800);
  fo4::test::end_record(out, master_owned);
  const auto plugin_owned = fo4::test::begin_record(out, "GMST", 0, 0x01000800);
  fo4::test::end_record(out, plugin_owned);
  fo4::test::end_group(out, gmst);

  const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
  const auto children = fo4::test::begin_group(out, "????", 1);
  const auto duplicate = fo4::test::begin_record(out, "CELL", 0, 0x01000800);
  fo4::test::end_record(out, duplicate);
  const auto tes4_signature = fo4::test::begin_record(out, "TES4", 0, 0x01000801);
  fo4::test::end_record(out, tes4_signature);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, wrld);
  return out.bytes;
}

}  // namespace

TEST(Reader, FindRecordReturnsTheFirstMatchInTraversalOrder) {
  const auto bytes = duplicate_form_id_plugin();
  const auto plugin = fo4::read_plugin(bytes, "Dupes.esp");
  const auto* master_owned = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  const auto* plugin_owned = std::get_if<fo4::Record>(&plugin.groups[0].children[1]);
  ASSERT_NE(master_owned, nullptr);
  ASSERT_NE(plugin_owned, nullptr);
  const auto fallout = fo4::PluginKey::from_filename("Fallout4.esm");
  const auto self = fo4::PluginKey::from_filename("Dupes.esp");

  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x00000800}), master_owned);
  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x01000800}), plugin_owned);
  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x01000801}), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x02000800}), nullptr);
  EXPECT_EQ(fo4::find_record(plugin, fo4::FormKey{fallout, fo4::LocalFormId{0x800}}), master_owned);
  EXPECT_EQ(fo4::find_record(plugin, fo4::FormKey{self, fo4::LocalFormId{0x800}}), plugin_owned);
  EXPECT_EQ(fo4::find_record(plugin, fo4::FormKey{fo4::PluginKey::from_filename("DUPES.ESP"),
                                                  fo4::LocalFormId{0x800}}),
            plugin_owned);
  EXPECT_EQ(fo4::find_record(plugin, fo4::FormKey{fo4::PluginKey::from_filename("Other.esp"),
                                                  fo4::LocalFormId{0x800}}),
            nullptr);
  EXPECT_EQ(fo4::find_record(plugin, fo4::FormKey{self, fo4::LocalFormId{0x801}}), nullptr);

  const fo4::RecordIndex index{plugin};
  EXPECT_EQ(&index.plugin(), &plugin);
  EXPECT_EQ(index.find(fo4::FormKey{fallout, fo4::LocalFormId{0x800}}), master_owned);
  EXPECT_EQ(index.find(fo4::FormKey{self, fo4::LocalFormId{0x800}}), plugin_owned);
  EXPECT_EQ(index.find(fo4::FormKey{fo4::PluginKey::from_filename("DUPES.ESP"),
                                    fo4::LocalFormId{0x800}}),
            plugin_owned);
  EXPECT_EQ(index.find(fo4::FormKey{self, fo4::LocalFormId{0x801}}), nullptr);
  for (const auto* record : fo4::collect_records(plugin)) {
    EXPECT_EQ(index.find(record->form_key), fo4::find_record(plugin, record->form_key));
  }
}

namespace {

std::vector<std::byte> nested_duplicate_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto wrld = fo4::test::begin_group(out, "WRLD", 0);
  const auto a = fo4::test::begin_record(out, "WRLD", 0, 0x00000900);
  fo4::test::end_record(out, a);
  const auto children = fo4::test::begin_group(out, "????", 1);
  const auto b = fo4::test::begin_record(out, "CELL", 0, 0x00000900);
  fo4::test::end_record(out, b);
  const auto d = fo4::test::begin_record(out, "CELL", 0, 0x00000901);
  fo4::test::end_record(out, d);
  fo4::test::end_group(out, children);
  const auto c = fo4::test::begin_record(out, "WRLD", 0, 0x00000901);
  fo4::test::end_record(out, c);
  fo4::test::end_group(out, wrld);
  return out.bytes;
}

}  // namespace

TEST(Reader, FindRecordPrefersEarlierRecordsAcrossNestedGroups) {
  const auto bytes = nested_duplicate_plugin();
  const auto plugin = fo4::read_plugin(bytes, "Nested.esp");
  const auto& top = plugin.groups.at(0).children;
  ASSERT_EQ(top.size(), 3u);
  const auto* a = std::get_if<fo4::Record>(&top[0]);
  const auto* nested = std::get_if<fo4::Group>(&top[1]);
  const auto* c = std::get_if<fo4::Record>(&top[2]);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(nested, nullptr);
  ASSERT_NE(c, nullptr);
  ASSERT_EQ(nested->children.size(), 2u);
  const auto* d = std::get_if<fo4::Record>(&nested->children[1]);
  ASSERT_NE(d, nullptr);

  const auto self = fo4::PluginKey::from_filename("Nested.esp");
  const fo4::FormKey key_900{self, fo4::LocalFormId{0x900}};
  const fo4::FormKey key_901{self, fo4::LocalFormId{0x901}};
  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x00000900}), a);
  EXPECT_EQ(fo4::find_record(plugin, fo4::RawFormId{0x00000901}), d);
  EXPECT_EQ(fo4::find_record(plugin, key_900), a);
  EXPECT_EQ(fo4::find_record(plugin, key_901), d);

  const fo4::RecordIndex index{plugin};
  EXPECT_EQ(index.find(key_900), a);
  EXPECT_EQ(index.find(key_901), d);
}

namespace {

std::vector<std::byte> nested_groups_plugin(std::size_t levels) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  std::vector<std::size_t> offsets;
  for (std::size_t i = 0; i < levels; ++i) {
    offsets.push_back(fo4::test::begin_group(out, "????", 1));
  }
  for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
    fo4::test::end_group(out, *it);
  }
  return out.bytes;
}

}  // namespace

TEST(ReaderLimits, SixteenLevelsOfGroupNestingStillParse) {
  const auto plugin = fo4::read_plugin(nested_groups_plugin(16));
  EXPECT_EQ(fo4::collect_groups(plugin).size(), 16u);
}

TEST(ReaderLimits, SeventeenLevelsOfGroupNestingAreRefused) {
  EXPECT_THROW(fo4::read_plugin(nested_groups_plugin(17)), fo4::parse_error);
}

TEST(ReaderLimits, CompressedUncompressedSizeBeyondZlibBoundLoadsUndecodable) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, 0x01000801);
  out.u32(0xFFFFFFFFu);
  out.raw(std::string_view{"tiny"});
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
}

TEST(Xxxx, ReadsExtendedSizeSubrecordWithoutDiscardingPayload) {
  std::vector<std::byte> payload(70000, std::byte{0xAB});
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "STAT", 0);
  const auto rec = fo4::test::begin_record(out, "STAT", 0, 0x01000800);
  fo4::test::write_xxxx_subrecord(out, "DATA", payload);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);

  const auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_xxxx);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "DATA");
  EXPECT_TRUE(record->subrecords[1].extended_size);
  EXPECT_EQ(record->subrecords[1].data, payload);
}
