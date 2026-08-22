#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(std::uint32_t flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

std::vector<std::byte> make_plugin_with_owned_record(std::uint32_t form_id) {
  auto bytes = make_plugin(0);
  fo4::test::ByteWriter group;
  const auto group_header = fo4::test::begin_group(group, "KYWD", 0);
  const auto record = fo4::test::begin_record(group, "KYWD", 0, form_id);
  fo4::test::write_subrecord(group, "EDID", std::string_view{"OwnedRecord\0", 12});
  fo4::test::end_record(group, record);
  fo4::test::end_group(group, group_header);
  bytes.insert(bytes.end(), group.bytes.begin(), group.bytes.end());
  return bytes;
}

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
