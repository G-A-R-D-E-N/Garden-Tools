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
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr const char* kOlder = "Older.esp";
constexpr const char* kNewer = "Newer.esp";

using EnvPtr = std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)>;

void write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream stream(path, std::ios::binary);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

void write_misc(fo4::test::ByteWriter& out, std::uint32_t form_id, const char* editor_id,
                std::size_t editor_id_size, std::uint32_t data) {
  const auto record = fo4::test::begin_record(out, "MISC", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view(editor_id, editor_id_size));
  fo4::test::ByteWriter body;
  body.u32(data);
  fo4::test::write_subrecord(out, "DATA", body.bytes);
  fo4::test::end_record(out, record);
}

std::vector<std::byte> fixture(std::uint32_t differing_data, bool extra_record) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "MISC", 0);
  write_misc(out, 0x800, "Same\0", 5, 2);
  write_misc(out, 0x801, "Differs\0", 8, differing_data);
  if (extra_record) write_misc(out, 0x802, "Extra\0", 6, 4);
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
  EXPECT_EQ(counts.kept, 3u);

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

}  // namespace
