#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::filesystem::path temp_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("radicle_c_api_assets_" + std::to_string(id));
}

struct Cleanup {
  std::filesystem::path root;
  ~Cleanup() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(stream.good());
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  ASSERT_TRUE(stream.good());
}

void write_text(const std::filesystem::path& path, std::string_view text) {
  std::vector<std::byte> bytes;
  for (const char ch : text) bytes.push_back(static_cast<std::byte>(ch));
  write_bytes(path, bytes);
}

std::vector<std::byte> make_stat_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "STAT", 0);
  const auto record = fo4::test::begin_record(out, "STAT", 0, 0x000800);
  fo4::test::write_subrecord(out, "MODL", std::string_view{"Weapons\\gone.nif\0", 17});
  fo4::test::write_subrecord(out, "ICON", std::string_view{"Interface\\here.dds\0", 19});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::string> collect_rows(char** strings, size_t count) {
  std::vector<std::string> out;
  for (size_t i = 0; i < count; ++i) {
    if (strings[i] != nullptr) out.emplace_back(strings[i]);
  }
  return out;
}

TEST(CApiAssets, MissingAssetsListsAbsentPaths) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  write_bytes(root / "Scan.esp", make_stat_plugin());
  write_text(root / "textures" / "interface" / "here.dds", "icon");

  const auto plugin_path = (root / "Scan.esp").string();
  const char* paths[] = {plugin_path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();

  const auto root_path = root.string();
  const char* roots[] = {root_path.c_str()};
  radicle_assets* assets = nullptr;
  ASSERT_EQ(radicle_assets_open_roots(roots, 1, nullptr, 0, &assets), RADICLE_STATUS_OK)
      << radicle_last_error();

  char** strings = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_missing_assets(env, assets, nullptr, &strings, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  const auto rows = collect_rows(strings, count);
  radicle_free_string_array(strings, count);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], "Scan.esp\t000800:Scan.esp\tmeshes/weapons/gone.nif");

  write_text(root / "meshes" / "weapons" / "gone.nif", "model");
  strings = nullptr;
  count = 0;
  ASSERT_EQ(radicle_env_missing_assets(env, assets, nullptr, &strings, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_TRUE(collect_rows(strings, count).empty());
  radicle_free_string_array(strings, count);

  strings = nullptr;
  count = 0;
  ASSERT_EQ(radicle_env_missing_assets(env, assets, "Scan.esp", &strings, &count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_TRUE(collect_rows(strings, count).empty());
  radicle_free_string_array(strings, count);

  radicle_assets_destroy(assets);
  radicle_env_destroy(env);
}

TEST(CApiAssets, UnknownPluginIsNotFound) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  write_bytes(root / "Scan.esp", make_stat_plugin());

  const auto plugin_path = (root / "Scan.esp").string();
  const char* paths[] = {plugin_path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();

  const auto root_path = root.string();
  const char* roots[] = {root_path.c_str()};
  radicle_assets* assets = nullptr;
  ASSERT_EQ(radicle_assets_open_roots(roots, 1, nullptr, 0, &assets), RADICLE_STATUS_OK)
      << radicle_last_error();

  char** strings = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_env_missing_assets(env, assets, "Nope.esp", &strings, &count),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(strings, nullptr);
  EXPECT_EQ(count, 0u);

  radicle_assets_destroy(assets);
  radicle_env_destroy(env);
}

TEST(CApiAssets, NullArgumentsAreRefused) {
  EXPECT_EQ(radicle_env_missing_assets(nullptr, nullptr, nullptr, nullptr, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

}  // namespace
