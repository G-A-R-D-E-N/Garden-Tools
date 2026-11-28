#include "fo4/c_api_loadorder.h"
#include "fo4/c_api_plugin_ops.h"
#include "fo4/c_api_pet_write.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using PluginPtr = std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)>;
using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;

struct Rec {
  std::uint32_t form_id;
  std::uint32_t flags;
};

std::vector<std::byte> misc_plugin(std::initializer_list<std::string_view> masters,
                                   std::uint32_t next_form_id,
                                   std::initializer_list<Rec> records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::string name(master);
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", std::string_view{name});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  for (const auto& rec : records) {
    const auto record = fo4::test::begin_record(out, "MISC", rec.flags, rec.form_id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Misc\0", 5});
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> cell_plugin(std::uint32_t cell_id,
                                   std::uint32_t reference_id,
                                   std::uint32_t next_form_id) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto cell = fo4::test::begin_record(out, "CELL", 0, cell_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Cell\0", 5});
  fo4::test::end_record(out, cell);
  const auto children = fo4::test::begin_group(out, std::string_view{"\0\0\0\0", 4}, 6);
  out.patch_u32(children + 8, cell_id);
  const auto reference = fo4::test::begin_record(out, "REFR", 0, reference_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Ref\0", 4});
  fo4::test::end_record(out, reference);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, top);
  return out.bytes;
}

// Removes every scratch directory when the test binary exits, so runs do not pile up in the temp folder.
struct ScratchDirs {
  std::vector<std::filesystem::path> paths;
  ~ScratchDirs() {
    for (const auto& path : paths) {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  }
};

std::filesystem::path scratch_dir(std::string_view name) {
  static ScratchDirs created;
  const auto dir = std::filesystem::temp_directory_path() /
                   (std::string("radicle_plugin_ops_") + std::string(name) + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(dir / "out");
  created.paths.push_back(dir);
  return dir;
}

std::string utf8(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::filesystem::path write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

PluginPtr read_plugin(const std::filesystem::path& path) {
  radicle_plugin* raw = nullptr;
  if (radicle_plugin_read_file(utf8(path).c_str(), &raw) != RADICLE_STATUS_OK) {
    throw std::runtime_error(radicle_last_error());
  }
  return PluginPtr(raw, &radicle_plugin_destroy);
}

EnvPtr load_env(const std::vector<std::filesystem::path>& paths) {
  std::vector<std::string> names;
  for (const auto& path : paths) names.push_back(utf8(path));
  std::vector<const char*> pointers;
  for (const auto& name : names) pointers.push_back(name.c_str());
  radicle_env* raw = nullptr;
  if (radicle_env_load(pointers.data(), pointers.size(), &raw) != RADICLE_STATUS_OK) {
    throw std::runtime_error(radicle_last_error());
  }
  return EnvPtr(raw, &radicle_env_destroy);
}

EnvPtr save_and_load(radicle_plugin* plugin, const std::filesystem::path& dir, const char* name) {
  const auto path = dir / "out" / name;
  if (radicle_plugin_write_file(plugin, utf8(path).c_str()) != RADICLE_STATUS_OK) {
    throw std::runtime_error(radicle_last_error());
  }
  return load_env({path});
}

std::vector<std::byte> saved_bytes(radicle_plugin* plugin) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  if (radicle_plugin_write_bytes(plugin, &bytes, &size) != RADICLE_STATUS_OK) {
    throw std::runtime_error(radicle_last_error());
  }
  std::vector<std::byte> out;
  for (size_t i = 0; i < size; ++i) out.push_back(static_cast<std::byte>(bytes[i]));
  radicle_free_bytes(bytes);
  return out;
}

std::uint32_t le32(const std::vector<std::byte>& bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(at + i))) << (8 * i);
  }
  return value;
}

std::vector<std::string> take(char** strings, size_t count) {
  std::vector<std::string> out(strings, strings + count);
  radicle_free_string_array(strings, count);
  return out;
}

}  // namespace

TEST(CApiPluginOps, SetLightFlagMarksAPluginWhoseIdsFitTheLightRange) {
  const auto dir = scratch_dir("light");
  auto plugin = read_plugin(write_bytes(dir / "Light.esp", misc_plugin({}, 0x801, {{0x800, 0}})));
  ASSERT_EQ(radicle_plugin_set_light_flag(plugin.get(), 1), RADICLE_STATUS_OK) << radicle_last_error();
  auto env = save_and_load(plugin.get(), dir, "Light.esp");
  radicle_env_plugin_info info{};
  ASSERT_EQ(radicle_env_get_plugin_info(env.get(), 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.kind, RADICLE_PLUGIN_LIGHT);
}

TEST(CApiPluginOps, SetLightFlagRefusesOwnIdsOutsideTheLightRange) {
  const auto dir = scratch_dir("light_refuse");
  auto high = read_plugin(write_bytes(dir / "High.esp", misc_plugin({}, 0x9001, {{0x9000, 0}})));
  EXPECT_EQ(radicle_plugin_set_light_flag(high.get(), 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_FALSE(std::string(radicle_last_error()).empty());
  auto low = read_plugin(write_bytes(dir / "Low.esp", misc_plugin({}, 0x101, {{0x100, 0}})));
  EXPECT_EQ(radicle_plugin_set_light_flag(low.get(), 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_light_flag(nullptr, 1), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPluginOps, CompactToEslMovesOutOfRangeRecordsAndReportsTheNextId) {
  const auto dir = scratch_dir("compact");
  auto plugin = read_plugin(write_bytes(dir / "Compact.esp", misc_plugin({}, 0x9001, {{0x800, 0}, {0x9000, 0}})));
  radicle_esl_compaction_status status = RADICLE_ESL_REKEY_FAILED;
  size_t remapped = 0;
  uint32_t next = 0;
  char** blockers = nullptr;
  size_t blocker_count = 0;
  ASSERT_EQ(radicle_plugin_compact_to_esl_detailed(plugin.get(), &status, &remapped, &next, &blockers, &blocker_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_ESL_COMPACTED);
  EXPECT_EQ(remapped, 1u);
  EXPECT_EQ(next, 0x802u);
  EXPECT_TRUE(take(blockers, blocker_count).empty());

  auto env = save_and_load(plugin.get(), dir, "Compact.esp");
  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Compact.esp", 0x801}, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Compact.esp", 0x9000}, &record),
            RADICLE_STATUS_NOT_FOUND);
}

TEST(CApiPluginOps, CompactToEslReportsNestedBlockersWithoutMutation) {
  const auto dir = scratch_dir("compact_nested");
  auto plugin = read_plugin(write_bytes(dir / "Cell.esp", cell_plugin(0x800, 0x9000, 0x9001)));
  const auto before = saved_bytes(plugin.get());
  radicle_esl_compaction_status status = RADICLE_ESL_COMPACTED;
  size_t remapped = 99;
  uint32_t next = 0;
  char** blockers = nullptr;
  size_t blocker_count = 0;
  ASSERT_EQ(radicle_plugin_compact_to_esl_detailed(plugin.get(), &status, &remapped, &next, &blockers, &blocker_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_ESL_BLOCKED_NESTED_RECORDS);
  EXPECT_EQ(remapped, 0u);
  EXPECT_EQ(take(blockers, blocker_count), std::vector<std::string>{"009000:Cell.esp\tREFR\t1"});
  EXPECT_EQ(saved_bytes(plugin.get()), before);
}

TEST(CApiPluginOps, CompactToEslLowersTheNextIdOfAnAlreadyCompatiblePlugin) {
  const auto dir = scratch_dir("compact_ok");
  auto plugin = read_plugin(write_bytes(dir / "Only.esp", misc_plugin({}, 0x9001, {{0x800, 0}})));
  radicle_esl_compaction_status status = RADICLE_ESL_COMPACTED;
  size_t remapped = 99;
  uint32_t next = 0;
  char** blockers = nullptr;
  size_t blocker_count = 0;
  ASSERT_EQ(radicle_plugin_compact_to_esl_detailed(plugin.get(), &status, &remapped, &next, &blockers, &blocker_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_ESL_ALREADY_COMPATIBLE);
  EXPECT_EQ(remapped, 0u);
  EXPECT_EQ(next, 0x801u);
  EXPECT_TRUE(take(blockers, blocker_count).empty());
  EXPECT_EQ(radicle_plugin_compact_to_esl_detailed(nullptr, &status, &remapped, &next, &blockers, &blocker_count),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPluginOps, SetHedrNextFormIdWritesTheTes4Field) {
  const auto dir = scratch_dir("hedr");
  auto plugin = read_plugin(write_bytes(dir / "Hedr.esp", misc_plugin({}, 0x900, {{0x800, 0}})));
  ASSERT_EQ(radicle_plugin_set_hedr_next_form_id(plugin.get(), 0x850), RADICLE_STATUS_OK) << radicle_last_error();
  const auto bytes = saved_bytes(plugin.get());
  ASSERT_GE(bytes.size(), 42u);
  EXPECT_EQ(le32(bytes, 38), 0x850u);
  EXPECT_EQ(radicle_plugin_set_hedr_next_form_id(nullptr, 1), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPluginOps, SetRecordFlagsReportsChangedUnchangedAndRefusesTheDeletedBit) {
  const auto dir = scratch_dir("flags");
  auto plugin = read_plugin(write_bytes(dir / "Misc.esp", misc_plugin({}, 0x900, {{0x800, 0}})));
  const radicle_form_key record{"Misc.esp", 0x800};
  radicle_record_flag_status status = RADICLE_RECORD_FLAGS_UNCHANGED;
  ASSERT_EQ(radicle_plugin_set_record_flags(plugin.get(), record, 0x400, &status), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_RECORD_FLAGS_CHANGED);
  ASSERT_EQ(radicle_plugin_set_record_flags(plugin.get(), record, 0x400, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_FLAGS_UNCHANGED);
  ASSERT_EQ(radicle_plugin_set_record_flags(plugin.get(), record, 0x420, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_FLAGS_DELETED_BIT);
  ASSERT_EQ(radicle_plugin_set_record_flags(plugin.get(), radicle_form_key{"Misc.esp", 0x999}, 0x400, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_FLAGS_RECORD_MISSING);
  EXPECT_EQ(radicle_plugin_set_record_flags(plugin.get(), radicle_form_key{nullptr, 0}, 0x400, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPluginOps, MarkOverrideDeletedMarksAMasterRecordOnceAndRefusesLocalRecords) {
  const auto dir = scratch_dir("mark");
  auto plugin = read_plugin(
      write_bytes(dir / "Patch.esp", misc_plugin({"Master.esm"}, 0x901, {{0x800, 0}, {0x01000900, 0}})));
  radicle_mark_deleted_status status = RADICLE_MARK_DELETED_RECORD_MISSING;
  ASSERT_EQ(radicle_plugin_mark_override_deleted(plugin.get(), radicle_form_key{"Master.esm", 0x800}, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_MARK_DELETED_MARKED);
  ASSERT_EQ(radicle_plugin_mark_override_deleted(plugin.get(), radicle_form_key{"Master.esm", 0x800}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_MARK_DELETED_ALREADY_DELETED);
  ASSERT_EQ(radicle_plugin_mark_override_deleted(plugin.get(), radicle_form_key{"Patch.esp", 0x900}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_MARK_DELETED_LOCAL_RECORD);
  ASSERT_EQ(radicle_plugin_mark_override_deleted(plugin.get(), radicle_form_key{"Master.esm", 0x123}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_MARK_DELETED_RECORD_MISSING);
}

TEST(CApiPluginOps, DeleteLocalRecordRemovesOwnRecordAndRefusesMasterRecords) {
  const auto dir = scratch_dir("delete");
  auto plugin = read_plugin(
      write_bytes(dir / "Patch.esp", misc_plugin({"Master.esm"}, 0x901, {{0x01000900, 0}, {0x800, 0}})));
  radicle_record_delete_status status = RADICLE_RECORD_DELETE_RECORD_MISSING;
  ASSERT_EQ(radicle_plugin_delete_local_record(plugin.get(), radicle_form_key{"Master.esm", 0x800}, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_RECORD_DELETE_NOT_LOCAL);
  ASSERT_EQ(radicle_plugin_delete_local_record(plugin.get(), radicle_form_key{"Patch.esp", 0x900}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_DELETE_DELETED);
  auto env = save_and_load(plugin.get(), dir, "Patch.esp");
  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_winner(env.get(), radicle_form_key{"Patch.esp", 0x900}, &record), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_delete_local_record(plugin.get(), radicle_form_key{"Patch.esp", 0x900}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_RECORD_DELETE_RECORD_MISSING);
}

TEST(CApiPluginOps, RefusedDeleteAndCleanLeavePluginBytesUnchanged) {
  const auto dir = scratch_dir("refused_bytes");
  auto plugin = read_plugin(
      write_bytes(dir / "Patch.esp", misc_plugin({"Master.esm"}, 0x901, {{0x01000900, 0}, {0x800, 0}})));
  const auto before = saved_bytes(plugin.get());
  radicle_record_delete_status delete_status = RADICLE_RECORD_DELETE_DELETED;
  ASSERT_EQ(radicle_plugin_delete_local_record(plugin.get(), radicle_form_key{"Patch.esp", 0x999}, &delete_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(delete_status, RADICLE_RECORD_DELETE_RECORD_MISSING);
  ASSERT_EQ(radicle_plugin_delete_local_record(plugin.get(), radicle_form_key{"Master.esm", 0x800}, &delete_status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(delete_status, RADICLE_RECORD_DELETE_NOT_LOCAL);
  EXPECT_EQ(saved_bytes(plugin.get()), before);
  radicle_clean_masters_status clean_status = RADICLE_CLEAN_MASTERS_INCOMPLETE_USAGE;
  char** removed = nullptr;
  size_t removed_count = 0;
  ASSERT_EQ(radicle_plugin_clean_unused_masters(plugin.get(), &clean_status, &removed, &removed_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(clean_status, RADICLE_CLEAN_MASTERS_NOTHING_UNUSED);
  EXPECT_TRUE(take(removed, removed_count).empty());
  EXPECT_EQ(saved_bytes(plugin.get()), before);
}

TEST(CApiPluginOps, UndeleteAndDisableReferenceRefusesNonReferencesAndMissingRecords) {
  const auto dir = scratch_dir("undelete");
  auto plugin = read_plugin(write_bytes(dir / "Misc.esp", misc_plugin({}, 0x900, {{0x800, 0}})));
  auto env = load_env({dir / "Misc.esp"});
  radicle_undelete_status status = RADICLE_UNDELETE_UNDELETED;
  ASSERT_EQ(radicle_plugin_undelete_and_disable_reference(plugin.get(), env.get(),
                                                          radicle_form_key{"Misc.esp", 0x800}, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_UNDELETE_NOT_A_REFERENCE);
  ASSERT_EQ(radicle_plugin_undelete_and_disable_reference(plugin.get(), env.get(),
                                                          radicle_form_key{"Misc.esp", 0x999}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_UNDELETE_RECORD_MISSING);
  EXPECT_EQ(radicle_plugin_undelete_and_disable_reference(plugin.get(), nullptr,
                                                          radicle_form_key{"Misc.esp", 0x800}, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiPluginOps, AddMasterAppendsOnceAndRefusesASelfMaster) {
  const auto dir = scratch_dir("add_master");
  auto plugin = read_plugin(write_bytes(dir / "Patch.esp", misc_plugin({}, 0x900, {{0x800, 0}})));
  radicle_master_add_status status = RADICLE_MASTER_ALREADY_PRESENT;
  ASSERT_EQ(radicle_plugin_add_master_detailed(plugin.get(), "Base.esm", RADICLE_PLUGIN_FULL, &status), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_MASTER_ADDED);
  ASSERT_EQ(radicle_plugin_add_master_detailed(plugin.get(), "base.ESM", RADICLE_PLUGIN_FULL, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_MASTER_ALREADY_PRESENT);
  EXPECT_EQ(radicle_plugin_add_master_detailed(plugin.get(), "Patch.esp", RADICLE_PLUGIN_FULL, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_master_detailed(plugin.get(), nullptr, RADICLE_PLUGIN_FULL, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  auto env = save_and_load(plugin.get(), dir, "Patch.esp");
  radicle_env_plugin_info info{};
  ASSERT_EQ(radicle_env_get_plugin_info(env.get(), 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.master_count, 1u);
}

TEST(CApiPluginOps, CleanUnusedMastersRemovesOnlyUnreferencedMasters) {
  const auto dir = scratch_dir("clean_masters");
  auto plugin = read_plugin(
      write_bytes(dir / "Patch.esp", misc_plugin({"Base.esm", "Unused.esm"}, 0x901, {{0x800, 0}, {0x02000900, 0x20}})));
  radicle_clean_masters_status status = RADICLE_CLEAN_MASTERS_INCOMPLETE_USAGE;
  char** removed = nullptr;
  size_t removed_count = 0;
  ASSERT_EQ(radicle_plugin_clean_unused_masters(plugin.get(), &status, &removed, &removed_count),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_CLEAN_MASTERS_CLEANED);
  EXPECT_EQ(take(removed, removed_count), std::vector<std::string>{"Unused.esm"});
  auto env = save_and_load(plugin.get(), dir, "Patch.esp");
  radicle_env_plugin_info info{};
  ASSERT_EQ(radicle_env_get_plugin_info(env.get(), 0, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.master_count, 1u);
  ASSERT_EQ(radicle_plugin_clean_unused_masters(plugin.get(), &status, &removed, &removed_count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CLEAN_MASTERS_NOTHING_UNUSED);
  EXPECT_TRUE(take(removed, removed_count).empty());
}

TEST(CApiPluginOps, PluginListWriteRoundTripsThroughTheReader) {
  const auto dir = scratch_dir("plugin_list");
  const auto path = dir / "out" / "plugins.txt";
  const char* names[] = {"Fallout4.esm", "Mod.esp"};
  const int32_t active[] = {1, 0};
  ASSERT_EQ(radicle_plugin_list_write(utf8(path).c_str(), names, active, 2), RADICLE_STATUS_OK)
      << radicle_last_error();
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_EQ(text, "*Fallout4.esm\r\nMod.esp\r\n");
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_plugin_list_read(utf8(path).c_str(), &rows, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(take(rows, count), (std::vector<std::string>{"Fallout4.esm\t1\t1", "Mod.esp\t0\t2"}));
}

TEST(CApiPluginOps, PluginListWriteRefusesNamesTheReaderCannotReturn) {
  const auto dir = scratch_dir("plugin_list_refuse");
  const auto path = dir / "out" / "plugins.txt";
  const int32_t inactive[] = {0};
  const char* commented[] = {"#Note.esp"};
  EXPECT_EQ(radicle_plugin_list_write(utf8(path).c_str(), commented, inactive, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  const char* empty[] = {""};
  EXPECT_EQ(radicle_plugin_list_write(utf8(path).c_str(), empty, inactive, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  const char* starred_inactive[] = {"*Mod.esp"};
  EXPECT_EQ(radicle_plugin_list_write(utf8(path).c_str(), starred_inactive, inactive, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const int32_t active[] = {1};
  EXPECT_EQ(radicle_plugin_list_write(utf8(path).c_str(), starred_inactive, active, 1),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const char* control[] = {"A\tB.esp"};
  EXPECT_EQ(radicle_plugin_list_write(utf8(path).c_str(), control, inactive, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  const char* duplicate[] = {"Mod.esp", "mod.ESP"};
  const int32_t flags[] = {1, 0};
  EXPECT_EQ(radicle_plugin_list_write(utf8(path).c_str(), duplicate, flags, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_FALSE(std::filesystem::exists(path));
}
