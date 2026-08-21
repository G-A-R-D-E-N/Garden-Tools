#include "fo4/c_api.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path testdata_path(const char* name) {
  return std::filesystem::path(FO4_TESTDATA_DIR) / name;
}

}  // namespace

TEST(CApi, RejectsInvalidArgumentsWithoutThrowing) {
  radicle_plugin* plugin = nullptr;
  EXPECT_EQ(radicle_plugin_read_file(nullptr, &plugin), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("required"), std::string::npos);
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
