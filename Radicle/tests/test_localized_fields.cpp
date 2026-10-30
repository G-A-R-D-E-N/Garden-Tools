#include "fo4/schema/localized_fields.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_quest.h"
#include "fo4/forms/armor.hpp"
#include "fo4/forms/race.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"
#include "quest_structure_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr auto k_race = fo4::Signature::from_string("RACE");
constexpr auto k_npc = fo4::Signature::from_string("NPC_");
constexpr auto k_desc = fo4::Signature::from_string("DESC");
constexpr auto k_full = fo4::Signature::from_string("FULL");
constexpr auto k_cnam = fo4::Signature::from_string("CNAM");
constexpr auto k_edid = fo4::Signature::from_string("EDID");

fo4::Subrecord make(fo4::Signature signature, std::vector<std::byte> data) {
  fo4::Subrecord sub;
  sub.signature = signature;
  sub.data = std::move(data);
  return sub;
}

std::vector<std::byte> text(std::string_view value) {
  std::vector<std::byte> out(value.size() + 1);
  for (std::size_t i = 0; i < value.size(); ++i) {
    out[i] = std::byte{static_cast<std::uint8_t>(value[i])};
  }
  return out;
}

std::vector<std::byte> id(std::uint32_t value) {
  std::vector<std::byte> out(4);
  std::memcpy(out.data(), &value, 4);
  return out;
}

fo4::Plugin plugin_with_flags(std::uint32_t tes4_flags) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", tes4_flags, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return fo4::read_plugin(out.bytes, "Test.esp");
}

std::vector<std::byte> bytes_of(std::string_view value) {
  std::vector<std::byte> out(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    out[i] = std::byte{static_cast<std::uint8_t>(value[i])};
  }
  return out;
}

using Member = std::pair<std::string_view, std::vector<std::byte>>;

std::vector<std::byte> localized_plugin(std::string_view record_type,
                                        const std::vector<Member>& members) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, record_type, 0);
  const auto record = fo4::test::begin_record(out, record_type, 0, 0x800);
  for (const auto& [signature, data] : members) {
    fo4::test::write_subrecord(out, signature, data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

void expect_raw_without_id(const fo4::schema::LocalizedString& value,
                           const std::vector<std::byte>& payload) {
  EXPECT_FALSE(value.localized());
  EXPECT_FALSE(value.id.has_value());
  EXPECT_FALSE(value.text.has_value());
  EXPECT_EQ(value.inline_text(), "");
  ASSERT_TRUE(value.raw_id.has_value());
  EXPECT_EQ(*value.raw_id, payload);
  EXPECT_EQ(fo4::schema::write_localized_string(value), payload);
}

}  // namespace

// Membership is declared per (record, subrecord); size never decides it.
TEST(LocalizedFields, MembershipIsPerRecordAndSubrecord) {
  EXPECT_TRUE(fo4::schema::is_localized_field(k_race, k_desc));
  EXPECT_TRUE(fo4::schema::is_localized_field(k_race, k_full));
  EXPECT_FALSE(fo4::schema::is_localized_field(k_race, k_edid));
  // NPC_ CNAM is four bytes in every shipped instance but is a Class FormID,
  // so it must never be treated as a string ID.
  EXPECT_FALSE(fo4::schema::is_localized_field(k_npc, k_cnam));
}

TEST(LocalizedFields, LocalizedPluginYieldsStringIds) {
  auto plugin = plugin_with_flags(0x80);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, id(0x1234)));
  ASSERT_TRUE(value.localized());
  EXPECT_EQ(*value.id, 0x1234u);
  EXPECT_FALSE(value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(value), id(0x1234));
}

TEST(LocalizedFields, NonLocalizedPluginYieldsInlineText) {
  auto plugin = plugin_with_flags(0);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, text("hello")));
  EXPECT_FALSE(value.localized());
  EXPECT_EQ(value.inline_text(), "hello");
  EXPECT_EQ(fo4::schema::write_localized_string(value), text("hello"));
}

TEST(LocalizedFields, BitSevenSelectsRepresentationForTheSameFourBytes) {
  const auto payload = make(k_full, text("abc"));
  const auto inline_plugin = plugin_with_flags(0);
  const auto external_plugin = plugin_with_flags(0x80);
  const auto inline_value =
      fo4::schema::read_localized_string(inline_plugin, k_race, k_full, payload);
  const auto external_value =
      fo4::schema::read_localized_string(external_plugin, k_race, k_full, payload);
  EXPECT_EQ(inline_value.inline_text(), "abc");
  EXPECT_FALSE(inline_value.localized());
  ASSERT_TRUE(external_value.localized());
  EXPECT_EQ(*external_value.id, 0x00636261u);
  EXPECT_FALSE(external_value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(inline_value), payload.data);
  EXPECT_EQ(fo4::schema::write_localized_string(external_value), payload.data);
  EXPECT_EQ(inline_plugin.header.tes4_flags.value(), 0u);
  EXPECT_EQ(external_plugin.header.tes4_flags.value(), 0x80u);
}

TEST(LocalizedFields, ZeroExternalIdIsPreservedWithoutTextLookup) {
  auto plugin = plugin_with_flags(0x80);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, id(0)));
  ASSERT_TRUE(value.localized());
  EXPECT_EQ(*value.id, 0u);
  EXPECT_FALSE(value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(value), id(0));
}

// A non-lstring field stays inline even in a localized plugin.
TEST(LocalizedFields, NonLocalizedFieldStaysInlineInALocalizedPlugin) {
  auto plugin = plugin_with_flags(0x80);
  const auto value =
      fo4::schema::read_localized_string(plugin, k_race, k_edid, make(k_edid, text("EdId")));
  EXPECT_FALSE(value.localized());
  EXPECT_EQ(value.inline_text(), "EdId");
}

TEST(LocalizedFields, NonFourByteLocalizedPayloadHasNoIdAndRoundTrips) {
  auto plugin = plugin_with_flags(0x80);
  const std::vector<std::vector<std::byte>> payloads{
      {std::byte{0x34}, std::byte{0x12}},
      {std::byte{'n'}, std::byte{'o'}, std::byte{'p'}, std::byte{'e'}, std::byte{0}},
  };
  for (const auto& payload : payloads) {
    const auto value =
        fo4::schema::read_localized_string(plugin, k_race, k_desc, make(k_desc, payload));
    EXPECT_FALSE(value.localized());
    EXPECT_FALSE(value.id.has_value());
    EXPECT_FALSE(value.text.has_value());
    EXPECT_EQ(fo4::schema::write_localized_string(value), payload);
  }
}

TEST(LocalizedFields, InlineTextMustBeSingleNulTerminated) {
  auto plugin = plugin_with_flags(0);
  EXPECT_THROW(
      static_cast<void>(fo4::schema::read_localized_string(
          plugin, k_race, k_desc, make(k_desc, std::vector<std::byte>{std::byte{'a'}}))),
      std::exception);
}

TEST(LocalizedFields, ArmorCallerKeepsNonFourBytePayloadWithoutId) {
  fo4::test::ByteWriter data;
  data.u32(5);
  data.f32(1.0f);
  data.u32(100);
  const auto full = bytes_of("abc");
  const auto desc = text("nope");
  const auto bytes = localized_plugin(
      "ARMO", {{"FULL", full}, {"DESC", desc}, {"DATA", data.bytes}});
  auto plugin = fo4::read_plugin(bytes, "Test.esp");

  std::optional<fo4::forms::Armor> armor;
  EXPECT_NO_THROW(armor = fo4::forms::decode_armor(plugin, fo4::FormKey::parse("000800:Test.esp")));
  ASSERT_TRUE(armor.has_value());
  ASSERT_TRUE(armor->full_name.has_value());
  expect_raw_without_id(*armor->full_name, full);
  ASSERT_TRUE(armor->description.has_value());
  expect_raw_without_id(*armor->description, desc);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(LocalizedFields, RaceCallerKeepsNonFourBytePayloadWithoutId) {
  const auto full = text("nope");
  const auto desc = bytes_of("abc");
  const auto bytes = localized_plugin(
      "RACE",
      {{"FULL", full},
       {"DESC", desc},
       {"DATA", std::vector<std::byte>(200)},
       {"VTCK", std::vector<std::byte>(8)},
       {"PNAM", std::vector<std::byte>(4)},
       {"UNAM", std::vector<std::byte>(4)},
       {"NAM1", {}},
       {"MNAM", {}},
       {"FNAM", {}},
       {"NAM3", {}}});
  auto plugin = fo4::read_plugin(bytes, "Test.esp");

  std::optional<fo4::forms::Race> race;
  EXPECT_NO_THROW(race = fo4::forms::decode_race(plugin, fo4::FormKey::parse("000800:Test.esp")));
  ASSERT_TRUE(race.has_value());
  ASSERT_TRUE(race->full_name.has_value());
  expect_raw_without_id(*race->full_name, full);
  expect_raw_without_id(race->description, desc);
  EXPECT_EQ(fo4::write_plugin(plugin), bytes);
}

TEST(LocalizedFields, QuestStructureCallerReportsNonFourByteObjectiveTextAsMalformed) {
  auto plugin = fo4::test::quest_structure_plugin(true);
  std::vector<std::byte> truncated;
  fo4::test::change_quest(plugin, [&truncated](fo4::Record& record) {
    for (auto& subrecord : record.subrecords) {
      if (subrecord.signature == fo4::Signature::from_string("NNAM") && subrecord.data.size() == 4) {
        subrecord.data.resize(3);
        truncated = subrecord.data;
        return;
      }
    }
  });
  const auto bytes = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::write_plugin(fo4::read_plugin(bytes, "Quests.esp")), bytes);

  const auto root = std::filesystem::temp_directory_path() /
                    ("localized_callers_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Quests.esp";
  fo4::write_plugin_file(plugin, path);

  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_quest_structure* structure = nullptr;
  const auto status = radicle_quest_structure_read(raw, "000800:Quests.esp", &structure);
  radicle_plugin_destroy(raw);
  ASSERT_EQ(status, RADICLE_STATUS_OK) << radicle_last_error();

  radicle_quest_structure_view view{};
  EXPECT_EQ(radicle_quest_structure_get_view(structure, &view), RADICLE_STATUS_OK);
  const bool has_one_objective = view.objective_count == 1u;
  const auto objective_text = has_one_objective ? view.objectives[0].text : radicle_quest_text{};
  const auto* text_bytes = reinterpret_cast<const std::byte*>(objective_text.bytes);
  const std::vector<std::byte> objective_bytes(text_bytes, text_bytes + objective_text.size);
  radicle_quest_structure_destroy(structure);
  std::error_code error;
  std::filesystem::remove_all(root, error);

  ASSERT_TRUE(has_one_objective);
  EXPECT_EQ(objective_text.kind, RADICLE_NAME_MALFORMED);
  EXPECT_EQ(objective_text.string_id, 0u);
  EXPECT_EQ(objective_text.size, 3u);
  EXPECT_EQ(objective_bytes, truncated);
}

TEST(LocalizedFields, Wp10LocalizedPairsAreMembers) {
  const std::pair<const char*, const char*> pairs[] = {
      {"QUST", "FULL"}, {"PERK", "FULL"}, {"PERK", "DESC"}, {"PERK", "EPF2"},
      {"SCOL", "FULL"}, {"WRLD", "FULL"}, {"REGN", "RDMP"}, {"TERM", "BTXT"},
      {"TERM", "ITXT"}, {"TERM", "RNAM"}, {"TERM", "UNAM"}, {"RACE", "TTGP"},
      {"RACE", "MPPN"}, {"RACE", "FMRN"}, {"NPC_", "FULL"}, {"NPC_", "SHRT"},
      {"NPC_", "ATTX"}, {"MISC", "FULL"}, {"OMOD", "FULL"}, {"OMOD", "DESC"},
      {"INNR", "WNAM"}, {"BPTD", "BPTN"}, {"CELL", "FULL"}, {"COBJ", "DESC"},
      {"FLST", "FULL"}};
  for (const auto& [record, subrecord] : pairs) {
    EXPECT_TRUE(fo4::schema::is_localized_field(fo4::Signature::from_string(record),
                                                fo4::Signature::from_string(subrecord)))
        << record << " " << subrecord;
  }
  EXPECT_FALSE(fo4::schema::is_localized_field(fo4::Signature::from_string("TERM"),
                                               fo4::Signature::from_string("SNAM")));
  EXPECT_FALSE(fo4::schema::is_localized_field(k_race, fo4::Signature::from_string("DATA")));
}

TEST(LocalizedFields, Wp10NewPairRoundTripsAStringId) {
  auto plugin = plugin_with_flags(0x80);
  const auto shrt = fo4::Signature::from_string("SHRT");
  const auto value = fo4::schema::read_localized_string(plugin, k_npc, shrt, make(shrt, id(0x4321)));
  ASSERT_TRUE(value.localized());
  EXPECT_EQ(*value.id, 0x4321u);
  EXPECT_FALSE(value.text.has_value());
  EXPECT_EQ(fo4::schema::write_localized_string(value), id(0x4321));
}
