#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"
#include "fo4/c_api_pet_write.h"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void begin_plugin(fo4::test::ByteWriter& out, std::uint32_t count, std::uint32_t next_id) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(count);
  hedr.u32(next_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  begin_plugin(out, 5, 0x805);
  const auto group = fo4::test::begin_group(out, "FLST", 0);
  const std::uint32_t ids[] = {0x800, 0x801, 0x802, 0x803};
  for (const auto id : ids) {
    const auto record = fo4::test::begin_record(out, "FLST", id == 0x800 ? 0x20u : 0u, id);
    std::vector<std::uint32_t> items;
    if (id == 0x802) items = {0x801};
    if (id == 0x803) items = {0x800, 0x801};
    for (const auto item : items) {
      fo4::test::ByteWriter link;
      link.u32(item);
      fo4::test::write_subrecord(out, "LNAM", link.bytes);
    }
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  const auto refrs = fo4::test::begin_group(out, "REFR", 0);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x804);
  fo4::test::ByteWriter health;
  health.u32(50);
  fo4::test::write_subrecord(out, "XHLT", health.bytes);
  fo4::test::ByteWriter count;
  count.u32(3);
  fo4::test::write_subrecord(out, "XWCN", count.bytes);
  fo4::test::ByteWriter floats;
  for (int i = 1; i <= 12; ++i) floats.f32(static_cast<float>(i));
  fo4::test::write_subrecord(out, "XWCU", floats.bytes);
  fo4::test::end_record(out, refr);
  fo4::test::end_group(out, refrs);
  return out.bytes;
}

std::vector<std::byte> make_master() {
  fo4::test::ByteWriter out;
  begin_plugin(out, 1, 0x801);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Base\0", 5});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> make_nested() {
  fo4::test::ByteWriter out;
  begin_plugin(out, 2, 0x802);
  const auto cells = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x800);
  fo4::test::end_record(out, cell);
  const std::string label{char(0), char(8), char(0), char(0)};
  const auto children = fo4::test::begin_group(out, label, 6);
  const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x801);
  fo4::test::end_record(out, refr);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, cells);
  return out.bytes;
}

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::optional<std::uint32_t> first_word(const fo4::Plugin& plugin, std::uint32_t local, std::string_view sig) {
  const auto* record = fo4::find_record(plugin, fo4::FormKey{plugin.key, fo4::LocalFormId{local}});
  if (record == nullptr) return std::nullopt;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != sig || sub.data.size() < 4) continue;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(sub.data[i])) << (8 * i);
    }
    return value;
  }
  return std::nullopt;
}

class CApiPetWrite : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_c_pet_write_" + std::to_string(id));
    const auto path = root_ / "src" / "Pet.esp";
    write_bytes(path, make_plugin());
    ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin_), RADICLE_STATUS_OK) << radicle_last_error();
  }

  void TearDown() override {
    radicle_plugin_destroy(plugin_);
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  static radicle_form_key key(std::uint32_t local) { return {"Pet.esp", local}; }

  std::filesystem::path save_to(const char* directory) {
    const auto out = root_ / directory / "Pet.esp";
    std::filesystem::create_directories(out.parent_path());
    EXPECT_EQ(radicle_plugin_write_file(plugin_, out.string().c_str()), RADICLE_STATUS_OK) << radicle_last_error();
    return out;
  }

  fo4::Plugin saved() { return fo4::read_plugin_file(save_to("out")); }

  std::filesystem::path root_;
  radicle_plugin* plugin_{nullptr};
};

}

TEST_F(CApiPetWrite, MaintenanceExportsMapStatusesAndArguments) {
  size_t undeleted = 99;
  size_t skipped = 99;
  EXPECT_EQ(radicle_plugin_clean_deleted(plugin_, &undeleted, &skipped), RADICLE_STATUS_OK);
  EXPECT_EQ(undeleted, 1u);
  EXPECT_EQ(skipped, 0u);
  EXPECT_EQ(radicle_plugin_clean_deleted(plugin_, &undeleted, &skipped), RADICLE_STATUS_OK);
  EXPECT_EQ(undeleted, 0u);
  EXPECT_EQ(radicle_plugin_clean_deleted(nullptr, &undeleted, &skipped), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_clean_deleted(plugin_, nullptr, &skipped), RADICLE_STATUS_INVALID_ARGUMENT);

  size_t count = 99;
  EXPECT_EQ(radicle_plugin_renumber_all(plugin_, 0x7FF, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_renumber_all(plugin_, 0x800, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_plugin_renumber_all(plugin_, 0x900, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 5u);
  EXPECT_EQ(radicle_plugin_renumber_all(plugin_, 0x900, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_renumber_all(nullptr, 0x900, &count), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_form_key renumbered{};
  EXPECT_EQ(radicle_plugin_renumber_form_id(plugin_, key(0x900), 0x910, 1, &renumbered), RADICLE_STATUS_OK);
  EXPECT_EQ(renumbered.local_id, 0x910u);
  EXPECT_EQ(radicle_plugin_renumber_form_id(plugin_, key(0x999), 0x911, 1, &renumbered), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_renumber_form_id(plugin_, key(0x901), 0x910, 1, &renumbered), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_renumber_form_id(plugin_, key(0x901), 0x1000000, 1, &renumbered), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_remove_record(plugin_, key(0x910)), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_record(plugin_, key(0x910)), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_record(nullptr, key(0x910)), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiPetWrite, RenumberAllKeepsLinksPointingAtTheMovedRecords) {
  size_t count = 0;
  ASSERT_EQ(radicle_plugin_renumber_all(plugin_, 0x801, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 5u);
  const auto reloaded = saved();
  EXPECT_EQ(first_word(reloaded, 0x803, "LNAM"), 0x802u);
  EXPECT_EQ(first_word(reloaded, 0x804, "LNAM"), 0x801u);
}

TEST_F(CApiPetWrite, RetargetLinksReportsOnlyTheNamedRecordsChanges) {
  size_t changed = 99;
  EXPECT_EQ(radicle_plugin_retarget_links(plugin_, key(0x802), key(0x801), key(0x800), &changed), RADICLE_STATUS_OK);
  EXPECT_EQ(changed, 1u);
  EXPECT_EQ(first_word(saved(), 0x802, "LNAM"), 0x800u);
  EXPECT_EQ(radicle_plugin_retarget_links(plugin_, key(0x802), key(0x801), key(0x800), &changed), RADICLE_STATUS_OK);
  EXPECT_EQ(changed, 0u);
  EXPECT_EQ(radicle_plugin_retarget_links(plugin_, key(0x999), key(0x801), key(0x800), &changed), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_retarget_links(plugin_, key(0x802), key(0x801), key(0x800), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiPetWrite, FieldSchemaExportListsTableRowsAndFreesThem) {
  radicle_field_schema_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_record_field_schema("REFR", &rows, &count), RADICLE_STATUS_OK);
  ASSERT_NE(rows, nullptr);
  bool scalar = false;
  bool counted = false;
  for (size_t i = 0; i < count; ++i) {
    const std::string path = rows[i].path;
    if (path == "REFR\\HealthPercent") scalar = rows[i].kind == RADICLE_FIELD_SCALAR;
    if (path == "REFR\\WaterCurrentVelocities") counted = rows[i].kind == RADICLE_FIELD_COUNTED_ARRAY;
  }
  EXPECT_TRUE(scalar);
  EXPECT_TRUE(counted);
  radicle_field_schema_free(rows);
  radicle_field_schema_free(nullptr);
  EXPECT_EQ(radicle_record_field_schema(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_record_field_schema("REFR", nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiPetWrite, FieldSchemaExExportCarriesScalarMetadata) {
  radicle_field_schema_ex_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_record_field_schema_ex("QUST", &rows, &count), RADICLE_STATUS_OK);
  ASSERT_NE(rows, nullptr);
  const radicle_field_schema_ex_row* flags = nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(rows[i].path, "QUST\\Flags") == 0) flags = &rows[i];
  }
  ASSERT_NE(flags, nullptr);
  EXPECT_EQ(flags->kind, RADICLE_FIELD_SCALAR);
  EXPECT_EQ(flags->storage, 1);
  EXPECT_EQ(flags->presentation, 3);
  EXPECT_STREQ(flags->subrecord, "DNAM");
  EXPECT_EQ(flags->embedded, 1);
  EXPECT_GT(flags->name_count, 10u);
  EXPECT_NE(flags->names, nullptr);
  EXPECT_STREQ(flags->names[11].name, "Active");
  radicle_field_schema_ex_free(rows);
}

TEST_F(CApiPetWrite, SchemaRecordSignaturesListsKnownTypes) {
  char** signatures = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_schema_record_signatures(&signatures, &count), RADICLE_STATUS_OK);
  ASSERT_NE(signatures, nullptr);
  EXPECT_GT(count, 120u);
  bool qust = false;
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(signatures[i], "QUST") == 0) qust = true;
  }
  EXPECT_TRUE(qust);
  radicle_schema_record_signatures_free(signatures);
}

TEST_F(CApiPetWrite, FieldAndListExportsRefuseUnknownPathsAndMissingRecords) {
  EXPECT_EQ(radicle_plugin_set_record_field(plugin_, key(0x800), "MISC\\Nope", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_field(plugin_, key(0x999), "REFR\\HealthPercent", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_field(plugin_, key(0x800), "REFR\\HealthPercent", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_field(plugin_, key(0x804), "REFR\\HealthPercent", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  size_t size = 0;
  EXPECT_EQ(radicle_plugin_list_size(plugin_, key(0x800), "REFR\\HealthPercent", &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_list_size(plugin_, key(0x999), "REFR\\HealthPercent", &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_list_remove(plugin_, key(0x804), "REFR\\HealthPercent", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_list_size(nullptr, key(0x800), "REFR\\HealthPercent", &size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiPetWrite, FieldAndListEditsRoundTripAndKeepTheCounter) {
  EXPECT_EQ(radicle_plugin_set_record_field(plugin_, key(0x804), "REFR\\HealthPercent", 0, "75"), RADICLE_STATUS_OK);
  const radicle_form_key added{"Pet.esp", 0x802};
  size_t size = 0;
  ASSERT_EQ(radicle_plugin_list_size(plugin_, key(0x803), "FLST\\Items", &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 2u);
  EXPECT_EQ(radicle_plugin_list_insert(plugin_, key(0x803), "FLST\\Items", 1, &added), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_list_remove(plugin_, key(0x803), "FLST\\Items", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_list_insert(plugin_, key(0x803), "FLST\\Items", 9, &added), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_list_size(plugin_, key(0x803), "FLST\\Items", &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 2u);

  const char* array = "REFR\\WaterCurrentVelocities";
  ASSERT_EQ(radicle_plugin_list_size(plugin_, key(0x804), array, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 3u);
  EXPECT_EQ(radicle_plugin_list_insert(plugin_, key(0x804), array, 1, nullptr), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_list_size(plugin_, key(0x804), array, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 4u);
  EXPECT_EQ(first_word(saved(), 0x804, "XWCN"), 4u);
  EXPECT_EQ(radicle_plugin_list_remove(plugin_, key(0x804), array, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(first_word(saved(), 0x804, "XWCN"), 3u);

  const auto path = save_to("env");
  const auto path_text = path.string();
  const char* paths[] = {path_text.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* refr = nullptr;
  const radicle_record* list = nullptr;
  ASSERT_EQ(radicle_env_winner(env, key(0x804), &refr), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_env_winner(env, key(0x803), &list), RADICLE_STATUS_OK);
  char* text = nullptr;
  ASSERT_EQ(radicle_record_get_field(env, refr, "REFR\\HealthPercent", 0, &text), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(text), "75");
  radicle_record_scalar_free(text);
  ASSERT_EQ(radicle_record_get_field(env, list, "FLST\\Items", 0, &text), RADICLE_STATUS_OK);
  EXPECT_NE(std::string(text).find("000802"), std::string::npos);
  radicle_record_scalar_free(text);
  EXPECT_EQ(radicle_record_get_field(env, list, "FLST\\Items", 5, &text), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(text, nullptr);
  ASSERT_EQ(radicle_record_list_size(env, refr, array, &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 3u);
  ASSERT_EQ(radicle_record_get_field(env, refr, array, 0, &text), RADICLE_STATUS_OK);
  ASSERT_NE(text, nullptr);
  EXPECT_EQ(strlen(text), 32u);
  radicle_record_scalar_free(text);
  radicle_env_destroy(env);
}

TEST_F(CApiPetWrite, NestedRecordsBlockRenumberingAndLeaveTheBytesAlone) {
  const auto path = root_ / "nested" / "Pet.esp";
  write_bytes(path, make_nested());
  radicle_plugin* nested = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &nested), RADICLE_STATUS_OK) << radicle_last_error();
  size_t count = 99;
  EXPECT_EQ(radicle_plugin_renumber_all(nested, 0x900, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(count, 0u);
  radicle_form_key renamed{};
  EXPECT_EQ(radicle_plugin_renumber_form_id(nested, key(0x801), 0x900, 1, &renamed), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(renamed.plugin, nullptr);
  const auto out = root_ / "nested_out" / "Pet.esp";
  std::filesystem::create_directories(out.parent_path());
  ASSERT_EQ(radicle_plugin_write_file(nested, out.string().c_str()), RADICLE_STATUS_OK);
  EXPECT_EQ(fo4::write_plugin(fo4::read_plugin_file(out)), fo4::write_plugin(fo4::read_plugin_file(path)));
  radicle_plugin_destroy(nested);
}

TEST_F(CApiPetWrite, CopyRecordVersionInsertsReplacesAndLeavesStatusOnFailure) {
  const auto master = root_ / "master" / "Master.esm";
  const auto other = root_ / "other" / "Master.esm";
  write_bytes(master, make_master());
  write_bytes(other, make_master());
  const auto master_text = master.string();
  const auto other_text = other.string();
  const char* master_paths[] = {master_text.c_str()};
  const char* other_paths[] = {other_text.c_str()};
  radicle_env* env = nullptr;
  radicle_env* second = nullptr;
  ASSERT_EQ(radicle_env_load(master_paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_env_load(other_paths, 1, &second), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* version = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Master.esm", 0x800}, &version), RADICLE_STATUS_OK);

  radicle_plugin* patch = nullptr;
  ASSERT_EQ(radicle_plugin_create("Patch.esp", &patch), RADICLE_STATUS_OK);
  auto status = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  EXPECT_EQ(radicle_plugin_copy_record_version(patch, second, version, 0, &status), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_ALREADY_EXISTS);
  EXPECT_EQ(radicle_plugin_copy_record_version(patch, env, version, 0, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_INSERTED);
  EXPECT_EQ(radicle_plugin_copy_record_version(patch, env, version, 0, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_ALREADY_EXISTS);
  EXPECT_EQ(radicle_plugin_copy_record_version(patch, env, version, 1, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_COPY_REPLACED);
  EXPECT_EQ(radicle_plugin_remove_record(patch, {"Master.esm", 0x800}), RADICLE_STATUS_OK);
  radicle_plugin_destroy(patch);
  radicle_env_destroy(second);
  radicle_env_destroy(env);
}

TEST_F(CApiPetWrite, EslExportsRoundTripTheLightFlag) {
  EXPECT_EQ(radicle_plugin_set_light_flag(plugin_, 1), RADICLE_STATUS_OK);
  EXPECT_TRUE(saved().tes4.header.flags.is_small_file());
  EXPECT_EQ(radicle_plugin_set_light_flag(plugin_, 0), RADICLE_STATUS_OK);
  EXPECT_FALSE(saved().tes4.header.flags.is_small_file());
  EXPECT_EQ(radicle_plugin_set_light_flag(nullptr, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  size_t remapped = 99;
  uint32_t next = 0;
  EXPECT_EQ(radicle_plugin_compact_to_esl(plugin_, &remapped, &next), RADICLE_STATUS_OK);
  EXPECT_GT(next, 0u);
  EXPECT_EQ(radicle_plugin_compact_to_esl(plugin_, nullptr, &next), RADICLE_STATUS_INVALID_ARGUMENT);
}
