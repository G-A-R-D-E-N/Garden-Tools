#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <span>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> copy_bytes(std::span<const std::byte> bytes) {
  return {bytes.begin(), bytes.end()};
}

}  // namespace

TEST(OriginalStorage, CallerInputCanDieAfterReadPluginReturns) {
  std::vector<std::byte> expected;
  fo4::Plugin plugin;
  {
    auto input = fo4::test::make_minimal_plugin();
    expected = input;
    plugin = fo4::read_plugin(input, "Lifetime.esp");
  }

  EXPECT_EQ(copy_bytes(fo4::plugin_original_bytes(plugin)), expected);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{0x01000800});
  ASSERT_NE(record, nullptr);
  EXPECT_FALSE(fo4::record_original_bytes(plugin, *record).empty());
  EXPECT_EQ(fo4::write_plugin(plugin), expected);
}

TEST(OriginalStorage, CopyAndMoveKeepSharedOriginalStorageAlive) {
  const auto expected = fo4::test::make_minimal_plugin();
  auto source = fo4::read_plugin(expected, "Copy.esp");
  auto copy = source;
  source = fo4::Plugin{};
  auto moved = std::move(copy);

  EXPECT_EQ(fo4::write_plugin(moved), expected);
}

TEST(OriginalStorage, DirtyRecordDoesNotReuseItsOriginalRange) {
  const auto input = fo4::test::make_minimal_plugin();
  auto plugin = fo4::read_plugin(input, "Dirty.esp");
  auto* record = std::get_if<fo4::Record>(&plugin.groups[0].children[0]);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 1u);
  record->subrecords[0].data[0] = std::byte{'X'};
  record->original_range.clear();
  plugin.original_range.clear();

  const auto written = fo4::write_plugin(plugin);
  EXPECT_NE(written, input);
  const auto reread = fo4::read_plugin(written, "Dirty.esp");
  const auto* reread_record = fo4::find_record(reread, fo4::RawFormId{0x01000800});
  ASSERT_NE(reread_record, nullptr);
  ASSERT_EQ(reread_record->subrecords.size(), 1u);
  EXPECT_EQ(reread_record->subrecords[0].data[0], std::byte{'X'});
}
