#include "fo4/forms/leveled_list.hpp"
#include "fo4/patch/list_merge.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr const char* kMaster = "Master.esm";
constexpr const char* kFirst = "First.esp";
constexpr const char* kSecond = "Second.esp";
constexpr const char* kThird = "Third.esp";

struct Entry {
  std::uint16_t level;
  std::uint32_t item;
  std::uint16_t count;
  std::uint32_t coed_owner{0};
  std::uint32_t coed_union{0};
};

struct ListRecord {
  std::uint32_t id{0x00000800};
  std::vector<Entry> entries;
};

std::vector<std::byte> leveled_plugin_lists(const std::vector<std::string>& masters,
                                            const std::vector<ListRecord>& lists,
                                            std::uint32_t coed_owner_record = 0) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::make_tes4_plugin(0, 1.0f, masters);
  out.raw(std::span<const std::byte>{tes4});
  const auto group = fo4::test::begin_group(out, "LVLI", 0);
  for (const auto& list : lists) {
    const auto record = fo4::test::begin_record(out, "LVLI", 0, list.id);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"List\0", 5});
    fo4::test::write_subrecord(out, "LVLD", std::string_view{"\0", 1});
    for (const auto& entry : list.entries) {
      fo4::test::ByteWriter body;
      body.u16(entry.level);
      body.u16(0);
      body.u32(entry.item);
      body.u16(entry.count);
      body.u8(0);
      body.u8(0);
      fo4::test::write_subrecord(out, "LVLO", body.bytes);
      if (entry.coed_owner != 0 || entry.coed_union != 0) {
        fo4::test::ByteWriter coed;
        coed.u32(entry.coed_owner);
        coed.u32(entry.coed_union);
        coed.u32(0);
        fo4::test::write_subrecord(out, "COED", coed.bytes);
      }
    }
    fo4::test::ByteWriter count;
    count.u8(static_cast<std::uint8_t>(list.entries.size()));
    fo4::test::write_subrecord(out, "LLCT", count.bytes);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
  if (coed_owner_record != 0) {
    const auto owner_group = fo4::test::begin_group(out, "NPC_", 0);
    const auto owner = fo4::test::begin_record(out, "NPC_", 0, coed_owner_record);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Owner\0", 6});
    fo4::test::end_record(out, owner);
    fo4::test::end_group(out, owner_group);
  }
  return out.bytes;
}

std::vector<std::byte> leveled_plugin(const std::vector<std::string>& masters,
                                      const std::vector<Entry>& entries,
                                      std::uint32_t coed_owner_record = 0) {
  return leveled_plugin_lists(masters, {{0x00000800, entries}}, coed_owner_record);
}

std::vector<std::byte> form_list_plugin(const std::vector<std::string>& masters,
                                        const std::vector<std::uint32_t>& items) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::make_tes4_plugin(0, 1.0f, masters);
  out.raw(std::span<const std::byte>{tes4});
  const auto group = fo4::test::begin_group(out, "FLST", 0);
  const auto record = fo4::test::begin_record(out, "FLST", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Forms\0", 6});
  for (const auto item : items) {
    fo4::test::ByteWriter link;
    link.u32(item);
    fo4::test::write_subrecord(out, "LNAM", link.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::pair<std::uint16_t, std::uint32_t>> entry_pairs(
    const std::vector<fo4::forms::LeveledListEntry>& entries) {
  std::vector<std::pair<std::uint16_t, std::uint32_t>> out;
  for (const auto& entry : entries) {
    out.emplace_back(entry.level, entry.item.local().value());
  }
  return out;
}

class ListMerge : public ::testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_list_merge_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  void write(std::string_view name, const std::vector<std::byte>& bytes) {
    std::ofstream file(root_ / std::string{name}, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }

  fo4::env::Environment load() {
    const std::filesystem::path paths[] = {root_ / kMaster, root_ / kFirst, root_ / kSecond};
    return fo4::env::Environment::load(paths);
  }

  fo4::env::Environment load_with_third() {
    const std::filesystem::path paths[] = {root_ / kMaster, root_ / kFirst, root_ / kSecond,
                                           root_ / kThird};
    return fo4::env::Environment::load(paths);
  }

  const fo4::FormKey key_{fo4::FormKey::parse("000800:Master.esm")};
  std::filesystem::path root_;
};

TEST_F(ListMerge, LeveledListMergeKeepsAnEntryAnEarlierPluginAdded) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}, {2, 0x000801, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x000801, 1}, {3, 0x01000803, 2}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}}));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  EXPECT_EQ(result.counts.unchanged, 0u);
  ASSERT_EQ(result.patch.header.masters.size(), 2u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename(kFirst));

  const auto merged = fo4::forms::decode_leveled_list(result.patch, key_);
  const std::vector<std::pair<std::uint16_t, std::uint32_t>> expected{{1, 0x000800},
                                                                     {3, 0x000803}};
  EXPECT_EQ(entry_pairs(merged.entries), expected);
  ASSERT_EQ(merged.entries.size(), 2u);
  EXPECT_EQ(merged.entries[1].count, 2);
  EXPECT_EQ(merged.entries[1].item.owner(), fo4::PluginKey::from_filename(kFirst));

  const auto written = fo4::write_plugin(result.patch);
  const auto reloaded = fo4::read_plugin(written, "Merged.esp");
  EXPECT_EQ(entry_pairs(fo4::forms::decode_leveled_list(reloaded, key_).entries), expected);
}

TEST_F(ListMerge, LeveledListMergeSkipsARecordTheWinnerAlreadyHolds) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x000801, 1}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x000801, 1}}));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 0u);
  EXPECT_EQ(result.counts.unchanged, 1u);
  EXPECT_TRUE(result.patch.groups.empty());
}

TEST_F(ListMerge, TheMergedPatchMasterTableIsInLoadOrder) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}, {2, 0x000801, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x000801, 1}, {3, 0x01000803, 1}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {4, 0x01000804, 1}}));
  write(kThird, leveled_plugin({kMaster, kFirst}, {{1, 0x000800, 1}, {2, 0x01000802, 1}}));
  const auto env = load_with_third();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  ASSERT_EQ(result.patch.header.masters.size(), 3u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename(kFirst));
  EXPECT_EQ(result.patch.header.masters[2].key, fo4::PluginKey::from_filename(kSecond));
}

TEST_F(ListMerge, AMergedEntryKeepsItsCoedOwnerAsAMaster) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x000800, 1, 0x01000805}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}}));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  ASSERT_EQ(result.patch.header.masters.size(), 2u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename(kFirst));

  const auto merged = fo4::forms::decode_leveled_list(result.patch, key_);
  ASSERT_EQ(merged.entries.size(), 2u);
  ASSERT_TRUE(merged.entries[1].extra.has_value());
  EXPECT_EQ(merged.entries[1].extra->owner, fo4::FormKey::parse("000805:First.esp"));
}

TEST_F(ListMerge, ACoedOwnerIsTypedFromTheLoadOrder) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x01000801, 1}}, 0x01000805));
  write(kSecond, leveled_plugin({kMaster},
                                {{1, 0x000800, 1}, {3, 0x01000801, 1, 0x01000805, 0x00000042}},
                                0x01000805));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  ASSERT_EQ(result.patch.header.masters.size(), 3u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename(kFirst));
  EXPECT_EQ(result.patch.header.masters[2].key, fo4::PluginKey::from_filename(kSecond));

  const auto merged = fo4::forms::decode_leveled_list(result.patch, key_);
  ASSERT_EQ(merged.entries.size(), 3u);
  ASSERT_TRUE(merged.entries[2].extra.has_value());
  EXPECT_EQ(merged.entries[2].extra->owner, fo4::FormKey::parse("000805:Second.esp"));
  EXPECT_EQ(merged.entries[2].extra->global_or_rank, 0x00000042u);
}

TEST_F(ListMerge, ASecondMergedRecordKeepsTheOwnerTypesOfTheFirst) {
  write(kMaster, leveled_plugin_lists({}, {{0x00000800, {{1, 0x000800, 1}, {2, 0x000801, 1}}},
                                          {0x00000801, {{1, 0x000801, 1}}}},
                                      0x00000805));
  write(kFirst, leveled_plugin_lists({kMaster},
                                     {{0x00000800, {{1, 0x000800, 1}, {2, 0x000801, 1},
                                                     {3, 0x00000803, 2}}}}));
  write(kSecond, leveled_plugin_lists({kMaster},
                                      {{0x00000800, {{1, 0x000800, 1},
                                                     {3, 0x00000801, 1, 0x00000805, 0x00000042}}},
                                       {0x00000801, {{1, 0x000801, 1}, {3, 0x00000803, 1}}}}));
  write(kThird, leveled_plugin_lists({kMaster, kFirst},
                                     {{0x00000801, {{1, 0x000801, 1}, {2, 0x01000802, 1}}}}));
  const auto env = load_with_third();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 2u);
  EXPECT_EQ(result.counts.unchanged, 0u);
  ASSERT_EQ(result.patch.header.masters.size(), 2u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename(kFirst));

  const auto merged = fo4::forms::decode_leveled_list(result.patch, key_);
  const auto coed = std::find_if(merged.entries.begin(), merged.entries.end(),
                                 [](const fo4::forms::LeveledListEntry& entry) {
                                   return entry.item == fo4::FormKey::parse("000801:Master.esm");
                                 });
  ASSERT_NE(coed, merged.entries.end());
  ASSERT_TRUE(coed->extra.has_value());
  EXPECT_EQ(coed->extra->owner, fo4::FormKey::parse("000805:Master.esm"));
  EXPECT_EQ(coed->extra->global_or_rank, 0x00000042u);
}

TEST_F(ListMerge, AnOwnerOutsideTheLoadOrderIsTypedFromTheVersionThatCarriesIt) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}}));
  write(kFirst, leveled_plugin({kMaster, "Missing.esm"}, {{1, 0x000800, 1}, {2, 0x01000801, 1}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}}));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  ASSERT_EQ(result.patch.header.masters.size(), 2u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename("Missing.esm"));
  EXPECT_EQ(result.patch.header.masters[1].kind, fo4::PluginKind::full);

  const auto merged = fo4::forms::decode_leveled_list(result.patch, key_);
  ASSERT_EQ(merged.entries.size(), 2u);
  EXPECT_EQ(merged.entries[1].item, fo4::FormKey::parse("000801:Missing.esm"));
}

TEST_F(ListMerge, AMultisetRemovalDropsOnlyTheCopiesTheOverrideDrops) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}, {1, 0x000800, 1}, {1, 0x000800, 1},
                                     {2, 0x000801, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {2, 0x000801, 1},
                                           {3, 0x01000803, 1}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}, {1, 0x000800, 1},
                                            {1, 0x000800, 1}, {2, 0x000801, 1}}));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  const std::vector<std::pair<std::uint16_t, std::uint32_t>> expected{
      {1, 0x000800}, {2, 0x000801}, {3, 0x000803}};
  EXPECT_EQ(entry_pairs(fo4::forms::decode_leveled_list(result.patch, key_).entries), expected);
}

TEST_F(ListMerge, ALaterOverrideWinsAConflictingCountOfOneItem) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 2}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 3}}));
  write(kThird, leveled_plugin({kMaster}, {{1, 0x000800, 1}}));
  const auto env = load_with_third();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("LVLI"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  const auto merged = fo4::forms::decode_leveled_list(result.patch, key_);
  ASSERT_EQ(merged.entries.size(), 1u);
  EXPECT_EQ(merged.entries[0].count, 3);
}

TEST_F(ListMerge, FormListMergeIsAUnionOfTheLoadOrder) {
  write(kMaster, form_list_plugin({}, {0x000800, 0x000801}));
  write(kFirst, form_list_plugin({kMaster}, {0x000800, 0x000801, 0x01000803}));
  write(kSecond, form_list_plugin({kMaster}, {0x000801}));
  const auto env = load();

  const auto result = fo4::patch::build_list_merge_patch(
      env, fo4::Signature::from_string("FLST"), "Merged.esp");

  EXPECT_EQ(result.counts.merged, 1u);
  ASSERT_EQ(result.patch.header.masters.size(), 2u);
  EXPECT_EQ(result.patch.header.masters[0].key, fo4::PluginKey::from_filename(kMaster));
  EXPECT_EQ(result.patch.header.masters[1].key, fo4::PluginKey::from_filename(kFirst));
  const auto descriptor =
      fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("FLST"), "FLST\\Items");
  ASSERT_TRUE(descriptor.has_value());
  const std::vector<fo4::FormKey> expected{fo4::FormKey::parse("000801:Master.esm"),
                                           fo4::FormKey::parse("000803:First.esp")};
  const auto values = fo4::schema::record_formlink_list_values(result.patch, key_, *descriptor);
  ASSERT_TRUE(values.has_value());
  EXPECT_EQ(*values, expected);

  const auto written = fo4::write_plugin(result.patch);
  const auto reloaded = fo4::read_plugin(written, "Merged.esp");
  const auto reread_values =
      fo4::schema::record_formlink_list_values(reloaded, key_, *descriptor);
  ASSERT_TRUE(reread_values.has_value());
  EXPECT_EQ(*reread_values, expected);
}

TEST_F(ListMerge, AnUnsupportedSignatureIsRefused) {
  write(kMaster, leveled_plugin({}, {{1, 0x000800, 1}}));
  write(kFirst, leveled_plugin({kMaster}, {{1, 0x000800, 1}}));
  write(kSecond, leveled_plugin({kMaster}, {{1, 0x000800, 1}}));
  const auto env = load();

  EXPECT_THROW(fo4::patch::build_list_merge_patch(env, fo4::Signature::from_string("MISC"),
                                                  "Merged.esp"),
               fo4::patch::ListMergeError);
}

}  // namespace
