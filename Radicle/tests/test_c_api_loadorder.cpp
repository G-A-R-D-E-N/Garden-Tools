#include "fo4/c_api_loadorder.h"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
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
  EXPECT_EQ(take_strings(rows, count), (std::vector<std::string>{"cc_a.esl"}));
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
