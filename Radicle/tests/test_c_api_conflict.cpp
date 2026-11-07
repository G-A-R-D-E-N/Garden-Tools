#include "fo4/c_api_conflict.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view k_dest_health_100{"\x64\0\0\0\x01\0\0\0", 8};
constexpr std::string_view k_dest_health_200{"\xC8\0\0\0\x01\0\0\0", 8};

std::vector<std::byte> plugin_with_misc(std::string_view payload, bool with_master) {
  fo4::test::ByteWriter out;
  const std::vector<std::string> masters =
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{};
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, masters);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Thing\0", 6});
  fo4::test::write_subrecord(out, "DEST", payload);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

bool has_row(char** rows, size_t count, std::string_view expected) {
  for (size_t index = 0; index < count; ++index) {
    if (std::string_view{rows[index]} == expected) return true;
  }
  return false;
}

std::string row_with_prefix(char** rows, size_t count, std::string_view prefix) {
  for (size_t index = 0; index < count; ++index) {
    const std::string_view row{rows[index]};
    if (row.starts_with(prefix)) return std::string{row};
  }
  return {};
}

}  // namespace

TEST(CApiConflict, ClassifiesRecordSubrecordAndFieldRows) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_misc(k_dest_health_100, false));
  const auto patch = write_file(root / "Patch.esp", plugin_with_misc(k_dest_health_200, true));
  const char* paths[] = {base.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(rows, nullptr);

  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\toverride\tmaster,override"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tDEST\toverride\tmaster,override"));
  EXPECT_TRUE(
      has_row(rows, count, "000800:Base.esm\tsubrecord\tEDID\tno_conflict\tmaster,identical_to_master"));
  EXPECT_TRUE(has_row(
      rows, count, "000800:Base.esm\tfield\tMISC\\DestructionHealth\toverride\tmaster,override"));
  EXPECT_TRUE(has_row(
      rows, count, "000800:Base.esm\tfield\tMISC\\DestructionStageCount\tno_conflict\tmaster,identical_to_master"));
  EXPECT_EQ(row_with_prefix(rows, count, "000800:Base.esm\tfield\tMISC\\DestructionResistanceValue"), "");
  radicle_free_string_array(rows, count);

  EXPECT_EQ(radicle_env_classify_conflicts(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_classify_conflicts(env, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_classify_conflicts(env, &rows, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, ARecordOnePluginDefinesIsOnlyOne) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_one_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_misc(k_dest_health_100, false));
  const char* paths[] = {base.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 6u);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tonly_one\tonly_one"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tfield\tMISC\\DestructionHealth\tonly_one\tonly_one"));
  radicle_free_string_array(rows, count);

  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}
