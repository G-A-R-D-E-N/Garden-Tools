#include "fo4/c_api_conflict.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
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

std::vector<std::byte> plugin_with_list(std::string_view count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  const auto record = fo4::test::begin_record(out, "LVLI", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"List\0", 5});
  fo4::test::write_subrecord(out, "LLCT", count);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_form_list(std::string_view name, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "FLST", 0);
  const auto record = fo4::test::begin_record(out, "FLST", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", name);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_placed_location(bool added, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Placed\0", 7});
  if (added) fo4::test::write_subrecord(out, "XLRL", std::string_view{"\0\0\0\0", 4});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_story_condition_count(std::uint32_t count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "SMBN", 0);
  const auto record = fo4::test::begin_record(out, "SMBN", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Branch\0", 7});
  fo4::test::ByteWriter data;
  data.u32(count);
  fo4::test::write_subrecord(out, "CITC", data.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_misc_keyword_count(std::uint32_t count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Thing\0", 6});
  fo4::test::ByteWriter data;
  data.u32(count);
  fo4::test::write_subrecord(out, "KSIZ", data.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_idle_count(std::string_view count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "IDLM", 0);
  const auto record = fo4::test::begin_record(out, "IDLM", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Idle\0", 5});
  fo4::test::write_subrecord(out, "IDLC", count);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_template_count(std::uint32_t count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "ARMO", 0);
  const auto record = fo4::test::begin_record(out, "ARMO", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Armor\0", 6});
  fo4::test::ByteWriter data;
  data.u32(count);
  fo4::test::write_subrecord(out, "OBTE", data.bytes);
  fo4::test::write_subrecord(out, "STOP", std::string_view{});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_actor_sound_count(std::uint32_t count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Actor\0", 6});
  fo4::test::ByteWriter data;
  data.u32(count);
  fo4::test::write_subrecord(out, "CS2H", data.bytes);
  fo4::test::write_subrecord(out, "CS2E", std::string_view{});
  fo4::test::write_subrecord(out, "CS2F", std::string_view{"\0", 1});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> plugin_with_quest_alias_count(std::uint32_t count, bool with_master) {
  fo4::test::ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f,
      with_master ? std::vector<std::string>{"Base.esm"} : std::vector<std::string>{});
  const auto group = fo4::test::begin_group(out, "QUST", 0);
  const auto record = fo4::test::begin_record(out, "QUST", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Quest\0", 6});
  fo4::test::ByteWriter next, id, flags, size;
  next.u32(2); id.u32(1); flags.u32(0); size.u32(count);
  fo4::test::write_subrecord(out, "ANAM", next.bytes);
  fo4::test::write_subrecord(out, "ALST", id.bytes);
  fo4::test::write_subrecord(out, "ALID", std::string_view{"Alias\0", 6});
  fo4::test::write_subrecord(out, "FNAM", flags.bytes);
  fo4::test::write_subrecord(out, "KSIZ", size.bytes);
  fo4::test::write_subrecord(out, "ALED", std::string_view{});
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

TEST(CApiConflict, LeveledCountDefaultIsBenignOnRecordAndSubrecordRows) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_count_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_list(std::string_view{"\1", 1}, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_list(std::string_view{"\2", 1}, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_list(std::string_view{"\3", 1}, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tLLCT\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, FormListEditorIdDefaultPropagatesToRecord) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_flst_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_form_list(std::string_view{"ListA\0", 6}, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_form_list(std::string_view{"ListB\0", 6}, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_form_list(std::string_view{"ListC\0", 6}, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tEDID\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, AddedPlacedLocationReferenceUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_xlrl_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_placed_location(false, false));
  const auto patch = write_file(root / "Patch.esp", plugin_with_placed_location(true, true));
  const char* paths[] = {base.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,identical_to_master"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tXLRL\tbenign\tunknown,identical_to_master"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, StoryConditionCountUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_citc_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_story_condition_count(1, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_story_condition_count(2, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_story_condition_count(3, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tCITC\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, RootKeywordCountUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_ksiz_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_misc_keyword_count(1, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_misc_keyword_count(2, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_misc_keyword_count(3, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tKSIZ\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, RootIdleCountUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_idlc_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_idle_count(std::string_view{"\x01", 1}, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_idle_count(std::string_view{"\x02", 1}, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_idle_count(std::string_view{"\x03", 1}, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tIDLC\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, RootObjectTemplateCountUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_obte_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_template_count(1, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_template_count(2, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_template_count(3, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tOBTE\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, NpcActorSoundCountUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_cs2h_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_actor_sound_count(1, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_actor_sound_count(2, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_actor_sound_count(3, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tCS2H\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST(CApiConflict, StableReferenceAliasCountUsesBenignPriority) {
  const auto root = std::filesystem::temp_directory_path() /
                    ("radicle_c_conflict_alias_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto base = write_file(root / "Base.esm", plugin_with_quest_alias_count(1, false));
  const auto middle = write_file(root / "Middle.esp", plugin_with_quest_alias_count(2, true));
  const auto patch = write_file(root / "Patch.esp", plugin_with_quest_alias_count(3, true));
  const char* paths[] = {base.c_str(), middle.c_str(), patch.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_classify_conflicts(env, &rows, &count), RADICLE_STATUS_OK);
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\trecord\t\tbenign\tmaster,benign,benign"));
  EXPECT_TRUE(has_row(rows, count, "000800:Base.esm\tsubrecord\tKSIZ\tbenign\tmaster,benign,benign"));
  radicle_free_string_array(rows, count);
  radicle_env_destroy(env);
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

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
