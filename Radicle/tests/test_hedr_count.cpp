#include "fo4/binary/cursor.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> make_short_hedr_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> make_nested_plugin(std::uint32_t stored_count, bool stale_onam = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", stale_onam ? fo4::RecordFlags::master : 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(stored_count);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (stale_onam) {
    fo4::test::ByteWriter onam;
    onam.u32(0x1234);
    fo4::test::write_subrecord(out, "ONAM", onam.bytes);
  }
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "MISC", 0);
  const auto first = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"First\0", 6});
  fo4::test::end_record(out, first);

  const auto nested = fo4::test::begin_group(out, "MISC", 1);
  const auto second = fo4::test::begin_record(out, "MISC", 0, 0x00000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Second\0", 7});
  fo4::test::end_record(out, second);
  fo4::test::end_group(out, nested);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::uint64_t recursive_hedr_count(const fo4::Plugin& plugin) {
  const auto records = fo4::collect_records(plugin);
  const auto non_tes4 = std::count_if(records.begin(), records.end(), [](const fo4::Record* r) {
    return r->header.signature != fo4::sig_tes4;
  });
  return static_cast<std::uint64_t>(non_tes4) + fo4::collect_groups(plugin).size();
}

std::uint32_t rebuilt_hedr_count(fo4::Plugin& plugin) {
  fo4::repair_hedr_record_count(plugin);
  const auto written = fo4::write_plugin(plugin);
  fo4::binary::Cursor cursor{written};
  cursor.read_signature();
  const auto payload_size = cursor.read_u32();
  cursor.seek(0);
  return fo4::read_plugin(cursor.read_bytes(fo4::k_record_header_size + payload_size),
                          plugin.path).header.record_count;
}

std::string lower_extension(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

std::vector<std::filesystem::path> official_hedr_samples(const std::filesystem::path& dir) {
  static constexpr const char* kMasters[] = {
      "Fallout4.esm",      "DLCRobot.esm",      "DLCworkshop01.esm", "DLCworkshop02.esm",
      "DLCworkshop03.esm", "DLCCoast.esm",      "DLCNukaWorld.esm",
  };
  std::vector<std::filesystem::path> out;
  for (const char* name : kMasters) {
    const auto path = dir / name;
    if (std::filesystem::exists(path)) out.push_back(path);
  }
  std::vector<std::filesystem::path> light;
  for (const auto& entry : std::filesystem::directory_iterator{
           dir, std::filesystem::directory_options::skip_permission_denied}) {
    if (entry.is_regular_file() && lower_extension(entry.path()) == ".esl") {
      light.push_back(entry.path());
    }
  }
  std::sort(light.begin(), light.end());
  out.insert(out.end(), light.begin(), light.end());
  return out;
}

}

TEST(HedrCount, CleanPluginPreservesOriginalBytesEvenWhenStoredCountIsStale) {
  const auto bytes = make_nested_plugin(999);
  auto plugin = fo4::read_plugin(bytes, "StaleButClean.esp");
  ASSERT_EQ(plugin.header.record_count, 999u);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(HedrCount, MixedPathPreservesNoncanonicalCountWhenStructureIsUnchanged) {
  const auto bytes = make_nested_plugin(999);
  auto plugin = fo4::read_plugin(bytes, "StaleButMixed.esp");
  plugin.original_range.clear();

  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

// Detects: editing a record must not silently normalize an unrelated HEDR count.
TEST(HedrCount, ContentEditPreservesNoncanonicalCount) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "ContentEdit.esp");
  auto& record = std::get<fo4::Record>(plugin.groups[0].children[0]);
  record.subrecords[0].data[0] = std::byte{'X'};
  record.original_range.clear();
  plugin.original_range.clear();
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "ContentEdit.esp");
  EXPECT_EQ(reread.header.record_count, 999u);
  EXPECT_EQ(std::get<fo4::Record>(reread.groups[0].children[0]).subrecords[0].data[0], std::byte{'X'});
}

// Detects: explicit count repair must survive a save and leave other HEDR bytes intact.
TEST(HedrCount, ExplicitRepairUpdatesCountAndPreservesOtherFields) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Repair.esp");
  fo4::repair_hedr_record_count(plugin);
  EXPECT_EQ(plugin.header.record_count, 4u);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Repair.esp");
  EXPECT_EQ(reread.header.record_count, 4u);
  EXPECT_EQ(reread.header.next_form_id, 0x802u);
}

// Detects: explicit HEDR repair must not rewrite unrelated stale ONAM metadata.
TEST(HedrCount, ExplicitRepairPreservesNoncanonicalOnam) {
  const auto bytes = make_nested_plugin(999, true);
  auto plugin = fo4::read_plugin(bytes, "Repair.esm");
  fo4::repair_hedr_record_count(plugin);
  EXPECT_TRUE(fo4::record_original_bytes(plugin, plugin.tes4).empty());
  const auto copied_tes4 = plugin.tes4;
  EXPECT_TRUE(fo4::record_original_bytes(plugin, copied_tes4).empty());
  auto expected = bytes;
  expected[34] = std::byte{4};
  expected[35] = expected[36] = expected[37] = std::byte{0};
  EXPECT_EQ(fo4::write_plugin(plugin), expected);
}

// Detects: repairing an extended HEDR must preserve its nonzero stored short size and all other bytes.
TEST(HedrCount, ExplicitRepairPreservesExtendedSubrecordEncoding) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  out.sig("XXXX");
  out.u16(4);
  out.u32(12);
  out.sig("HEDR");
  out.u16(7);
  out.f32(1.0f);
  out.u32(999);
  out.u32(0x800);
  fo4::test::end_record(out, tes4);
  auto plugin = fo4::read_plugin(out.bytes, "Extended.esp");
  fo4::repair_hedr_record_count(plugin);
  auto expected = out.bytes;
  std::fill(expected.begin() + 44, expected.begin() + 48, std::byte{0});
  EXPECT_EQ(fo4::write_plugin(plugin), expected);
}

// Detects: repairing a compressed TES4 must retain unrelated decoded subrecords after recompression.
TEST(HedrCount, ExplicitRepairPreservesCompressedHeaderContent) {
  fo4::test::ByteWriter payload;
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(999);
  hedr.u32(0x800);
  fo4::test::write_subrecord(payload, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(payload, "CNAM", std::string_view{"Author\0", 7});
  const std::vector<std::byte> description{std::byte{0x41}, std::byte{0x42}, std::byte{0}};
  fo4::test::write_xxxx_subrecord(payload, "SNAM", description);

  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::compressed, 0);
  out.u32(static_cast<std::uint32_t>(payload.bytes.size()));
  out.raw(fo4::test::zlib_compress(payload.bytes));
  fo4::test::end_record(out, tes4);

  auto plugin = fo4::read_plugin(out.bytes, "Compressed.esp");
  fo4::repair_hedr_record_count(plugin);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Compressed.esp");
  EXPECT_TRUE(reread.tes4.compressed);
  EXPECT_EQ(reread.header.record_count, 0u);
  EXPECT_EQ(reread.header.next_form_id, 0x800u);
  EXPECT_EQ(reread.header.author, "Author");
  const auto snam = std::find_if(reread.tes4.subrecords.begin(), reread.tes4.subrecords.end(),
                                 [](const auto& sub) { return sub.signature == fo4::sig_snam; });
  ASSERT_NE(snam, reread.tes4.subrecords.end());
  EXPECT_TRUE(snam->extended_size);
  EXPECT_EQ(snam->data, description);
}

// Detects: editing TES4 after a repair must still trigger the normal ONAM rebuild.
TEST(HedrCount, HeaderEditAfterRepairRebuildsOnam) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999, true), "Repair.esm");
  fo4::repair_hedr_record_count(plugin);
  plugin = fo4::set_plugin_author(std::move(plugin), "New author");
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Repair.esm");
  EXPECT_EQ(reread.header.author, "New author");
  EXPECT_TRUE(reread.header.overrides.empty());
  EXPECT_EQ(std::count_if(reread.tes4.subrecords.begin(), reread.tes4.subrecords.end(),
                          [](const auto& sub) { return sub.signature == fo4::sig_onam; }), 0);
}

// Detects: adding and deleting a record across saves must not restore the original stale count.
TEST(HedrCount, StructuralEditsReturningToOriginalTotalsRebuildCount) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Structural.esp");
  const auto added = fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  EXPECT_EQ(fo4::read_plugin(fo4::write_plugin(plugin), "Structural.esp").header.record_count, 5u);
  ASSERT_EQ(fo4::delete_local_record(plugin, added), fo4::RecordDeleteStatus::deleted);
  EXPECT_EQ(fo4::read_plugin(fo4::write_plugin(plugin), "Structural.esp").header.record_count, 4u);
}

// Detects: replacing one local record with another must not retain a stale count.
TEST(HedrCount, EqualCountStructuralReplacementRebuildsCount) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Structural.esp");
  const auto original = fo4::FormKey::parse("000800:Structural.esp");
  ASSERT_EQ(fo4::delete_local_record(plugin, original), fo4::RecordDeleteStatus::deleted);
  fo4::create_local_record(plugin, fo4::Signature::from_string("MISC"));
  EXPECT_EQ(fo4::read_plugin(fo4::write_plugin(plugin), "Structural.esp").header.record_count, 4u);
}

// Detects: content-only saves must preserve a malformed header that repair must reject.
TEST(HedrCount, ShortHedrSurvivesContentOnlySave) {
  const auto bytes = make_short_hedr_plugin();
  auto plugin = fo4::read_plugin(bytes, "ShortHedr.esp");
  plugin.original_range.clear();
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
  EXPECT_THROW(fo4::repair_hedr_record_count(plugin), std::runtime_error);
}

// Detects: explicit repair must reject ambiguous duplicate HEDR subrecords without changing the plugin.
TEST(HedrCount, ExplicitRepairRejectsDuplicateHedr) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Duplicate.esp");
  plugin.tes4.subrecords.push_back(plugin.tes4.subrecords.front());
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
  EXPECT_THROW(fo4::repair_hedr_record_count(plugin), std::runtime_error);
  EXPECT_EQ(plugin.header.record_count, 999u);
}

TEST(HedrCount, DirtyRebuildCountsAllGroupsAndNonTes4RecordsRecursively) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Rebuilt.esp");

  plugin.original_range.clear();
  plugin.groups[0].children.pop_back();

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "Rebuilt.esp");
  EXPECT_EQ(reread.header.record_count, 2u);
  EXPECT_EQ(fo4::collect_records(reread).size(), 2u);
  EXPECT_EQ(fo4::collect_groups(reread).size(), 1u);
}

TEST(HedrCount, ShortHedrIsRefusedWhenRebuilt) {
  auto plugin = fo4::read_plugin(make_short_hedr_plugin(), "ShortHedr.esp");
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
  try {
    fo4::repair_hedr_record_count(plugin);
    ADD_FAILURE() << "repair_hedr_record_count accepted a TES4 HEDR shorter than 12 bytes";
  } catch (const std::runtime_error& error) {
    EXPECT_EQ(dynamic_cast<const fo4::FormIdError*>(&error), nullptr);
    EXPECT_STREQ(error.what(), "TES4 HEDR must be at least 12 bytes on rebuild");
  }
}

TEST(HedrCount, CommittedRealPluginsRebuildStaleCounts) {
  for (const char* name : {"FW_HeatherSurvivalPatch.esp", "SlowTime.esp"}) {
    SCOPED_TRACE(name);
    auto plugin =
        fo4::read_plugin_file(std::filesystem::path{FO4_TESTDATA_DIR} / name);
    const auto expected = recursive_hedr_count(plugin);
    EXPECT_EQ(plugin.header.record_count, expected);
    const auto hedr = std::find_if(plugin.tes4.subrecords.begin(), plugin.tes4.subrecords.end(),
                                   [](const auto& sub) { return sub.signature == fo4::sig_hedr; });
    ASSERT_NE(hedr, plugin.tes4.subrecords.end());
    ASSERT_EQ(hedr->data.size(), 12u);
    std::fill(hedr->data.begin() + 4, hedr->data.begin() + 8, std::byte{0xff});
    EXPECT_EQ(rebuilt_hedr_count(plugin), expected);
  }
}

TEST(HedrCount, OfficialMastersAndLightPluginsRebuildTheRecursiveCountWhenPresent) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));
  const auto paths = official_hedr_samples(dir);
  if (paths.empty()) GTEST_SKIP() << "no official masters or .esl files under " << dir;

  for (const auto& path : paths) {
    SCOPED_TRACE(path.filename().string());
    auto plugin = fo4::read_plugin_file(path);
    const auto expected = recursive_hedr_count(plugin);
    RecordProperty(path.stem().string() + "_stored_count", std::to_string(plugin.header.record_count));
    RecordProperty(path.stem().string() + "_recursive_count", std::to_string(expected));
    EXPECT_EQ(rebuilt_hedr_count(plugin), expected);
  }
}
