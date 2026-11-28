#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/text_export.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"
#include "real_sample_plugin.hpp"
#include "temp_data_dir.hpp"
#include "real_data.hpp"
#include "official_masters.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

class TextExportReal : public testing::TestWithParam<const char*> {};

constexpr std::array<const char*, 6> kDlcMasters{"DLCRobot.esm",       "DLCworkshop01.esm", "DLCCoast.esm",
                                                 "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"};
static_assert(kDlcMasters.size() + 1 == fo4::test::kOfficialMasters.size());

std::filesystem::path installed_data_dir() {
  const auto* data_dir = ::fo4::test::fo4_data_dir_cstr();
  if (data_dir == nullptr) return {};
  record_game_build(detect_game_build(data_dir));
  return data_dir;
}

}  // namespace

TEST_P(TextExportReal, OfficialMasterRoundTripsToCanonicalBytes) {
  const auto dir = installed_data_dir();
  if (dir.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const std::string name = GetParam();
  if (!std::filesystem::exists(dir / name)) GTEST_SKIP() << "missing official master: " << (dir / name);

  const auto plugin = fo4::read_plugin_file(dir / name);
  const auto expected = fo4::write_plugin(fo4::test::canonical_plugin(plugin));
  for (const bool schema_labels : {false, true}) {
    fo4::test::TempDataDir scratch;
    const auto rebuilt = fo4::write_plugin(fo4::import_plugin_text(
        fo4::export_plugin_text(plugin, scratch.path(), fo4::TextExportOptions{schema_labels})));
    EXPECT_TRUE(rebuilt == expected) << name << (schema_labels ? " with schema labels" : "");
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, TextExportReal, testing::ValuesIn(kDlcMasters));

TEST(TextExportSampleReal, FalloutMasterSampleKeepsCompressedRecordsByteIdentical) {
  const auto dir = installed_data_dir();
  if (dir.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  if (!std::filesystem::exists(dir / "Fallout4.esm")) GTEST_SKIP() << "missing official master: " << (dir / "Fallout4.esm");

  const auto master = fo4::read_plugin_file(dir / "Fallout4.esm");
  const auto sample = fo4::test::make_sample(
      master,
      [](const fo4::Record& record) {
        if (record.compressed) return true;
        return std::ranges::any_of(record.subrecords,
                                   [](const fo4::Subrecord& sub) { return sub.extended_size; });
      },
      50);
  const auto expected = fo4::write_plugin(fo4::test::canonical_plugin(sample));
  fo4::test::TempDataDir scratch;
  const auto rebuilt = fo4::write_plugin(
      fo4::import_plugin_text(fo4::export_plugin_text(sample, scratch.path())));
  EXPECT_TRUE(rebuilt == expected) << "Fallout4.esm sample";

  const auto out = fo4::read_plugin(rebuilt, master.path);
  const fo4::RecordIndex shipped{master};
  std::size_t compressed_total = 0;
  std::size_t identical = 0;
  for (const auto* record : fo4::collect_records(out)) {
    if (!record->compressed) continue;
    ++compressed_total;
    const auto* original = shipped.find(record->form_key);
    ASSERT_NE(original, nullptr) << record->form_key.to_string();
    if (fo4::test::record_original_bytes(out, *record) == fo4::test::record_original_bytes(master, *original)) {
      ++identical;
    }
  }
  ASSERT_GT(compressed_total, 0u);
  std::cout << "Fallout4.esm sample: " << identical << " of " << compressed_total
            << " compressed records byte-identical to the shipped bytes\n";
}
