#include "fo4/c_api_maintenance.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> plugin_bytes(std::initializer_list<std::string_view> masters,
                                    std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> keywords) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(keywords.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::vector<std::byte> name;
    for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    name.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "KYWD", 0);
  for (const auto& [form_id, flags] : keywords) {
    const auto record = fo4::test::begin_record(out, "KYWD", flags, form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Kw\0", 3});
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::string write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::vector<std::string> take(char** strings, size_t count) {
  std::vector<std::string> out(strings, strings + count);
  radicle_free_string_array(strings, count);
  return out;
}

}  // namespace

TEST(CApiMaintenance, ReportsItmDeletedAndUnusedMasters) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_maintenance_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_bytes({}, {{0x000800, 0}}));
  const auto unused = write_file(root / "Unused.esm", plugin_bytes({}, {{0x000800, 0}}));
  const auto patch = write_file(root / "Patch.esp",
                                plugin_bytes({"Base.esm", "Unused.esm"}, {{0x00000800, 0}, {0x02000900, 0x20}}));
  const char* paths[] = {base.c_str(), unused.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK) << radicle_last_error();

  char** strings = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_identical_to_master(env, "Patch.esp", 0, &strings, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(take(strings, count), std::vector<std::string>{"000800:Base.esm\tKYWD\tBase.esm"});

  size_t records = 0, references = 0, navmeshes = 0;
  ASSERT_EQ(radicle_env_deleted_counts(env, "Patch.esp", &records, &references, &navmeshes), RADICLE_STATUS_OK);
  EXPECT_EQ(records, 1u);
  EXPECT_EQ(references, 0u);

  int32_t complete = 0;
  ASSERT_EQ(radicle_env_unused_masters(env, "Patch.esp", &strings, &count, &complete), RADICLE_STATUS_OK);
  EXPECT_EQ(complete, 1);
  EXPECT_EQ(take(strings, count), std::vector<std::string>{"Unused.esm"});

  EXPECT_EQ(radicle_env_unused_masters(env, "Missing.esp", &strings, &count, &complete), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_deleted_counts(env, nullptr, &records, &references, &navmeshes),
            RADICLE_STATUS_INVALID_ARGUMENT);
  size_t native = 0, out_of_range = 0, capacity = 0;
  ASSERT_EQ(radicle_env_esl_eligibility(env, "Patch.esp", &native, &out_of_range, &capacity), RADICLE_STATUS_OK);
  EXPECT_EQ(native, 1u);
  EXPECT_EQ(out_of_range, 0u);
  EXPECT_EQ(capacity, 0xFFEu);

  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiMaintenance, EslEligibilityMatchesTheTargetFloorForMasterlessPlugins) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_esl_floor_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_bytes({}, {{0x000800, 0}}));
  const auto masterless = write_file(root / "Masterless.esp", plugin_bytes({}, {{0x000700, 0}, {0x000800, 0}}));
  const auto mastered = write_file(root / "Mastered.esp", plugin_bytes({"Base.esm"}, {{0x01000700, 0}, {0x01000000, 0}}));
  const char* paths[] = {base.c_str(), masterless.c_str(), mastered.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK) << radicle_last_error();

  size_t native = 0, out_of_range = 0, capacity = 0;
  ASSERT_EQ(radicle_env_esl_eligibility(env, "Masterless.esp", &native, &out_of_range, &capacity), RADICLE_STATUS_OK);
  EXPECT_EQ(native, 2u);
  EXPECT_EQ(out_of_range, 1u);
  EXPECT_EQ(capacity, 0x800u);

  ASSERT_EQ(radicle_env_esl_eligibility(env, "Mastered.esp", &native, &out_of_range, &capacity), RADICLE_STATUS_OK);
  EXPECT_EQ(native, 2u);
  EXPECT_EQ(out_of_range, 1u);
  EXPECT_EQ(capacity, 0xFFEu);

  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}
