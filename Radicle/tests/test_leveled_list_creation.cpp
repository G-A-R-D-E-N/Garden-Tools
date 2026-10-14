#include "fo4/forms/leveled_list.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Rec {
  std::string_view signature;
  std::uint32_t form_id;
};

constexpr std::uint32_t k_weapon = 0x00000800;
constexpr std::uint32_t k_keyword = 0x00000801;
constexpr std::uint32_t k_npc = 0x00000802;
constexpr std::uint32_t k_spell = 0x00000803;

std::vector<std::byte> make_plugin(std::initializer_list<Rec> records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x800u + static_cast<std::uint32_t>(records.size()));
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  for (const auto& rec : records) {
    const auto group = fo4::test::begin_group(out, rec.signature, 0);
    const auto record = fo4::test::begin_record(out, rec.signature, 0, rec.form_id);
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<std::byte> fixture() {
  return make_plugin({{"WEAP", k_weapon}, {"KYWD", k_keyword}, {"NPC_", k_npc}, {"SPEL", k_spell}});
}

fo4::FormKey create_list(fo4::Plugin& plugin, std::string_view signature, std::string_view editor_id) {
  return fo4::forms::create_leveled_list(plugin, fo4::Signature::from_string(signature),
                                         editor_id, 5, 2, 1);
}

fo4::forms::LeveledListEntry entry_for(std::string_view form_key) {
  fo4::forms::LeveledListEntry entry;
  entry.item = fo4::FormKey::parse(form_key);
  return entry;
}

std::vector<std::string> signature_names(const fo4::Record& record) {
  std::vector<std::string> names;
  for (const auto& subrecord : record.subrecords) names.push_back(subrecord.signature.to_string());
  return names;
}

}  // namespace

TEST(LeveledListCreation, CreatesEachLeveledTypeAndReadsItBack) {
  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  const auto item = create_list(plugin, "LVLI", "CreatedItems");
  const auto npc = create_list(plugin, "LVLN", "CreatedNpcs");
  const auto spell = create_list(plugin, "LVSP", "CreatedSpells");
  EXPECT_EQ(item, fo4::FormKey::parse("000804:Patch.esp"));
  EXPECT_EQ(npc, fo4::FormKey::parse("000805:Patch.esp"));
  EXPECT_EQ(spell, fo4::FormKey::parse("000806:Patch.esp"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const std::vector<std::pair<std::string_view, fo4::FormKey>> created{
      {"LVLI", item}, {"LVLN", npc}, {"LVSP", spell}};
  for (const auto& [signature, key] : created) {
    SCOPED_TRACE(std::string(signature));
    const auto* record = fo4::find_record(reread, key);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->header.signature.to_string(), std::string(signature));
    EXPECT_EQ(signature_names(*record),
              (std::vector<std::string>{"EDID", "OBND", "LVLD", "LVLM", "LVLF", "LLCT"}));
    EXPECT_EQ(record->subrecords[1].data, std::vector<std::byte>(12));

    const auto decoded = fo4::forms::decode_leveled_list(reread, key);
    EXPECT_EQ(decoded.chance_none, 5u);
    EXPECT_EQ(decoded.max_count, 2u);
    EXPECT_EQ(decoded.flags, 1u);
    EXPECT_TRUE(decoded.entries.empty());
    EXPECT_FALSE(decoded.use_global.has_value());
  }
}

TEST(LeveledListCreation, ValidTargetIsAcceptedAndInvalidTargetIsRefused) {
  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  const auto item = create_list(plugin, "LVLI", "CreatedItems");
  const auto npc = create_list(plugin, "LVLN", "CreatedNpcs");
  const auto spell = create_list(plugin, "LVSP", "CreatedSpells");

  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, item, 0, entry_for("000800:Patch.esp")));
  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, item, 1, entry_for("000801:Patch.esp")),
               fo4::forms::LeveledListError);
  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, item, 1, entry_for("000810:Patch.esp")),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, item).entries.size(), 1u);

  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, npc, 0, entry_for("000802:Patch.esp")));
  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, npc, 1, entry_for("000800:Patch.esp")),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, npc).entries.size(), 1u);

  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, spell, 0, entry_for("000803:Patch.esp")));
  EXPECT_THROW(fo4::forms::insert_leveled_list_entry(plugin, spell, 1, entry_for("000800:Patch.esp")),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::forms::decode_leveled_list(plugin, spell).entries.size(), 1u);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(fo4::forms::decode_leveled_list(reread, item).entries[0].item,
            fo4::FormKey::parse("000800:Patch.esp"));
  EXPECT_EQ(fo4::forms::decode_leveled_list(reread, spell).entries[0].item,
            fo4::FormKey::parse("000803:Patch.esp"));
}

TEST(LeveledListCreation, RefusesOtherSignaturesAndUndefinedFlags) {
  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  EXPECT_THROW(static_cast<void>(fo4::forms::create_leveled_list(
                   plugin, fo4::Signature::from_string("MISC"), std::string_view{"Bad"}, 0, 0, 0)),
               fo4::forms::LeveledListError);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_leveled_list(
                   plugin, fo4::Signature::from_string("LVLI"), std::string_view{"Bad"}, 0, 0, 0x08)),
               fo4::forms::LeveledListError);
  EXPECT_EQ(plugin.header.next_form_id, 0x804u);
  EXPECT_EQ(plugin.groups.size(), 4u);
}

TEST(LeveledListCreation, RefusesEmbeddedNulInEditorId) {
  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(static_cast<void>(fo4::forms::create_leveled_list(
                   plugin, fo4::Signature::from_string("LVLI"), std::string_view{"A\0B", 3}, 5, 2, 1)),
               fo4::forms::LeveledListError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(LeveledListCreation, ResetDropsEditorIdEntriesAndSettings) {
  auto plugin = fo4::read_plugin(fixture(), "Patch.esp");
  const auto item = create_list(plugin, "LVLI", "CreatedItems");
  ASSERT_TRUE(fo4::forms::insert_leveled_list_entry(plugin, item, 0, entry_for("000800:Patch.esp")));
  ASSERT_TRUE(fo4::forms::reset_leveled_list(plugin, item));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, item);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(signature_names(*record),
            (std::vector<std::string>{"OBND", "LVLD", "LVLF", "LLCT"}));
  EXPECT_EQ(record->subrecords.front().data, std::vector<std::byte>(12));
  const auto decoded = fo4::forms::decode_leveled_list(reread, item);
  EXPECT_TRUE(decoded.entries.empty());
  EXPECT_EQ(decoded.chance_none, 0u);
  EXPECT_EQ(decoded.max_count, 0u);
  EXPECT_EQ(decoded.flags, 0u);

  EXPECT_THROW(static_cast<void>(fo4::forms::reset_leveled_list(
                   plugin, fo4::FormKey::parse("000800:Patch.esp"))),
               fo4::forms::LeveledListError);
}
