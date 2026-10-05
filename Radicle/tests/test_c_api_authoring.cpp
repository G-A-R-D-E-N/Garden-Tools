#include "fo4/c_api_authoring.h"
#include "fo4/plugin/reader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Plugin {
  radicle_plugin* handle{nullptr};
  Plugin() { EXPECT_EQ(radicle_plugin_create("Authoring.esp", &handle), RADICLE_STATUS_OK); }
  ~Plugin() { radicle_plugin_destroy(handle); }
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;

  fo4::Plugin snapshot() const {
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
    std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes),
                                reinterpret_cast<std::byte*>(bytes) + size);
    radicle_free_bytes(bytes);
    return fo4::read_plugin(copy, "Authoring.esp");
  }
};

std::vector<std::string> order(const fo4::Plugin& plugin, std::uint32_t local) {
  std::vector<std::string> out;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.form_id.value() != local) continue;
    for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  }
  return out;
}

radicle_form_key make_armor(Plugin& plugin, const char* editor_id = "NewArmor") {
  radicle_form_key key{};
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", editor_id, &key), RADICLE_STATUS_OK);
  return key;
}

}  // namespace

TEST(CApiAuthoring, CreatesAddsAndRemovesThroughTheExports) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  ASSERT_NE(key.plugin, nullptr);
  EXPECT_STREQ(key.plugin, "Authoring.esp");
  EXPECT_EQ(key.local_id, 0x800u);

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "MICO"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICON"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICON"), RADICLE_STATUS_OK);
  EXPECT_EQ(order(plugin.snapshot(), 0x800),
            (std::vector<std::string>{"EDID", "ICON", "MICO", "DATA"}));

  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "MICO", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "MICO", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(order(plugin.snapshot(), 0x800), (std::vector<std::string>{"EDID", "ICON", "DATA"}));
}

TEST(CApiAuthoring, MapsEveryRefusalToAStatus) {
  Plugin plugin;
  const auto key = make_armor(plugin);
  const radicle_form_key missing{key.plugin, 0x8FF};

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, missing, "ICON"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "MOD2"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "KWDA"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICO"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(nullptr, key, "ICON"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, radicle_form_key{nullptr, 0}, "ICON"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, missing, "MOD2", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "MOD2", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "DATA", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "KWDA", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "XX", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(order(plugin.snapshot(), 0x800), (std::vector<std::string>{"EDID", "DATA"}));
}

TEST(CApiAuthoring, CreateRecordRefusalsLeaveThePluginUntouched) {
  Plugin plugin;
  radicle_form_key key{"x", 9};

  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "MISC", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(key.plugin, nullptr);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "EYES", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "\xE4\xB8\xAD", &key),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "\xFF", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", "A", nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARMO", nullptr, &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "ARM", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_record(nullptr, "ARMO", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);

  const auto first = make_armor(plugin, "Caf\xC3\xA9");
  EXPECT_EQ(first.local_id, 0x800u);
  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "LVSP", "List", &key), RADICLE_STATUS_OK);
  EXPECT_EQ(key.local_id, 0x801u);
  const auto reread = plugin.snapshot();
  EXPECT_EQ(order(reread, 0x801), (std::vector<std::string>{"EDID", "LVLD", "LVLM", "LVLF", "LLCT"}));
}
