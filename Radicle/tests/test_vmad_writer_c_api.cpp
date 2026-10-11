#include "fo4/c_api_vmad.h"
#include "vmad_writer_fixture.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

TEST(CApiVmadWriter, TypedSettersReparseAndRejectInvalidBoundaryValues) {
  namespace fixture = fo4::test::vmad;
  const auto root = std::filesystem::temp_directory_path() /
    ("radicle_vmad_writer_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  struct Cleanup {
    std::filesystem::path path;
    radicle_plugin* plugin{nullptr};
    ~Cleanup() { radicle_plugin_destroy(plugin); std::error_code error; std::filesystem::remove_all(path, error); }
  } cleanup{root, nullptr};
  const auto input = root / "Vmad.esp";
  const auto bytes = fixture::plugin_bytes();
  std::ofstream stream(input, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  ASSERT_EQ(radicle_plugin_read_file(input.string().c_str(), &cleanup.plugin), RADICLE_STATUS_OK);
  const radicle_form_key key{"Vmad.esp", 0x800};
  auto* plugin = cleanup.plugin;
  EXPECT_EQ(radicle_plugin_set_vmad_int(nullptr, key, 0, 4, 0, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 99, 4, 0, 1), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 0, 2, 0, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_bool(plugin, key, 0, 8, 0, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_float(plugin, key, 0, 6, 0, std::numeric_limits<float>::quiet_NaN()), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, "\xFF"), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto unchanged = root / "Unchanged.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, unchanged.string().c_str()), RADICLE_STATUS_OK);
  EXPECT_EQ(fo4::test::original_bytes(fo4::read_plugin_file(unchanged)), bytes);
  EXPECT_EQ(radicle_plugin_set_vmad_object(plugin, key, 0, 0, 0, {nullptr, 0}, -1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 2, 0, "Caf\xC3\xA9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_string(plugin, key, 0, 3, 0, "Caf\xC3\xA9"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_int(plugin, key, 0, 4, 0, -42), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_float(plugin, key, 0, 6, 0, 7.5f), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_vmad_bool(plugin, key, 0, 9, 1, 0), RADICLE_STATUS_OK);
  const auto output = root / "Saved.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin, output.string().c_str()), RADICLE_STATUS_OK);
  auto reparsed = fo4::read_plugin_file(output);
  const auto* record = fo4::find_record(reparsed, fixture::key(0x800, "Saved.esp"));
  ASSERT_NE(record, nullptr);
  const auto properties = fo4::read_vmad_scripts(record->subrecords[0].data).scripts[0].properties;
  EXPECT_EQ(properties[0].object->value(), 0u); EXPECT_EQ(properties[0].alias, -1);
  EXPECT_EQ(properties[2].text, "Caf\xC3\xA9"); EXPECT_EQ(properties[4].integer, -42);
  EXPECT_EQ(properties[6].real, 7.5f);
  EXPECT_EQ(record->subrecords[0].data[properties[9].value_offset + 5], std::byte{0});
  const auto& data = record->subrecords[0].data;
  const std::vector<std::byte> cafe{std::byte{0x43}, std::byte{0x61}, std::byte{0x66}, std::byte{0xC3}, std::byte{0xA9}};
  const auto text_at = properties[2].value_offset;
  EXPECT_EQ(data[text_at], std::byte{5}); EXPECT_EQ(data[text_at + 1], std::byte{0});
  EXPECT_EQ(std::vector<std::byte>(data.begin() + text_at + 2, data.begin() + text_at + 7), cafe);
  const auto element_at = properties[3].value_offset + 4;
  EXPECT_EQ(data[element_at], std::byte{5}); EXPECT_EQ(data[element_at + 1], std::byte{0});
  EXPECT_EQ(std::vector<std::byte>(data.begin() + element_at + 2, data.begin() + element_at + 7), cafe);
}
