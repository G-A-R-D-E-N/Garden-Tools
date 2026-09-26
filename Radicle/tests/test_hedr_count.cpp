#include "fo4/binary/cursor.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_nested_plugin(std::uint32_t stored_count) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(stored_count);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
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

// Independent of the writer: every non-TES4 major record plus every GRUP.
std::uint64_t recursive_hedr_count(const fo4::Plugin& plugin) {
  const auto records = fo4::collect_records(plugin);
  const auto non_tes4 = std::count_if(records.begin(), records.end(), [](const fo4::Record* r) {
    return r->header.signature != fo4::sig_tes4;
  });
  return static_cast<std::uint64_t>(non_tes4) + fo4::collect_groups(plugin).size();
}

std::uint32_t rebuilt_hedr_count(fo4::Plugin& plugin) {
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();
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

// Official masters (DLCCoast/DLCNukaWorld are the WRLD-heavy ones) plus every
// light plugin in Data, which on a stock install is the Creation Club ESLs.
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
  const auto plugin = fo4::read_plugin(bytes, "StaleButClean.esp");
  ASSERT_EQ(plugin.header.record_count, 999u);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(HedrCount, MixedPathPreservesNoncanonicalCountWhenStructureIsUnchanged) {
  const auto bytes = make_nested_plugin(999);
  auto plugin = fo4::read_plugin(bytes, "StaleButMixed.esp");
  plugin.original_range.clear();

  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(HedrCount, DirtyRebuildCountsAllGroupsAndNonTes4RecordsRecursively) {
  auto plugin = fo4::read_plugin(make_nested_plugin(999), "Rebuilt.esp");

  // xEdit's writer counts every major record as 1 and each GRUP as
  // 1 + its recursive children, then subtracts one for TES4 itself.
  // This fixture therefore has: top GRUP + first record + nested GRUP +
  // second record = 4.
  plugin.original_range.clear();
  plugin.tes4.original_range.clear();

  const auto written = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(written, "Rebuilt.esp");
  EXPECT_EQ(reread.header.record_count, 4u);
  EXPECT_EQ(fo4::collect_records(reread).size(), 3u);  // TES4 + two MISC
  EXPECT_EQ(fo4::collect_groups(reread).size(), 2u);
}

TEST(HedrCount, CommittedRealPluginsStoreTheRecursiveCount) {
  for (const char* name : {"FW_HeatherSurvivalPatch.esp", "SlowTime.esp"}) {
    SCOPED_TRACE(name);
    auto plugin =
        fo4::read_plugin_file(std::filesystem::path{FO4_TESTDATA_DIR} / name);
    EXPECT_EQ(plugin.header.record_count, recursive_hedr_count(plugin));
    EXPECT_EQ(rebuilt_hedr_count(plugin), plugin.header.record_count);
  }
}

TEST(HedrCount, OfficialMastersAndLightPluginsRebuildTheRecursiveCountWhenPresent) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
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
