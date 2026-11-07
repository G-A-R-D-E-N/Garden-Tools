#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(std::uint32_t flags,
                                   float version = 1.0f,
                                   bool has_master = false,
                                   std::uint32_t next_form_id = 0x800) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(version);
  hedr.u32(0);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (has_master) {
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Base.esm\0", 9});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> make_plugin_with_owned_records(const std::vector<std::uint32_t>& form_ids,
                                                      float version,
                                                      bool has_master,
                                                      std::uint32_t next_form_id) {
  auto bytes = make_plugin(0, version, has_master, next_form_id);
  fo4::test::ByteWriter group;
  const auto group_header = fo4::test::begin_group(group, "KYWD", 0);
  for (const auto form_id : form_ids) {
    const auto record = fo4::test::begin_record(group, "KYWD", 0, form_id);
    fo4::test::write_subrecord(group, "EDID", std::string_view{"OwnedRecord\0", 12});
    fo4::test::end_record(group, record);
  }
  fo4::test::end_group(group, group_header);
  bytes.insert(bytes.end(), group.bytes.begin(), group.bytes.end());
  return bytes;
}

std::vector<std::byte> make_plugin_with_owned_record(std::uint32_t form_id,
                                                     float version = 1.0f,
                                                     bool has_master = false) {
  return make_plugin_with_owned_records({form_id}, version, has_master, 0x800);
}

struct TempDir {
  TempDir() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("radicle_plugin_flags_" + std::to_string(id));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

}  // namespace

TEST(RecordFlags, ModelsPersistentBitAt0400) {
  const fo4::RecordFlags persistent{0x00000400u};
  EXPECT_TRUE(persistent.is_persistent());
  EXPECT_TRUE(persistent.test(fo4::RecordFlags::persistent));

  const fo4::RecordFlags clear{0x00000800u};
  EXPECT_FALSE(clear.is_persistent());
}

TEST(PluginFlags, SetsAndClearsSmallFileFlagThroughSerialization) {
  auto plugin = fo4::read_plugin(make_plugin(0), "Flagged.esp");
  ASSERT_FALSE(plugin.header.tes4_flags.is_small_file());

  plugin = fo4::set_plugin_light_flag(std::move(plugin), true);
  EXPECT_TRUE(plugin.header.tes4_flags.is_small_file());
  EXPECT_TRUE(plugin.tes4.header.flags.is_small_file());

  auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Flagged.esp");
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());
  EXPECT_TRUE(reloaded.tes4.header.flags.is_small_file());

  reloaded = fo4::set_plugin_light_flag(std::move(reloaded), false);
  EXPECT_FALSE(reloaded.header.tes4_flags.is_small_file());
  EXPECT_FALSE(reloaded.tes4.header.flags.is_small_file());

  const auto cleared = fo4::read_plugin(fo4::write_plugin(reloaded), "Flagged.esp");
  EXPECT_FALSE(cleared.header.tes4_flags.is_small_file());
  EXPECT_FALSE(cleared.tes4.header.flags.is_small_file());
}

TEST(PluginFlags, RejectsLightFlagWhenOwnedLocalIdExceedsTwelveBits) {
  auto plugin = fo4::read_plugin(make_plugin_with_owned_record(0x00001234), "TooLarge.esp");
  ASSERT_EQ(plugin.groups.size(), 1u);

  EXPECT_THROW(
      static_cast<void>(fo4::set_plugin_light_flag(std::move(plugin), true)),
      fo4::FormIdError);
}

TEST(PluginFlags, RejectsLightFlagWhenMasterlessLocalIdIsBelowTheFullFloor) {
  auto plugin = fo4::read_plugin(make_plugin_with_owned_record(0x00000005u), "Low.esp");

  EXPECT_THROW(
      static_cast<void>(fo4::set_plugin_light_flag(plugin, true)),
      fo4::FormIdError);
  EXPECT_NO_THROW(static_cast<void>(fo4::set_plugin_light_flag(plugin, false)));
}

TEST(PluginFlags, RejectsLightFlagBelowTheFloorWhenHedrIsBelowOne) {
  auto plugin = fo4::read_plugin(
      make_plugin_with_owned_record(0x01000005u, 0.95f, true), "OldHedr.esp");

  EXPECT_THROW(
      static_cast<void>(fo4::set_plugin_light_flag(plugin, true)),
      fo4::FormIdError);
}

TEST(PluginFlags, AllowsLightFlagForLowLocalIdsWithHedrOneAndAMaster) {
  auto plugin = fo4::set_plugin_light_flag(
      fo4::read_plugin(make_plugin_with_owned_record(0x01000005u, 1.0f, true), "Hardcoded.esp"),
      true);

  EXPECT_TRUE(plugin.header.tes4_flags.is_small_file());
}

TEST(PluginFlags, AllowsLightFlagForMasterlessIdsAtTheFullFloor) {
  auto plugin = fo4::set_plugin_light_flag(
      fo4::read_plugin(make_plugin_with_owned_record(0x00000800u), "Floor.esp"), true);

  EXPECT_TRUE(plugin.header.tes4_flags.is_small_file());
}

TEST(PluginFlags, LightFlagLowersHedrNextToOnePastTheHighestOwnId) {
  auto plugin = fo4::set_plugin_light_flag(
      fo4::read_plugin(make_plugin_with_owned_records({0x01000800u, 0x01000801u}, 1.0f, true, 0x5000u),
                       "Lowered.esp"),
      true);

  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Lowered.esp");
  EXPECT_EQ(reloaded.header.next_form_id, 0x802u);
}

TEST(PluginFlags, LightFlagKeepsHedrNextThatIsAlreadyInRange) {
  auto plugin = fo4::set_plugin_light_flag(
      fo4::read_plugin(make_plugin_with_owned_records({0x01000800u, 0x01000801u}, 1.0f, true, 0x900u),
                       "InRange.esp"),
      true);

  EXPECT_EQ(plugin.header.next_form_id, 0x900u);
}

TEST(PluginFlags, LightFlagLowersMasterlessHedrNextToTheFullFloor) {
  auto plugin = fo4::set_plugin_light_flag(
      fo4::read_plugin(make_plugin(0, 1.0f, false, 0x5000u), "Empty.esp"), true);

  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
}

TEST(PluginFlags, ClearingLightFlagNeverChangesHedrNext) {
  auto plugin = fo4::set_plugin_light_flag(
      fo4::read_plugin(make_plugin(fo4::RecordFlags::small_file, 1.0f, true, 0x5000u), "Cleared.esp"),
      false);

  EXPECT_FALSE(plugin.header.tes4_flags.is_small_file());
  EXPECT_EQ(plugin.header.next_form_id, 0x5000u);
}

TEST(PluginFlags, WriteFileToEslSetsMasterAndLightFlags) {
  const TempDir dir;
  auto plugin = fo4::read_plugin(make_plugin_with_owned_record(0x00000800u), "Save.esp");
  const auto target = dir.path / "Save.esl";

  fo4::write_plugin_file(plugin, target);

  const auto reloaded = fo4::read_plugin_file(target);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_master());
  EXPECT_TRUE(reloaded.header.tes4_flags.is_small_file());
}

TEST(PluginFlags, WriteFileToEsmSetsMasterFlagOnly) {
  const TempDir dir;
  auto plugin = fo4::read_plugin(make_plugin_with_owned_record(0x00000800u), "Save.esp");
  const auto target = dir.path / "Save.esm";

  fo4::write_plugin_file(plugin, target);

  const auto reloaded = fo4::read_plugin_file(target);
  EXPECT_TRUE(reloaded.header.tes4_flags.is_master());
  EXPECT_FALSE(reloaded.header.tes4_flags.is_small_file());
}

TEST(PluginFlags, WriteFileToEspKeepsFlags) {
  const TempDir dir;
  const auto master = fo4::read_plugin(make_plugin(fo4::RecordFlags::master), "Master.esp");
  const auto plain = fo4::read_plugin(make_plugin(0), "Plain.esp");

  fo4::write_plugin_file(master, dir.path / "Master.esp");
  fo4::write_plugin_file(plain, dir.path / "Plain.esp");

  EXPECT_TRUE(fo4::read_plugin_file(dir.path / "Master.esp").header.tes4_flags.is_master());
  EXPECT_FALSE(fo4::read_plugin_file(dir.path / "Plain.esp").header.tes4_flags.is_master());
}

TEST(PluginFlags, WriteFileToEslRefusesOutOfRangeFormIds) {
  const TempDir dir;
  auto plugin = fo4::read_plugin(make_plugin_with_owned_record(0x00001000u), "TooLarge.esp");
  const auto target = dir.path / "TooLarge.esl";

  EXPECT_THROW(fo4::write_plugin_file(plugin, target), fo4::FormIdError);
  EXPECT_FALSE(std::filesystem::exists(target));
}
