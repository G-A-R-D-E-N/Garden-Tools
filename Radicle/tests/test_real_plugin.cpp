#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

namespace {

std::filesystem::path testdata(const char* name) {
  return std::filesystem::path{FO4_TESTDATA_DIR} / name;
}

}  // namespace

TEST(RealPlugin, ReadsHeatherSurvivalPatch) {
  const auto path = testdata("FW_HeatherSurvivalPatch.esp");
  ASSERT_TRUE(std::filesystem::exists(path)) << path;
  const auto plugin = fo4::read_plugin_file(path);

  EXPECT_EQ(plugin.tes4.header.signature, fo4::sig_tes4);
  EXPECT_EQ(plugin.tes4.header.form_version, 131);
  EXPECT_FLOAT_EQ(plugin.header.version, 1.0f);
  EXPECT_EQ(plugin.header.record_count, 7u);
  EXPECT_EQ(plugin.header.next_form_id, 0x802u);
  EXPECT_EQ(plugin.header.author, "NomadsReach");
  ASSERT_EQ(plugin.header.masters.size(), 3u);
  EXPECT_EQ(plugin.header.masters[0].key, fo4::PluginKey::from_filename("Fallout4.esm"));
  EXPECT_EQ(plugin.header.masters[1].key,
            fo4::PluginKey::from_filename("llamaCompanionHeatherv2.esp"));
  EXPECT_EQ(plugin.header.masters[2].key,
            fo4::PluginKey::from_filename("Survival Mode Overhaul.esp"));

  ASSERT_EQ(plugin.groups.size(), 3u);
  EXPECT_EQ(plugin.groups[0].header.top_label().to_string(), "MGEF");
  EXPECT_EQ(plugin.groups[1].header.top_label().to_string(), "SPEL");
  EXPECT_EQ(plugin.groups[2].header.top_label().to_string(), "ALCH");

  const auto records = fo4::collect_records(plugin);
  // HEDR's 7 counts 3 top groups + 4 major records. TES4 is extra in our walk.
  EXPECT_EQ(records.size(), 5u);
  const auto* mgef = fo4::find_record(plugin, fo4::RawFormId{0x03000800});
  ASSERT_NE(mgef, nullptr);
  EXPECT_EQ(mgef->header.signature.to_string(), "MGEF");
  EXPECT_EQ(plugin.original_bytes.size(), std::filesystem::file_size(path));
}

TEST(RealPlugin, ReadsSlowTime) {
  const auto path = testdata("SlowTime.esp");
  ASSERT_TRUE(std::filesystem::exists(path)) << path;
  const auto plugin = fo4::read_plugin_file(path);
  EXPECT_EQ(plugin.tes4.header.signature, fo4::sig_tes4);
  EXPECT_FALSE(plugin.groups.empty());
  EXPECT_GT(fo4::collect_records(plugin).size(), 1u);
}

TEST(RealPlugin, HeatherRemapSameMastersPreservesFormKeys) {
  auto plugin = fo4::read_plugin_file(testdata("FW_HeatherSurvivalPatch.esp"));
  const auto before = fo4::collect_form_links(plugin);
  ASSERT_FALSE(before.empty());
  std::vector<fo4::PluginKey> masters;
  masters.reserve(plugin.header.masters.size());
  for (const auto& master : plugin.header.masters) {
    masters.push_back(master.key);
  }
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(masters));
  const auto after = fo4::collect_form_links(plugin);
  ASSERT_EQ(before.size(), after.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after[i].key, before[i].key);
    EXPECT_EQ(after[i].subrecord, before[i].subrecord);
    EXPECT_EQ(after[i].offset, before[i].offset);
  }
}

TEST(RealPlugin, SlowTimeRemapSameMastersPreservesFormKeys) {
  auto plugin = fo4::read_plugin_file(testdata("SlowTime.esp"));
  const auto before = fo4::collect_form_links(plugin);
  ASSERT_FALSE(before.empty());
  std::vector<fo4::PluginKey> masters;
  masters.reserve(plugin.header.masters.size());
  for (const auto& master : plugin.header.masters) {
    masters.push_back(master.key);
  }
  plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(masters));
  const auto after = fo4::collect_form_links(plugin);
  ASSERT_EQ(before.size(), after.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after[i].key, before[i].key);
    EXPECT_EQ(after[i].subrecord, before[i].subrecord);
    EXPECT_EQ(after[i].offset, before[i].offset);
  }
}
