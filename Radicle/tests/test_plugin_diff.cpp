#include "fo4/graph/plugin_diff.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
  auto plugin = make_plugin({{0x800, std::string_view{"Same\0", 5}}});
  EXPECT_TRUE(fo4::diff_plugins(plugin, plugin).empty());
}

namespace {

fo4::Plugin make_ordered(std::initializer_list<std::pair<std::string_view, std::string_view>> subrecords) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800);
  for (const auto& [signature, data] : subrecords) fo4::test::write_subrecord(out, signature, data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Mod.esp");
}

}  // namespace

TEST(PluginDiff, ReportsSubrecordsReorderedAcrossSignatures) {
  const auto before = make_ordered({{"EDID", std::string_view{"A\0", 2}}, {"MODL", std::string_view{"m\0", 2}},
                                    {"OBND", std::string_view{"012345678901", 12}}});
  const auto after = make_ordered({{"EDID", std::string_view{"A\0", 2}}, {"OBND", std::string_view{"012345678901", 12}},
                                   {"MODL", std::string_view{"m\0", 2}}});
  const auto diff = fo4::diff_plugins(before, after);
  std::vector<std::string> changed;
  for (const auto& item : diff) {
    if (item.signature.to_string() != "MISC") continue;
    for (const auto signature : item.changed_subrecords) changed.push_back(signature.to_string());
  }
  EXPECT_EQ(changed, (std::vector<std::string>{"MODL", "OBND"}));
}

TEST(PluginDiff, SingleChangeKeepsPreciseSignature) {
  const auto before = make_ordered({{"EDID", std::string_view{"A\0", 2}}, {"MODL", std::string_view{"m\0", 2}},
                                    {"OBND", std::string_view{"012345678901", 12}}});
  const auto after = make_ordered({{"EDID", std::string_view{"B\0", 2}}, {"MODL", std::string_view{"m\0", 2}},
                                   {"OBND", std::string_view{"012345678901", 12}}});
  const auto diff = fo4::diff_plugins(before, after);
  ASSERT_EQ(diff.size(), 1u);
  ASSERT_EQ(diff[0].changed_subrecords.size(), 1u);
  EXPECT_EQ(diff[0].changed_subrecords[0].to_string(), "EDID");
}

TEST(PluginDiff, ReferenceMovedBetweenCellsIsChanged) {
  const auto cell_plugin = [](std::uint32_t parent) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(3);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto top = fo4::test::begin_group(out, "CELL", 0);
    for (const std::uint32_t id : {0x800u, 0x802u}) {
      const auto cell = fo4::test::begin_record(out, "CELL", 0, id);
      fo4::test::end_record(out, cell);
      if (id != parent) continue;
      const auto children = fo4::test::begin_group(out, "CELL", 6);
      out.patch_u32(children + 8, id);
      const auto refr = fo4::test::begin_record(out, "REFR", 0, 0x801);
      fo4::test::write_subrecord(out, "EDID", std::string_view{"Same\0", 5});
      fo4::test::end_record(out, refr);
      fo4::test::end_group(out, children);
    }
    fo4::test::end_group(out, top);
    return fo4::read_plugin(out.bytes, "Mod.esp");
  };
  const auto diff = fo4::diff_plugins(cell_plugin(0x800), cell_plugin(0x802));
  ASSERT_EQ(diff.size(), 1u);
  EXPECT_EQ(diff[0].key, fo4::FormKey::parse("000801:Mod.esp"));
  EXPECT_EQ(diff[0].change, fo4::RecordChange::changed);
  EXPECT_TRUE(diff[0].placement_changed);
}

TEST(PluginDiff, DuplicateFormKeyInOnePluginKeepsTheFirstRecord) {
  const auto before = make_plugin({{0x800, std::string_view{"Same\0", 5}},
                                   {0x800, std::string_view{"Same\0", 5}}});
  const auto after = make_plugin({{0x800, std::string_view{"Same\0", 5}},
                                  {0x800, std::string_view{"Later\0", 6}}});
  EXPECT_TRUE(fo4::diff_plugins(before, after).empty());
  EXPECT_TRUE(fo4::diff_plugins(after, before).empty());
}
