#include "fo4/c_api_authoring.h"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Plugin {
  radicle_plugin* handle{nullptr};
  explicit Plugin(const char* filename = "Authoring.esp") {
    EXPECT_EQ(radicle_plugin_create(filename, &handle), RADICLE_STATUS_OK);
  }
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

std::vector<std::byte> zstr(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> coupled_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const std::pair<const char*, std::vector<std::pair<const char*, std::vector<std::byte>>>> records[] = {
      {"ARMO", {{"EDID", zstr("Armor")}, {"MOD2", zstr("a.nif")}, {"DESC", zstr("Desc")},
                {"DATA", std::vector<std::byte>(12)}}},
      {"PROJ", {{"EDID", zstr("Shot")}, {"OBND", std::vector<std::byte>(12)}, {"DEST", std::vector<std::byte>(8)},
                {"DSTD", std::vector<std::byte>(20)}, {"DSTF", {}}, {"DATA", std::vector<std::byte>(92)},
                {"DNAM", std::vector<std::byte>(4)}, {"NAM1", zstr("m.nif")}, {"VNAM", std::vector<std::byte>(4)}}}};
  std::uint32_t form_id = 0x800;
  for (const auto& [signature, subs] : records) {
    const auto group = fo4::test::begin_group(out, signature, 0);
    const auto record = fo4::test::begin_record(out, signature, 0, form_id++);
    for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<std::byte> debr_fixture() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const std::pair<std::uint32_t, std::vector<std::pair<const char*, std::vector<std::byte>>>> records[] = {
      {0x800, {{"EDID", zstr("Gen")}, {"DATA", std::vector<std::byte>(3)},
               {"MODT", std::vector<std::byte>(4, std::byte{1})}, {"DATA", std::vector<std::byte>(3)},
               {"MODT", std::vector<std::byte>(4, std::byte{2})}}},
      {0x801, {{"EDID", zstr("Two")}, {"DATA", std::vector<std::byte>(3)}, {"DATA", std::vector<std::byte>(3)}}}};
  for (const auto& [form_id, subs] : records) {
    const auto group = fo4::test::begin_group(out, "DEBR", 0);
    const auto record = fo4::test::begin_record(out, "DEBR", 0, form_id);
    for (const auto& [name, data] : subs) fo4::test::write_subrecord(out, name, data);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<std::byte> written(radicle_plugin* handle) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
  std::vector<std::byte> copy(reinterpret_cast<std::byte*>(bytes), reinterpret_cast<std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  return copy;
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
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "DESC"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("default"), std::string::npos);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "KWDA"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, "ICO"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, key, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(nullptr, key, "ICON"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin.handle, radicle_form_key{nullptr, 0}, "ICON"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::strlen(radicle_last_error()), 0u);

  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, missing, "DESC", 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin.handle, key, "DESC", 0), RADICLE_STATUS_NOT_FOUND);
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

  EXPECT_EQ(radicle_plugin_create_record(plugin.handle, "XXXX", "A", &key), RADICLE_STATUS_INVALID_ARGUMENT);
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
  EXPECT_EQ(order(reread, 0x801), (std::vector<std::string>{"EDID", "OBND", "LVLD", "LVLM", "LVLF", "LLCT"}));
}

TEST(CApiAuthoring, CoupledSubrecordsAreRefusedWithoutChangingBytes) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_authoring_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Authoring.esp";
  {
    const auto bytes = coupled_fixture();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &handle), RADICLE_STATUS_OK) << radicle_last_error();
  const auto before = written(handle);

  const radicle_form_key armor{"Authoring.esp", 0x800};
  const radicle_form_key projectile{"Authoring.esp", 0x801};
  const std::pair<radicle_form_key, const char*> coupled[] = {
      {armor, "MOD2"}, {armor, "MOD4"}, {projectile, "MODL"}, {projectile, "DEST"}, {projectile, "DSTD"}};
  for (const auto& [key, signature] : coupled) {
    EXPECT_EQ(radicle_plugin_add_subrecord(handle, key, signature), RADICLE_STATUS_INVALID_ARGUMENT) << signature;
    EXPECT_NE(std::string(radicle_last_error()).find("not authorable"), std::string::npos);
    EXPECT_EQ(radicle_plugin_remove_subrecord(handle, key, signature, 0), RADICLE_STATUS_INVALID_ARGUMENT)
        << signature;
    EXPECT_NE(std::string(radicle_last_error()).find("not authorable"), std::string::npos);
  }
  EXPECT_EQ(written(handle), before);
  EXPECT_EQ(radicle_plugin_remove_subrecord(handle, armor, "DESC", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x800),
            (std::vector<std::string>{"EDID", "MOD2", "DATA"}));

  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiAuthoring, UnknownOrderRemovalIsRefusedAsInvalidArgumentWithoutChangingBytes) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_authoring_debr_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Authoring.esp";
  {
    const auto bytes = debr_fixture();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  radicle_plugin* handle = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &handle), RADICLE_STATUS_OK) << radicle_last_error();
  const auto before = written(handle);

  const radicle_form_key generic{"Authoring.esp", 0x800};
  EXPECT_EQ(radicle_plugin_remove_subrecord(handle, generic, "DATA", 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("cannot be proven safe"), std::string::npos);
  EXPECT_EQ(written(handle), before);
  EXPECT_EQ(order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x800),
            (std::vector<std::string>{"EDID", "DATA", "MODT", "DATA", "MODT"}));

  const radicle_form_key two{"Authoring.esp", 0x801};
  EXPECT_EQ(radicle_plugin_remove_subrecord(handle, two, "DATA", 1), RADICLE_STATUS_OK);
  EXPECT_EQ(order(fo4::read_plugin(written(handle), "Authoring.esp"), 0x801),
            (std::vector<std::string>{"EDID", "DATA"}));

  radicle_plugin_destroy(handle);
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(CApiAuthoring, CopiesARecordAsAnOverrideThroughTheExport) {
  Plugin source{"Source.esp"};
  Plugin target{"Target.esp"};
  const auto key = make_armor(source);
  ASSERT_NE(key.plugin, nullptr);

  radicle_record_copy_status status = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_INSERTED);
  EXPECT_EQ(order(target.snapshot(), 0x800), (std::vector<std::string>{"EDID", "DATA"}));

  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_ALREADY_EXISTS);
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_REPLACED);

  const radicle_form_key missing{key.plugin, 0x8FF};
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, missing, 0, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_copy_record_override(nullptr, source.handle, key, 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_copy_record_override(target.handle, source.handle, key, 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(order(target.snapshot(), 0x800), (std::vector<std::string>{"EDID", "DATA"}));
}
