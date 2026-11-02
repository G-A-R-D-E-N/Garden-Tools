#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "fo4/schema/form_links.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::filesystem::path testdata(const char* name) {
  return std::filesystem::path{FO4_TESTDATA_DIR} / name;
}

struct SubrecordSnapshot {
  fo4::Signature signature{};
  std::vector<std::byte> data;
  bool extended_size{false};

  friend bool operator==(const SubrecordSnapshot&, const SubrecordSnapshot&) = default;
};

struct RecordPayloadSnapshot {
  fo4::FormKey key;
  fo4::Signature signature{};
  std::vector<SubrecordSnapshot> subrecords;

  friend bool operator==(const RecordPayloadSnapshot&, const RecordPayloadSnapshot&) = default;
};

std::vector<RecordPayloadSnapshot> masked_record_payloads(const fo4::Plugin& plugin) {
  const auto links = fo4::collect_form_links(plugin);
  std::vector<RecordPayloadSnapshot> snapshots;

  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature == fo4::sig_tes4 || record->form_key.owner().empty()) {
      continue;
    }

    RecordPayloadSnapshot snapshot;
    snapshot.key = record->form_key;
    snapshot.signature = record->header.signature;
    snapshot.subrecords.reserve(record->subrecords.size());
    std::map<std::uint32_t, std::size_t> ordinals;

    for (const auto& subrecord : record->subrecords) {
      const auto ordinal = ordinals[subrecord.signature.packed()]++;
      SubrecordSnapshot sub{
          .signature = subrecord.signature,
          .data = subrecord.data,
          .extended_size = subrecord.extended_size,
      };

      // Mask only the exact reported FormLink occurrence and four-byte slot.
      // Every byte in every other repeated subrecord remains required to match.
      for (const auto& link : links) {
        if (link.record != record->form_key || link.subrecord != subrecord.signature ||
            link.subrecord_ordinal != ordinal) {
          continue;
        }
        if (link.offset + 4 > sub.data.size()) continue;
        std::fill_n(sub.data.begin() + static_cast<std::ptrdiff_t>(link.offset),
                    4, std::byte{0});
      }
      snapshot.subrecords.push_back(std::move(sub));
    }
    snapshots.push_back(std::move(snapshot));
  }

  std::ranges::sort(snapshots, {}, [](const RecordPayloadSnapshot& value) {
    return value.key.to_string();
  });
  return snapshots;
}

void expect_same_logical_links(const std::vector<fo4::FormLinkHit>& before,
                               const std::vector<fo4::FormLinkHit>& after,
                               bool* raw_changed = nullptr) {
  ASSERT_EQ(before.size(), after.size());
  bool changed = false;
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(after[i].record, before[i].record);
    EXPECT_EQ(after[i].subrecord, before[i].subrecord);
    EXPECT_EQ(after[i].subrecord_ordinal, before[i].subrecord_ordinal);
    EXPECT_EQ(after[i].offset, before[i].offset);
    EXPECT_EQ(after[i].key, before[i].key);
    changed = changed || after[i].raw != before[i].raw;
  }
  if (raw_changed != nullptr) *raw_changed = changed;
}

}  // namespace

TEST(RealPluginPreservation, UnmodifiedFallout4MasterIsByteIdentical) {
  const char* data_dir = std::getenv("FO4_DATA_DIR");
  if (data_dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(data_dir));

  const auto path = std::filesystem::path{data_dir} / "Fallout4.esm";
  if (!std::filesystem::exists(path)) {
    GTEST_SKIP() << "Fallout4.esm is not present under " << data_dir;
  }

  auto plugin = fo4::read_plugin_file(path);
  const auto original = fo4::plugin_original_bytes(plugin);
  const std::vector<std::byte> expected{original.begin(), original.end()};
  plugin.original_range.clear();
  const auto written = fo4::write_plugin(plugin);
  EXPECT_EQ(written, expected);
}

TEST(RealPlugin, ReadsHeatherSurvivalPatch) {
  const auto path = testdata("FW_HeatherSurvivalPatch.esp");
  ASSERT_TRUE(std::filesystem::exists(path)) << path;
  auto plugin = fo4::read_plugin_file(path);

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
  EXPECT_EQ(fo4::plugin_original_bytes(plugin).size(), std::filesystem::file_size(path));
}

TEST(RealPlugin, SameMastersRemapPreservesFormKeys) {
  for (const char* name : {"FW_HeatherSurvivalPatch.esp", "SlowTime.esp"}) {
    SCOPED_TRACE(name);
    auto plugin = fo4::read_plugin_file(testdata(name));
    const auto before = fo4::collect_form_links(plugin);
    ASSERT_FALSE(before.empty());
    std::vector<fo4::PluginKey> masters;
    masters.reserve(plugin.header.masters.size());
    for (const auto& master : plugin.header.masters) {
      masters.push_back(master.key);
    }
    plugin = fo4::remap_plugin_masters(std::move(plugin), std::move(masters));
    const auto after = fo4::collect_form_links(plugin);
    expect_same_logical_links(before, after);
  }
}

TEST(RealPlugin, HeatherReverseMasterRemapPreservesAllNonFormLinkPayloadBytes) {
  const auto path = testdata("FW_HeatherSurvivalPatch.esp");
  auto plugin = fo4::read_plugin_file(path);
  ASSERT_EQ(plugin.header.masters.size(), 3u);

  const auto before_links = fo4::collect_form_links(plugin);
  ASSERT_FALSE(before_links.empty());
  const auto before_payloads = masked_record_payloads(plugin);
  ASSERT_FALSE(before_payloads.empty());

  std::vector<fo4::PluginKey> reversed_masters;
  reversed_masters.reserve(plugin.header.masters.size());
  for (auto it = plugin.header.masters.rbegin(); it != plugin.header.masters.rend(); ++it) {
    reversed_masters.push_back(it->key);
  }

  plugin = fo4::remap_plugin_masters(std::move(plugin), reversed_masters);
  ASSERT_EQ(plugin.header.masters.size(), 3u);
  EXPECT_EQ(plugin.header.masters[0].key,
            fo4::PluginKey::from_filename("Survival Mode Overhaul.esp"));
  EXPECT_EQ(plugin.header.masters[1].key,
            fo4::PluginKey::from_filename("llamaCompanionHeatherv2.esp"));
  EXPECT_EQ(plugin.header.masters[2].key,
            fo4::PluginKey::from_filename("Fallout4.esm"));

  const auto remapped_links = fo4::collect_form_links(plugin);
  bool raw_changed = false;
  expect_same_logical_links(before_links, remapped_links, &raw_changed);
  EXPECT_TRUE(raw_changed) << "real fixture did not exercise a changed MAST ordinal";
  EXPECT_EQ(masked_record_payloads(plugin), before_payloads);

  const auto serialized = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(serialized, path);
  ASSERT_EQ(reread.header.masters.size(), 3u);
  EXPECT_EQ(reread.header.masters[0].key,
            fo4::PluginKey::from_filename("Survival Mode Overhaul.esp"));
  EXPECT_EQ(reread.header.masters[1].key,
            fo4::PluginKey::from_filename("llamaCompanionHeatherv2.esp"));
  EXPECT_EQ(reread.header.masters[2].key,
            fo4::PluginKey::from_filename("Fallout4.esm"));

  const auto reread_links = fo4::collect_form_links(reread);
  expect_same_logical_links(before_links, reread_links);
  EXPECT_EQ(masked_record_payloads(reread), before_payloads);
}
