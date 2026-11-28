#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/form_links.hpp"
#include "corruption_harness.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

TEST(Reader, ReadsTes4AndTopLevelGroups) {
  const auto bytes = fo4::test::make_minimal_plugin();
  auto plugin = fo4::read_plugin(bytes, "minimal.esp");

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

  auto plugin = fo4::read_plugin(out.bytes);
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

  auto plugin = fo4::read_plugin(out.bytes);
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
  auto plugin = fo4::read_plugin(bytes, "Dupes.esp");
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
  auto plugin = fo4::read_plugin(bytes, "Nested.esp");
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
  auto plugin = fo4::read_plugin(nested_groups_plugin(16));
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

  auto plugin = fo4::read_plugin(out.bytes);
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

  auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_xxxx);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "DATA");
  EXPECT_TRUE(record->subrecords[1].extended_size);
  EXPECT_EQ(record->subrecords[1].data, payload);
}

TEST(Reader, NonTes4SubrecordsStayUnmaterializedUntilRead) {
  const auto bytes = fo4::test::make_minimal_plugin();
  auto plugin = fo4::read_plugin(bytes, "lazy.esp");
  EXPECT_TRUE(plugin.tes4.subrecords.materialized());

  const auto* record = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(record->subrecords.materialized());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_TRUE(record->subrecords.materialized());
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
}

namespace {

std::vector<std::byte> plugin_with_compressed_gmst(std::span<const std::byte> body) {
  fo4::test::ByteWriter payload;
  payload.u32(static_cast<std::uint32_t>(body.size()));
  payload.raw(fo4::test::zlib_compress(body));
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "GMST", 0);
  const auto rec = fo4::test::begin_record(out, "GMST", fo4::RecordFlags::compressed, 0x01000800);
  out.raw(payload.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

}  // namespace

TEST(Reader, CompressedNonTes4SubrecordsDecodeOnAccess) {
  fo4::test::ByteWriter body;
  fo4::test::write_subrecord(body, "EDID", std::string_view{"sLazy\0", 6});
  const auto bytes = plugin_with_compressed_gmst(body.bytes);

  auto plugin = fo4::read_plugin(bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->compressed);
  EXPECT_FALSE(record->undecodable);
  EXPECT_FALSE(record->subrecords.materialized());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
  EXPECT_EQ(record->subrecords[0].data.size(), 6u);
}

TEST(ReaderLimits, CompressedNonTes4BodyWithTruncatedSubrecordLoadsUndecodable) {
  fo4::test::ByteWriter body;
  body.sig("EDID");
  body.u16(16);
  body.u16(0);
  const auto bytes = plugin_with_compressed_gmst(body.bytes);

  auto plugin = fo4::read_plugin(bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
  EXPECT_TRUE(record->subrecords.empty());
}

TEST(ReaderLimits, CorruptCompressedTes4StillThrowsAtLoad) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::compressed, 0);
  out.u32(4);
  out.raw(std::string_view{"junk"});
  fo4::test::end_record(out, tes4);
  EXPECT_THROW(fo4::read_plugin(out.bytes), fo4::parse_error);
}

TEST(ReaderLimits, TruncatedUncompressedNonTes4SubrecordFailsAtLoad) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "GMST", 0);
  const auto rec = fo4::test::begin_record(out, "GMST", 0, 0x01000800);
  out.sig("EDID");
  out.u16(16);
  out.u16(0);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  EXPECT_THROW(fo4::read_plugin(out.bytes), fo4::parse_error);
}

TEST(Reader, CopiedLazyRecordOutlivesItsPlugin) {
  EXPECT_TRUE(std::is_nothrow_move_constructible_v<fo4::Record>);
  fo4::Record copied;
  {
    const auto bytes = fo4::test::make_minimal_plugin();
    auto plugin = fo4::read_plugin(bytes, "Outlive.esp");
    const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
    ASSERT_NE(record, nullptr);
    copied = *record;
    ASSERT_FALSE(copied.subrecords.materialized());
  }
  fo4::Record moved = std::move(copied);
  ASSERT_EQ(moved.subrecords.size(), 1u);
  EXPECT_EQ(moved.subrecords[0].signature, fo4::sig_edid);
}

TEST(Reader, FirstNonemptyDataReadsWithoutCachingTheList) {
  fo4::test::ByteWriter body;
  fo4::test::write_subrecord(body, "EDID", std::string_view{"sLazy\0", 6});
  const auto bytes = plugin_with_compressed_gmst(body.bytes);
  auto plugin = fo4::read_plugin(bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  ASSERT_FALSE(record->subrecords.materialized());

  const auto edid = record->subrecords.first_nonempty_data(fo4::sig_edid);
  ASSERT_TRUE(edid.has_value());
  EXPECT_EQ(edid->size(), 6u);
  EXPECT_FALSE(record->subrecords.first_nonempty_data(fo4::Signature::from_string("FULL")).has_value());
  EXPECT_FALSE(record->subrecords.materialized());
}

TEST(Reader, ReleaseReturnsUnmodifiedCompressedListToItsSource) {
  fo4::test::ByteWriter body;
  fo4::test::write_subrecord(body, "EDID", std::string_view{"sLazy\0", 6});
  const auto bytes = plugin_with_compressed_gmst(body.bytes);
  auto plugin = fo4::read_plugin(bytes);
  auto* record = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 1u);
  ASSERT_TRUE(record->subrecords.materialized());

  EXPECT_TRUE(record->subrecords.release());
  EXPECT_FALSE(record->subrecords.materialized());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
  EXPECT_EQ(record->subrecords[0].data.size(), 6u);
}

TEST(Reader, EditedListIsNeverReleased) {
  const auto bytes = fo4::test::make_minimal_plugin();
  auto plugin = fo4::read_plugin(bytes, "edit.esp");
  auto* record = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  ASSERT_NE(record, nullptr);
  const auto before = record->subrecords[0].data.size();
  record->subrecords[0].data.push_back(std::byte{'x'});

  EXPECT_FALSE(record->subrecords.release());
  EXPECT_TRUE(record->subrecords.materialized());
  ASSERT_EQ(record->subrecords.size(), 1u);
  EXPECT_EQ(record->subrecords[0].data.size(), before + 1);
}

TEST(Reader, NonConstAccessAloneBlocksRelease) {
  const auto bytes = fo4::test::make_minimal_plugin();
  auto plugin = fo4::read_plugin(bytes, "mutable.esp");
  auto* record = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 1u);
  static_cast<void>(record->subrecords.begin());

  EXPECT_FALSE(record->subrecords.release());
  EXPECT_TRUE(record->subrecords.materialized());
}

TEST(Reader, CrossPluginCopyDoesNotPinSourceBuffer) {
  fo4::Record copied;
  std::weak_ptr<const std::vector<std::byte>> source_buffer;
  {
    const auto bytes = fo4::test::make_minimal_plugin();
    auto plugin = fo4::read_plugin(bytes, "Source.esp");
    const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
    ASSERT_NE(record, nullptr);
    copied = *record;
    copied.subrecords.own_record_bytes();
    ASSERT_FALSE(copied.subrecords.materialized());
    source_buffer = plugin.original_data;
  }
  EXPECT_TRUE(source_buffer.expired());
  ASSERT_EQ(copied.subrecords.size(), 1u);
  EXPECT_EQ(copied.subrecords[0].signature, fo4::sig_edid);
}

TEST(Reader, ConcurrentFirstAccessSharesOneDecodedList) {
  const auto bytes = fo4::test::make_minimal_plugin();
  auto plugin = fo4::read_plugin(bytes, "Race.esp");
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  ASSERT_FALSE(record->subrecords.materialized());

  std::vector<const fo4::Subrecord*> seen(8, nullptr);
  std::vector<std::thread> workers;
  for (std::size_t i = 0; i < seen.size(); ++i) {
    workers.emplace_back([record, &seen, i] { seen[i] = record->subrecords.data(); });
  }
  for (auto& worker : workers) worker.join();

  ASSERT_NE(seen[0], nullptr);
  for (const auto* pointer : seen) EXPECT_EQ(pointer, seen[0]);
  EXPECT_TRUE(record->subrecords.materialized());
  EXPECT_EQ(seen[0]->signature, fo4::sig_edid);
}

namespace {

std::vector<std::byte> plugin_with_misc_name_link() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esp\0", 9});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  const auto rec = fo4::test::begin_record(out, "MISC", 0, 0x01000801);
  fo4::test::ByteWriter link;
  link.u32(0x00000800);
  fo4::test::write_subrecord(out, "NAME", link.bytes);
  fo4::test::end_record(out, rec);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

}  // namespace

TEST(Reader, FormLinkScanLeavesNonTes4SubrecordsUnmaterialized) {
  const auto bytes = plugin_with_misc_name_link();
  auto plugin = fo4::read_plugin(bytes, "Links.esp");
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000801});
  ASSERT_NE(record, nullptr);
  ASSERT_FALSE(record->subrecords.materialized());

  const auto lazy_links = fo4::collect_form_links(plugin);
  EXPECT_FALSE(record->subrecords.materialized());

  ASSERT_EQ(record->subrecords.size(), 1u);
  const auto decoded_links = fo4::collect_form_links(plugin);
  ASSERT_EQ(lazy_links.size(), 1u);
  ASSERT_EQ(decoded_links.size(), lazy_links.size());
  EXPECT_EQ(lazy_links[0].key, fo4::FormKey::parse("000800:Base.esp"));
  EXPECT_EQ(lazy_links[0].key, decoded_links[0].key);
  EXPECT_EQ(lazy_links[0].record, decoded_links[0].record);
  EXPECT_EQ(lazy_links[0].subrecord, decoded_links[0].subrecord);
  EXPECT_EQ(lazy_links[0].offset, decoded_links[0].offset);
  EXPECT_EQ(lazy_links[0].raw.value(), decoded_links[0].raw.value());
}

namespace {

std::vector<std::byte> text_bytes(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  return out;
}

std::vector<std::byte> corruption_sample_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x803);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "CNAM", std::string_view{"fo4sdk\0", 7});
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "GMST", 0);
  const auto plain = fo4::test::begin_record(out, "GMST", 0, 0x01000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"sPlain\0", 7});
  fo4::test::write_xxxx_subrecord(out, "DATA", fo4::test::zlib_compress(text_bytes("0123456789abcdef")));
  fo4::test::end_record(out, plain);

  fo4::test::ByteWriter body;
  fo4::test::write_subrecord(body, "EDID", std::string_view{"sPacked\0", 8});
  fo4::test::write_subrecord(body, "DATA", std::string_view{"abcd"});
  fo4::test::ByteWriter packed;
  packed.u32(static_cast<std::uint32_t>(body.size()));
  packed.raw(fo4::test::zlib_compress(body.bytes));
  const auto compressed = fo4::test::begin_record(out, "GMST", fo4::RecordFlags::compressed, 0x01000801);
  out.raw(packed.bytes);
  fo4::test::end_record(out, compressed);

  const auto nested = fo4::test::begin_group(out, "????", 1);
  const auto inner = fo4::test::begin_record(out, "GMST", 0, 0x01000802);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"sInner\0", 7});
  fo4::test::end_record(out, inner);
  fo4::test::end_group(out, nested);
  fo4::test::end_group(out, top);
  return out.bytes;
}

void load_every_record(std::span<const std::byte> bytes) {
  auto plugin = fo4::read_plugin(bytes, "fuzz.esp");
  static_cast<void>(fo4::parse_tes4_header(plugin.tes4));
  std::size_t touched = 0;
  fo4::visit_records(plugin, [&touched](fo4::Record& record) {
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) touched += record.subrecords[i].data.size();
  });
  static_cast<void>(fo4::collect_groups(plugin));
  static_cast<void>(touched);
}

}  // namespace

// Constraint: a corrupted record or group size, flag, XXXX length or zlib body must end in fo4::parse_error, never a read past the buffer.
TEST(Reader, CorruptedPluginsStayInsideTheParseErrorContract) {
  const auto sample = corruption_sample_plugin();
  const auto stats = fo4::test::expect_closed_under_corruption<fo4::parse_error>(
      sample, 24, [](const std::vector<std::byte>& bytes) { load_every_record(bytes); });
  EXPECT_GT(stats.rejected, 0u);
}
