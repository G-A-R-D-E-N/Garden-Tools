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

}  // namespace

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
