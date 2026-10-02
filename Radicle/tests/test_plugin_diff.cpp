#include "fo4/graph/plugin_diff.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace {

struct MiscSpec {
  std::uint32_t form_id;
  std::string_view editor_id;
  std::uint32_t flags{0};
};

fo4::Plugin make_plugin(std::initializer_list<MiscSpec> records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  for (const auto& spec : records) {
    const auto record = fo4::test::begin_record(out, "MISC", spec.flags, spec.form_id);
    fo4::test::write_subrecord(out, "EDID", spec.editor_id);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Mod.esp");
}

}  // namespace

TEST(PluginDiff, ReportsAddedRemovedAndChangedRecords) {
  const auto before = make_plugin({{0x800, std::string_view{"Same\0", 5}},
                                   {0x801, std::string_view{"Old\0", 4}},
                                   {0x802, std::string_view{"Gone\0", 5}}});
  const auto after = make_plugin({{0x800, std::string_view{"Same\0", 5}},
                                  {0x801, std::string_view{"New\0", 4}},
                                  {0x803, std::string_view{"Added\0", 6}}});
  const auto diff = fo4::diff_plugins(before, after);
  ASSERT_EQ(diff.size(), 3u);

  EXPECT_EQ(diff[0].key, fo4::FormKey::parse("000801:Mod.esp"));
  EXPECT_EQ(diff[0].change, fo4::RecordChange::changed);
  EXPECT_FALSE(diff[0].header_changed);
  ASSERT_EQ(diff[0].changed_subrecords.size(), 1u);
  EXPECT_EQ(diff[0].changed_subrecords[0].to_string(), "EDID");

  EXPECT_EQ(diff[1].key, fo4::FormKey::parse("000803:Mod.esp"));
  EXPECT_EQ(diff[1].change, fo4::RecordChange::added);

  EXPECT_EQ(diff[2].key, fo4::FormKey::parse("000802:Mod.esp"));
  EXPECT_EQ(diff[2].change, fo4::RecordChange::removed);
}

TEST(PluginDiff, FlagChangeIsAHeaderChange) {
  const auto before = make_plugin({{0x800, std::string_view{"Same\0", 5}}});
  const auto after = make_plugin({{0x800, std::string_view{"Same\0", 5}, fo4::RecordFlags::deleted}});
  const auto diff = fo4::diff_plugins(before, after);
  ASSERT_EQ(diff.size(), 1u);
  EXPECT_TRUE(diff[0].header_changed);
  EXPECT_TRUE(diff[0].changed_subrecords.empty());
}

TEST(PluginDiff, IdenticalPluginsHaveNoDiff) {
  const auto plugin = make_plugin({{0x800, std::string_view{"Same\0", 5}}});
  EXPECT_TRUE(fo4::diff_plugins(plugin, plugin).empty());
}
