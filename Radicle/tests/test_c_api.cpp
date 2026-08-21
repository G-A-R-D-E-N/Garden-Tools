#include "fo4/c_api.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path testdata_path(const char* name) {
  return std::filesystem::path(FO4_TESTDATA_DIR) / name;
}

std::filesystem::path temp_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("radicle_c_api_" + std::to_string(id));
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

std::vector<std::byte> make_remap_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);

  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Drop.esm\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Keep.esp\0", 9});
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "KYWD", 0);
  const auto local = fo4::test::begin_record(out, "KYWD", 0, 0x03000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Local\0", 6});
  fo4::test::end_record(out, local);
  const auto inherited = fo4::test::begin_record(out, "KYWD", 0, 0x0200009A);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"KeepRecord\0", 11});
  fo4::test::end_record(out, inherited);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::set<std::string> collect_form_keys(radicle_plugin* plugin) {
  char** values = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_plugin_collect_form_keys(plugin, &values, &count), RADICLE_STATUS_OK)
      << radicle_last_error();

  std::set<std::string> out;
  for (size_t i = 0; i < count; ++i) {
    if (values[i] != nullptr) out.emplace(values[i]);
  }
  radicle_free_string_array(values, count);
  return out;
}

}  // namespace

TEST(CApi, RejectsInvalidArgumentsWithoutThrowing) {
  radicle_plugin* plugin = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(nullptr, &plugin), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("required"), std::string::npos);
}

TEST(CApi, NegativeSurfaceRejectsNullAndInvalidInput) {
  radicle_plugin* plugin = nullptr;

  // Null path string.
  EXPECT_EQ(radicle_plugin_read_file("/definitely/not/a/plugin.esp", nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Null out pointers on every entry point that produces output.
  uint8_t* bytes = reinterpret_cast<uint8_t*>(0x1);
  size_t byte_count = 1;
  EXPECT_EQ(radicle_plugin_write_bytes(nullptr, &bytes, &byte_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(bytes, reinterpret_cast<uint8_t*>(0x1));
  EXPECT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &byte_count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  char** strings = nullptr;
  size_t string_count = 0;
  EXPECT_EQ(radicle_plugin_collect_form_keys(nullptr, &strings, &string_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(strings, nullptr);
  EXPECT_EQ(string_count, 0u);
  EXPECT_EQ(radicle_plugin_collect_form_links(nullptr, &strings, &string_count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Null master arrays / descriptors.
  EXPECT_EQ(radicle_plugin_stamp_master_kinds(nullptr, nullptr, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const char* const master_paths[] = {nullptr};
  EXPECT_EQ(radicle_plugin_stamp_master_kinds(plugin, master_paths, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_master_descriptor bad_descriptor[] = {{nullptr, RADICLE_PLUGIN_FULL}};
  EXPECT_EQ(radicle_plugin_remap_masters(plugin, bad_descriptor, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  // Out-of-range enum value must not crash; it is rejected as invalid.
  const radicle_master_descriptor bad_kind[] = {
      {"Fallout4.esm", static_cast<radicle_plugin_kind>(99)}};
  EXPECT_EQ(radicle_plugin_remap_masters(plugin, bad_kind, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);

  // Null write target and double destroy are both safe no-ops/failures.
  EXPECT_EQ(radicle_plugin_write_file(plugin, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin_destroy(nullptr);
  radicle_free_bytes(nullptr);
  radicle_free_string_array(nullptr, 3u);
}

TEST(CApi, WriteFileLeavesOriginalIntactWhenWriteFails) {
  namespace fs = std::filesystem;
  const auto root = temp_root();
  Cleanup cleanup{root};

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(
                testdata_path("FW_HeatherSurvivalPatch.esp").string().c_str(), &plugin),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  // A regular file sitting where a parent directory must be created forces
  // failure long before any destination byte is touched.
  const auto blocker = root / "blocker";
  write_bytes(blocker, {std::byte{0x00}});
  const auto impossible = blocker / "Nested" / "Patch.esp";
  EXPECT_NE(radicle_plugin_write_file(plugin, impossible.string().c_str()),
            RADICLE_STATUS_OK);
  EXPECT_TRUE(std::filesystem::is_regular_file(blocker));

  // A sibling temporary file from a failed staged write must not linger next
  // to a real output file after a successful save.
  const auto target = root / "out" / "Patch.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, target.string().c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();

  bool stray_temp_files = false;
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path().filename().string().find(".radicle-tmp") != std::string::npos) {
      stray_temp_files = true;
    }
  }
  EXPECT_FALSE(stray_temp_files);

  radicle_plugin_destroy(plugin);
}

TEST(CApi, ReadsSerializesAndCollectsFormKeys) {
  const auto path = testdata_path("FW_HeatherSurvivalPatch.esp").string();

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  uint8_t* bytes = nullptr;
  size_t byte_count = 0;
  ASSERT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &byte_count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_NE(bytes, nullptr);
  EXPECT_GT(byte_count, 0u);
  radicle_free_bytes(bytes);

  char** form_keys = nullptr;
  size_t form_key_count = 0;
  ASSERT_EQ(radicle_plugin_collect_form_keys(plugin, &form_keys, &form_key_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_GT(form_key_count, 0u);
  radicle_free_string_array(form_keys, form_key_count);

  char** form_links = nullptr;
  size_t form_link_count = 0;
  ASSERT_EQ(radicle_plugin_collect_form_links(plugin, &form_links, &form_link_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_free_string_array(form_links, form_link_count);

  radicle_plugin_destroy(plugin);
}

TEST(CApi, RemapsMastersWritesAndReloadsStableFormKeys) {
  const auto root = temp_root();
  Cleanup cleanup{root};
  const auto input = root / "input" / "Patch.esp";
  const auto output = root / "output" / "Patch.esp";
  write_bytes(input, make_remap_fixture());
  std::filesystem::create_directories(output.parent_path());

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);

  const auto before = collect_form_keys(plugin);
  EXPECT_TRUE(before.contains("000800:Patch.esp"));
  EXPECT_TRUE(before.contains("00009A:Keep.esp"));

  const radicle_master_descriptor masters[] = {
      {"Fallout4.esm", RADICLE_PLUGIN_FULL},
      {"Keep.esp", RADICLE_PLUGIN_FULL},
  };
  ASSERT_EQ(radicle_plugin_remap_masters(plugin, masters, 2), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_plugin_destroy(plugin);
  plugin = nullptr;

  ASSERT_EQ(radicle_plugin_read_file(output.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(plugin, nullptr);
  const auto after = collect_form_keys(plugin);
  EXPECT_TRUE(after.contains("000800:Patch.esp"));
  EXPECT_TRUE(after.contains("00009A:Keep.esp"));
  radicle_plugin_destroy(plugin);
}
