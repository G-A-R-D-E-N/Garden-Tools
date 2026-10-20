#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_patch.h"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kOlder = "Older.esp";
constexpr const char* kMiddle = "Middle.esp";
constexpr const char* kNewer = "Newer.esp";

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

struct MiscRecord {
  std::uint32_t form_id;
  std::string_view editor_id;
  std::uint32_t flags;
  std::uint32_t data;
};

std::vector<std::byte> misc_plugin(const std::vector<std::string>& masters,
                                   const std::vector<MiscRecord>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::make_tes4_plugin(0, 1.0f, masters);
  out.raw(std::span<const std::byte>{tes4});
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  for (const auto& record : records) {
    const auto header = fo4::test::begin_record(out, "MISC", record.flags, record.form_id);
    fo4::test::write_subrecord(out, "EDID", record.editor_id);
    fo4::test::ByteWriter body;
    body.u32(record.data);
    fo4::test::write_subrecord(out, "DATA", body.bytes);
    fo4::test::end_record(out, header);
  }
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> fixture(std::uint32_t differing_data, bool extra_record) {
  std::vector<MiscRecord> records{{0x800, std::string_view{"Same\0", 5}, 0, 2},
                                  {0x801, std::string_view{"Differs\0", 8}, 0, differing_data}};
  if (extra_record) records.push_back({0x802, std::string_view{"Extra\0", 6}, 0, 4});
  return misc_plugin({}, records);
}

struct ListEntry {
  std::uint16_t level;
  std::uint32_t item;
  std::uint16_t count;
};

std::vector<std::byte> leveled_plugin(const std::vector<std::string>& masters,
                                      const std::vector<ListEntry>& entries) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::make_tes4_plugin(0, 1.0f, masters);
  out.raw(std::span<const std::byte>{tes4});
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  const auto record = fo4::test::begin_record(out, "LVLI", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"List\0", 5});
  fo4::test::write_subrecord(out, "LVLD", std::string_view{"\0", 1});
  for (const auto& entry : entries) {
    fo4::test::ByteWriter body;
    body.u16(entry.level);
    body.u16(0);
    body.u32(entry.item);
    body.u16(entry.count);
    body.u8(0);
    body.u8(0);
    fo4::test::write_subrecord(out, "LVLO", body.bytes);
  }
  fo4::test::ByteWriter count;
  count.u8(static_cast<std::uint8_t>(entries.size()));
  fo4::test::write_subrecord(out, "LLCT", count.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

class DeltaPatchCApi : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_delta_patch_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    older_ = root_ / kOlder;
    middle_ = root_ / kMiddle;
    newer_ = root_ / kNewer;
    output_ = root_ / "Patch.esp";
    write_bytes(older_, fixture(2, false));
    write_bytes(newer_, fixture(3, true));
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  EnvPtr open_env() {
    const auto older_text = older_.string();
    const auto newer_text = newer_.string();
    const char* paths[] = {older_text.c_str(), newer_text.c_str()};
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK);
    return EnvPtr(env, radicle_env_destroy);
  }

  EnvPtr open_env_with_middle() {
    const auto older_text = older_.string();
    const auto middle_text = middle_.string();
    const auto newer_text = newer_.string();
    const char* paths[] = {older_text.c_str(), middle_text.c_str(), newer_text.c_str()};
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths, 3, &env), RADICLE_STATUS_OK);
    return EnvPtr(env, radicle_env_destroy);
  }

  EnvPtr open_output() {
    const auto output_text = output_.string();
    const char* paths[] = {output_text.c_str()};
    radicle_env* env = nullptr;
    EXPECT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
    return EnvPtr(env, radicle_env_destroy);
  }

  const radicle_record* by_editor_id(const radicle_env* env, const char* editor_id) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_find_editor_id(env, editor_id, "MISC", &record), RADICLE_STATUS_OK);
    return record;
  }

  std::filesystem::path root_;
  std::filesystem::path older_;
  std::filesystem::path middle_;
  std::filesystem::path newer_;
  std::filesystem::path output_;
};

TEST_F(DeltaPatchCApi, BuildDropsRecordsIdenticalToTheOlderModule) {
  const auto env = open_env();
  radicle_delta_patch_counts counts{};
  const auto output_text = output_.string();
  ASSERT_EQ(radicle_env_build_delta_patch(env.get(), kOlder, kNewer, output_text.c_str(), &counts),
            RADICLE_STATUS_OK);
  EXPECT_EQ(counts.removed, 1u);
  EXPECT_EQ(counts.kept, 2u);

  const auto patch = open_output();
  const radicle_record* dropped = nullptr;
  EXPECT_EQ(radicle_env_find_editor_id(patch.get(), "Same", "MISC", &dropped),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(by_editor_id(patch.get(), "Differs"), nullptr);
  EXPECT_NE(by_editor_id(patch.get(), "Extra"), nullptr);
}

TEST_F(DeltaPatchCApi, PatchIsWrittenOnlyByASuccessfulBuild) {
  const auto env = open_env();
  radicle_delta_patch_counts counts{};
  const auto output_text = output_.string();
  EXPECT_EQ(radicle_env_build_delta_patch(env.get(), "Missing.esp", kNewer, output_text.c_str(),
                                          &counts),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_build_delta_patch(env.get(), kOlder, "Missing.esp", output_text.c_str(),
                                          &counts),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_build_delta_patch(env.get(), kNewer, kNewer, output_text.c_str(), &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_delta_patch(nullptr, kOlder, kNewer, output_text.c_str(), &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_delta_patch(env.get(), kOlder, kNewer, output_text.c_str(), nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(counts.kept, 0u);
  EXPECT_EQ(counts.removed, 0u);
  EXPECT_FALSE(std::filesystem::exists(output_));
}

TEST_F(DeltaPatchCApi, RecordOnlyInTheOlderPluginIsNotCopiedIntoThePatch) {
  write_bytes(older_, misc_plugin({}, {{0x800, std::string_view{"Shared\0", 7}, 0, 2},
                                       {0x803, std::string_view{"OlderOnly\0", 10}, 0, 9}}));
  write_bytes(newer_, misc_plugin({}, {{0x800, std::string_view{"Shared\0", 7}, 0, 2}}));

  const auto env = open_env();
  radicle_delta_patch_counts counts{};
  const auto output_text = output_.string();
  ASSERT_EQ(radicle_env_build_delta_patch(env.get(), kOlder, kNewer, output_text.c_str(), &counts),
            RADICLE_STATUS_OK);
  EXPECT_EQ(counts.removed, 1u);
  EXPECT_EQ(counts.kept, 0u);

  const auto patch = open_output();
  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_find_editor_id(patch.get(), "OlderOnly", "MISC", &record),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_find_editor_id(patch.get(), "Shared", "MISC", &record),
            RADICLE_STATUS_NOT_FOUND);
}

TEST_F(DeltaPatchCApi, SameLocalIdUnderADifferentMasterListIsStillDropped) {
  write_bytes(older_, misc_plugin({}, {{0x800, std::string_view{"Same\0", 5}, 0, 2}}));
  write_bytes(newer_, misc_plugin({"Fallout4.esm"},
                                  {{0x01000800, std::string_view{"Same\0", 5}, 0, 2}}));

  const auto env = open_env();
  radicle_delta_patch_counts counts{};
  const auto output_text = output_.string();
  ASSERT_EQ(radicle_env_build_delta_patch(env.get(), kOlder, kNewer, output_text.c_str(), &counts),
            RADICLE_STATUS_OK);
  EXPECT_EQ(counts.removed, 1u);
  EXPECT_EQ(counts.kept, 0u);

  const auto patch = open_output();
  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_find_editor_id(patch.get(), "Same", "MISC", &record),
            RADICLE_STATUS_NOT_FOUND);
}

TEST_F(DeltaPatchCApi, DeletedFlagDifferenceKeepsTheRecord) {
  write_bytes(older_, misc_plugin({}, {{0x800, std::string_view{"Gone\0", 5}, 0, 2}}));
  write_bytes(newer_, misc_plugin({}, {{0x800, std::string_view{"Gone\0", 5}, 0x20, 2}}));

  const auto env = open_env();
  radicle_delta_patch_counts counts{};
  const auto output_text = output_.string();
  ASSERT_EQ(radicle_env_build_delta_patch(env.get(), kOlder, kNewer, output_text.c_str(), &counts),
            RADICLE_STATUS_OK);
  EXPECT_EQ(counts.removed, 0u);
  EXPECT_EQ(counts.kept, 1u);

  const auto patch = open_output();
  const radicle_record* kept = by_editor_id(patch.get(), "Gone");
  ASSERT_NE(kept, nullptr);
  radicle_record_info info{};
  ASSERT_EQ(radicle_record_get_info(patch.get(), kept, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.deleted, 1);
  EXPECT_EQ(info.record_flags, 0x20u);
}

TEST_F(DeltaPatchCApi, ListMergeWritesAPatchAndCountsTheMergedLists) {
  write_bytes(older_, leveled_plugin({}, {{1, 0x000800, 1}, {2, 0x000801, 1}}));
  write_bytes(middle_,
              leveled_plugin({"Older.esp"}, {{1, 0x000800, 1}, {2, 0x000801, 1}, {4, 0x01000804, 1}}));
  write_bytes(newer_, leveled_plugin({"Older.esp"}, {{1, 0x000800, 1}, {3, 0x01000803, 2}}));

  const auto env = open_env_with_middle();
  radicle_list_merge_counts counts{};
  const auto output_text = output_.string();
  ASSERT_EQ(radicle_env_build_list_merge_patch(env.get(), "LVLI", "Merged.esp", output_text.c_str(),
                                               &counts),
            RADICLE_STATUS_OK);
  EXPECT_EQ(counts.merged, 1u);
  EXPECT_EQ(counts.unchanged, 0u);

  const auto patch = open_output();
  const radicle_record* record = nullptr;
  EXPECT_EQ(radicle_env_find_editor_id(patch.get(), "List", "LVLI", &record), RADICLE_STATUS_OK);
  EXPECT_NE(record, nullptr);
}

TEST_F(DeltaPatchCApi, ListMergeRefusesBadArgumentsAndZeroesTheCounts) {
  write_bytes(older_, leveled_plugin({}, {{1, 0x000800, 1}}));
  write_bytes(newer_, leveled_plugin({"Older.esp"}, {{1, 0x000800, 1}}));

  const auto env = open_env();
  radicle_list_merge_counts counts{7, 7};
  const auto output_text = output_.string();
  EXPECT_EQ(radicle_env_build_list_merge_patch(nullptr, "LVLI", "Merged.esp", output_text.c_str(),
                                               &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_list_merge_patch(env.get(), "LVLI", nullptr, output_text.c_str(),
                                               &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_list_merge_patch(env.get(), "LVLI", "Merged.esp", nullptr, &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_list_merge_patch(env.get(), "LVLI", "Merged.esp", output_text.c_str(),
                                               nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_list_merge_patch(env.get(), nullptr, "Merged.esp",
                                               output_text.c_str(), &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_list_merge_patch(env.get(), "LVL", "Merged.esp", output_text.c_str(),
                                               &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_build_list_merge_patch(env.get(), "MISC", "Merged.esp", output_text.c_str(),
                                               &counts),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(counts.merged, 0u);
  EXPECT_EQ(counts.unchanged, 0u);
  EXPECT_FALSE(std::filesystem::exists(output_));
}

}  // namespace
