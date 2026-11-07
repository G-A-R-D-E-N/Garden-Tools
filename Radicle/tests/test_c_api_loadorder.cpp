#include "fo4/c_api_loadorder.h"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using fo4::test::TempDataDir;

std::vector<std::byte> bytes_of(const std::string& text) {
  std::vector<std::byte> bytes;
  for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
  return bytes;
}

std::vector<std::string> take_strings(char** strings, std::size_t count) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < count; ++i) out.emplace_back(strings[i]);
  radicle_free_string_array(strings, count);
  return out;
}

}  // namespace

TEST(CApiLoadOrder, NullArgumentsAreInvalid) {
  char** rows = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_plugin_list_read(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_list_read("plugins.txt", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_list_read("plugins.txt", &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_ccc_read(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_ccc_read("Fallout4.ccc", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_ccc_read("Fallout4.ccc", &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  char** findings = nullptr;
  std::size_t finding_count = 0;
  std::int32_t failed = 0;
  EXPECT_EQ(radicle_load_order_assemble(nullptr, nullptr, nullptr, RADICLE_OFFICIAL_TABLE_OG_NG, &rows, &count,
                                        &findings, &finding_count, &failed),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_load_order_assemble("Data", nullptr, nullptr, RADICLE_OFFICIAL_TABLE_OG_NG, &rows, &count,
                                        &findings, &finding_count, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiLoadOrder, UnknownTableIsInvalidAndOutputsAreZeroed) {
  TempDataDir data;
  char* sentinel_row = nullptr;
  char* sentinel_finding = nullptr;
  char** rows = &sentinel_row;
  std::size_t count = 99;
  char** findings = &sentinel_finding;
  std::size_t finding_count = 99;
  std::int32_t failed = 7;
  const auto path = data.path().string();
  EXPECT_EQ(radicle_load_order_assemble(path.c_str(), nullptr, nullptr, static_cast<radicle_official_table>(7),
                                        &rows, &count, &findings, &finding_count, &failed),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(findings, nullptr);
  EXPECT_EQ(finding_count, 0u);
  EXPECT_EQ(failed, 0);
}

TEST(CApiLoadOrder, NonDirectoryDataDirIsIoError) {
  TempDataDir data;
  data.write("plain.txt", bytes_of("not a directory"), 1);
  const auto file = (data.path() / "plain.txt").string();

  char* sentinel_row = nullptr;
  char* sentinel_finding = nullptr;
  char** rows = &sentinel_row;
  std::size_t count = 99;
  char** findings = &sentinel_finding;
  std::size_t finding_count = 99;
  std::int32_t failed = 7;
  EXPECT_EQ(radicle_load_order_assemble(file.c_str(), nullptr, nullptr, RADICLE_OFFICIAL_TABLE_OG_NG, &rows,
                                        &count, &findings, &finding_count, &failed),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(findings, nullptr);
  EXPECT_EQ(finding_count, 0u);
  EXPECT_EQ(failed, 0);
}

TEST(CApiLoadOrder, MissingPluginListIsIoErrorWithClearedOutputs) {
  TempDataDir data;
  const auto missing = (data.path() / "missing.txt").string();
  const auto path = data.path().string();

  char* sentinel_row = nullptr;
  char* sentinel_finding = nullptr;
  char** rows = &sentinel_row;
  std::size_t count = 99;
  EXPECT_EQ(radicle_plugin_list_read(missing.c_str(), &rows, &count), RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);

  rows = &sentinel_row;
  count = 99;
  char** findings = &sentinel_finding;
  std::size_t finding_count = 99;
  std::int32_t failed = 7;
  EXPECT_EQ(radicle_load_order_assemble(path.c_str(), missing.c_str(), nullptr, RADICLE_OFFICIAL_TABLE_OG_NG, &rows,
                                        &count, &findings, &finding_count, &failed),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(findings, nullptr);
  EXPECT_EQ(finding_count, 0u);
  EXPECT_EQ(failed, 0);
}

TEST(CApiLoadOrder, PluginListAndCccRowsAreFreedWithStringArrayFree) {
  TempDataDir data;
  data.write("plugins.txt", bytes_of("*Mod.esp\r\nGone.esp\r\n"), 1);
  data.write("Fallout4.ccc", bytes_of("\xEF\xBB\xBF" "cc_a.esl\r\n"), 2);

  char** rows = nullptr;
  std::size_t count = 0;
  const auto list = (data.path() / "plugins.txt").string();
  ASSERT_EQ(radicle_plugin_list_read(list.c_str(), &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(take_strings(rows, count), (std::vector<std::string>{"Mod.esp\t1\t1", "Gone.esp\t0\t2"}));

  const auto ccc = (data.path() / "Fallout4.ccc").string();
  ASSERT_EQ(radicle_ccc_read(ccc.c_str(), &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(take_strings(rows, count), (std::vector<std::string>{"\xC3\xAF\xC2\xBB\xC2\xBF" "cc_a.esl"}));
}

TEST(CApiLoadOrder, AssembleRowsAndFindingsAreFreedWithStringArrayFree) {
  TempDataDir data;
  data.write("Fallout4.esm", fo4::test::make_tes4_plugin(0x1, 1.0f, {}), 1);
  data.write("Mod.esp", fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"}), 2);
  data.write("plugins.txt", bytes_of("*Mod.esp\r\nGone.esp\r\n"), 3);
  const auto path = data.path().string();
  const auto list = (data.path() / "plugins.txt").string();

  char** rows = nullptr;
  std::size_t row_count = 0;
  char** findings = nullptr;
  std::size_t finding_count = 0;
  std::int32_t failed = 9;
  ASSERT_EQ(radicle_load_order_assemble(path.c_str(), list.c_str(), nullptr, RADICLE_OFFICIAL_TABLE_AE, &rows,
                                        &row_count, &findings, &finding_count, &failed),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(failed, 0);
  EXPECT_EQ(take_strings(rows, row_count), (std::vector<std::string>{"Fallout4.esm\t1\t1\t0", "Mod.esp\t1\t0\t0"}));
  const auto finding_rows = take_strings(findings, finding_count);
  ASSERT_EQ(finding_rows.size(), 1u);
  EXPECT_EQ(finding_rows[0].rfind("missing_file\tGone.esp\t", 0), 0u);
}

TEST(CApiLoadOrder, MissingCccIsIoErrorWithClearedOutputs) {
  TempDataDir data;
  const auto missing = (data.path() / "missing.ccc").string();
  const auto path = data.path().string();

  char* sentinel_row = nullptr;
  char** names = &sentinel_row;
  std::size_t count = 99;
  EXPECT_EQ(radicle_ccc_read(missing.c_str(), &names, &count), RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(names, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_GT(std::strlen(radicle_last_error()), 0u);

  char** rows = &sentinel_row;
  count = 99;
  char* sentinel_finding = nullptr;
  char** findings = &sentinel_finding;
  std::size_t finding_count = 99;
  std::int32_t failed = 7;
  EXPECT_EQ(radicle_load_order_assemble(path.c_str(), nullptr, missing.c_str(), RADICLE_OFFICIAL_TABLE_OG_NG, &rows,
                                        &count, &findings, &finding_count, &failed),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(findings, nullptr);
  EXPECT_EQ(finding_count, 0u);
  EXPECT_EQ(failed, 0);
}

TEST(CApiLoadOrder, AssembleHonoursCccPathAndOfficialTable) {
  TempDataDir data;
  data.write("Fallout4.esm", fo4::test::make_tes4_plugin(0x1, 1.0f, {}), 1);
  data.write("DLCDummy10thAnniversary.esm", fo4::test::make_tes4_plugin(0x1, 1.0f, {}), 2);
  data.write("cc_a.esl", fo4::test::make_tes4_plugin(0x201, 1.0f, {}), 3);
  data.write("Fallout4.ccc", bytes_of("cc_a.esl\r\n"), 4);
  const auto path = data.path().string();
  const auto ccc = (data.path() / "Fallout4.ccc").string();

  char** rows = nullptr;
  std::size_t row_count = 0;
  char** findings = nullptr;
  std::size_t finding_count = 0;
  std::int32_t failed = 9;
  ASSERT_EQ(radicle_load_order_assemble(path.c_str(), nullptr, ccc.c_str(), RADICLE_OFFICIAL_TABLE_OG_NG, &rows,
                                        &row_count, &findings, &finding_count, &failed),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(failed, 0);
  EXPECT_EQ(take_strings(rows, row_count),
            (std::vector<std::string>{"cc_a.esl\t1\t1\t1", "Fallout4.esm\t1\t1\t0",
                                      "DLCDummy10thAnniversary.esm\t0\t1\t0"}));
  EXPECT_TRUE(take_strings(findings, finding_count).empty());

  rows = nullptr;
  row_count = 0;
  findings = nullptr;
  finding_count = 0;
  failed = 9;
  ASSERT_EQ(radicle_load_order_assemble(path.c_str(), nullptr, ccc.c_str(), RADICLE_OFFICIAL_TABLE_AE, &rows,
                                        &row_count, &findings, &finding_count, &failed),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(failed, 0);
  EXPECT_EQ(take_strings(rows, row_count),
            (std::vector<std::string>{"DLCDummy10thAnniversary.esm\t1\t1\t0", "cc_a.esl\t1\t1\t1",
                                      "Fallout4.esm\t1\t1\t0"}));
  EXPECT_TRUE(take_strings(findings, finding_count).empty());
}

TEST(CApiLoadOrder, AssembleSurfacesTheFailedFlagWithStatusOk) {
  TempDataDir data;
  data.write("A.esm", fo4::test::make_tes4_plugin(0x1, 1.0f, {"B.esm"}), 1);
  data.write("B.esm", fo4::test::make_tes4_plugin(0x1, 1.0f, {"A.esm"}), 2);
  const auto path = data.path().string();

  char** rows = nullptr;
  std::size_t row_count = 0;
  char** findings = nullptr;
  std::size_t finding_count = 0;
  std::int32_t failed = 0;
  ASSERT_EQ(radicle_load_order_assemble(path.c_str(), nullptr, nullptr, RADICLE_OFFICIAL_TABLE_OG_NG, &rows,
                                        &row_count, &findings, &finding_count, &failed),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(failed, 1);
  EXPECT_EQ(take_strings(rows, row_count).size(), 2u);
  const auto finding_rows = take_strings(findings, finding_count);
  ASSERT_EQ(finding_rows.size(), 1u);
  EXPECT_EQ(finding_rows[0].rfind("hoist_cycle\t", 0), 0u);
}

namespace {

void write_bytes(const std::filesystem::path& file, const std::string& text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out{file, std::ios::binary};
  out << text;
}

}  // namespace

TEST(CApiLoadOrder, Mo2ProfileRowsRootsAndPluginPathsFollowTheInstance) {
  TempDataDir root;
  write_bytes(root.path() / "ModOrganizer.ini", "selected_profile=Test\nbase_directory=%BASE_DIR%\n");
  write_bytes(root.path() / "profiles" / "Test" / "modlist.txt", "+Mod\n-Off\n");
  write_bytes(root.path() / "profiles" / "Test" / "plugins.txt", "*Ex.esp\n");
  write_bytes(root.path() / "mods" / "Mod" / "Ex.esp", "");
  const auto instance = root.path().string();

  char** mod_rows = nullptr;
  std::size_t mod_count = 0;
  char** plugin_rows = nullptr;
  std::size_t plugin_count = 0;
  ASSERT_EQ(radicle_mo2_profile_read(instance.c_str(), nullptr, &mod_rows, &mod_count, &plugin_rows, &plugin_count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(take_strings(mod_rows, mod_count), (std::vector<std::string>{"Mod\t1\t0", "Off\t0\t0"}));
  EXPECT_EQ(take_strings(plugin_rows, plugin_count), (std::vector<std::string>{"Ex.esp\t1\t1"}));

  char** roots = nullptr;
  std::size_t root_count = 0;
  ASSERT_EQ(radicle_mo2_asset_roots(instance.c_str(), "Test", &roots, &root_count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_strings(roots, root_count),
            (std::vector<std::string>{(root.path() / "mods" / "Mod").string(), (root.path() / "overwrite").string()}));

  char** paths = nullptr;
  std::size_t path_count = 0;
  ASSERT_EQ(radicle_mo2_plugin_paths(instance.c_str(), "Test", &paths, &path_count), RADICLE_STATUS_OK);
  EXPECT_EQ(take_strings(paths, path_count),
            (std::vector<std::string>{"ex.esp\t" + (root.path() / "mods" / "Mod" / "Ex.esp").string()}));

  char* sentinel = nullptr;
  char** empty_rows = &sentinel;
  std::size_t empty_count = 9;
  char** empty_plugins = &sentinel;
  std::size_t empty_plugin_count = 9;
  EXPECT_EQ(radicle_mo2_profile_read(instance.c_str(), "", &empty_rows, &empty_count, &empty_plugins, &empty_plugin_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(empty_rows, nullptr);
  EXPECT_EQ(empty_count, 0u);
  EXPECT_EQ(empty_plugins, nullptr);
  EXPECT_EQ(empty_plugin_count, 0u);

  empty_rows = &sentinel;
  empty_count = 9;
  EXPECT_EQ(radicle_mo2_asset_roots(instance.c_str(), "", &empty_rows, &empty_count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(empty_rows, nullptr);
  EXPECT_EQ(empty_count, 0u);

  empty_rows = &sentinel;
  empty_count = 9;
  EXPECT_EQ(radicle_mo2_plugin_paths(instance.c_str(), "", &empty_rows, &empty_count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(empty_rows, nullptr);
  EXPECT_EQ(empty_count, 0u);
}

TEST(CApiLoadOrder, Mo2MissingPluginsTxtIsIoErrorWithClearedOutputs) {
  TempDataDir root;
  write_bytes(root.path() / "ModOrganizer.ini", "selected_profile=Test\n");
  const auto instance = root.path().string();

  char* sentinel = nullptr;
  char** mod_rows = &sentinel;
  std::size_t mod_count = 9;
  char** plugin_rows = &sentinel;
  std::size_t plugin_count = 9;
  EXPECT_EQ(radicle_mo2_profile_read(instance.c_str(), nullptr, &mod_rows, &mod_count, &plugin_rows, &plugin_count),
            RADICLE_STATUS_IO_ERROR);
  EXPECT_EQ(mod_rows, nullptr);
  EXPECT_EQ(mod_count, 0u);
  EXPECT_EQ(plugin_rows, nullptr);
  EXPECT_EQ(plugin_count, 0u);
}
