#include "fo4/graph/conflict_kind.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view k_value_a{"\xAA\xBB\0\0", 4};
constexpr std::string_view k_value_b{"\xCC\xDD\0\0", 4};
constexpr std::string_view k_value_c{"\x11", 1};
constexpr std::string_view k_dest_health_100{"\x64\0\0\0\x01\0\0\0", 8};
constexpr std::string_view k_dest_health_200{"\xC8\0\0\0\x01\0\0\0", 8};

const auto k_data = fo4::Signature::from_string("DATA");
const auto k_edid = fo4::Signature::from_string("EDID");
const auto k_mode = fo4::Signature::from_string("MODE");
const auto k_misc = fo4::Signature::from_string("MISC");

struct Sub {
  std::string_view signature;
  std::string_view data;
};

struct Rec {
  std::string_view signature;
  std::uint32_t form_id;
  std::string_view editor_id;
  std::vector<Sub> subs;
  std::uint32_t flags{0};
};

std::vector<std::byte> make_plugin(const std::vector<Rec>& records, bool with_master) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(static_cast<std::uint32_t>(records.size()));
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  if (with_master) {
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);

  for (const auto& spec : records) {
    const auto group = fo4::test::begin_group(out, spec.signature, 0);
    const auto record = fo4::test::begin_record(out, spec.signature, spec.flags, spec.form_id);
    std::vector<std::byte> edid;
    for (const char ch : spec.editor_id) {
      edid.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    }
    edid.push_back(std::byte{0});
    fo4::test::write_subrecord(out, "EDID", edid);
    for (const auto& sub : spec.subs) {
      fo4::test::write_subrecord(out, sub.signature, sub.data);
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

fo4::Plugin base_plugin(std::string_view signature,
                        std::string_view editor_id,
                        std::vector<Sub> subs) {
  return fo4::read_plugin(make_plugin({{signature, 0x800, editor_id, std::move(subs)}}, false),
                          "Master.esm");
}

fo4::Plugin override_plugin(std::string_view name,
                            std::string_view signature,
                            std::string_view editor_id,
                            std::vector<Sub> subs) {
  return fo4::read_plugin(make_plugin({{signature, 0x800, editor_id, std::move(subs)}}, true),
                          name);
}

fo4::FormKey master_key() { return fo4::FormKey::parse("000800:Master.esm"); }

fo4::FormKey quest_key() { return fo4::FormKey::parse("000900:Master.esm"); }

fo4::RecordConflict classify(std::span<const fo4::Plugin* const> order,
                             const fo4::FormKey& key,
                             std::span<const fo4::ConflictPriorityRule> rules = {},
                             fo4::ConflictPriority record_priority = fo4::ConflictPriority::normal) {
  const auto graph = fo4::ConflictGraph::build(order);
  return fo4::classify_record_conflict(
      graph, fo4::ConflictQuery{order, key, record_priority, rules});
}

}  // namespace

TEST(ConflictKind, ARecordInOnePluginIsOnlyOne) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::only_one);
  ASSERT_EQ(result.roles.size(), 1u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::only_one);

  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[0].signature, k_edid);
  EXPECT_EQ(result.subrecords[0].ordinal, 0u);
  EXPECT_EQ(result.subrecords[1].signature, k_data);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::only_one);
  EXPECT_EQ(result.subrecords[1].roles[0], fo4::ConflictRole::only_one);
}

TEST(ConflictKind, AnIdenticalOverrideIsIdenticalToMaster) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::no_conflict);
  ASSERT_EQ(result.roles.size(), 2u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::identical_to_master);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::no_conflict);
  EXPECT_EQ(result.subrecords[1].roles[1], fo4::ConflictRole::identical_to_master);
}

TEST(ConflictKind, AChangedSubrecordInOneOverrideIsAnOverrideWithoutConflict) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  ASSERT_EQ(result.roles.size(), 2u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::override_);

  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[0].category, fo4::ConflictCategory::no_conflict);
  EXPECT_EQ(result.subrecords[1].signature, k_data);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.subrecords[1].roles[1], fo4::ConflictRole::override_);
}

TEST(ConflictKind, AnOverrideRestoredByALaterPluginIsAConflict) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::conflict);
  ASSERT_EQ(result.roles.size(), 3u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::loses);
  EXPECT_EQ(result.roles[2], fo4::ConflictRole::identical_to_master_wins);
}

TEST(ConflictKind, ThreePluginsWithThreeValuesAreAConflict) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_c}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::conflict);
  ASSERT_EQ(result.roles.size(), 3u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::loses);
  EXPECT_EQ(result.roles[2], fo4::ConflictRole::wins);
}

TEST(ConflictKind, ASubrecordAddedByAnOverrideIsBenignWithThatPriority) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto patch =
      override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_a}, {"MODE", k_value_c}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const fo4::ConflictPriorityRule rules[] = {{k_misc, k_mode, fo4::ConflictPriority::benign_if_added}};
  const auto result = classify(order, master_key(), rules);

  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[2].signature, k_mode);
  EXPECT_EQ(result.subrecords[2].category, fo4::ConflictCategory::benign);
  ASSERT_EQ(result.subrecords[2].roles.size(), 2u);
  EXPECT_EQ(result.subrecords[2].roles[0], fo4::ConflictRole::unknown);
  EXPECT_EQ(result.subrecords[2].roles[1], fo4::ConflictRole::identical_to_master);
}

// Detects: an empty marker subrecord compared equal to an absent one, hiding an override that only adds a marker.
TEST(ConflictKind, AnEmptyMarkerAddedByAnOverrideIsNotIdenticalToMaster) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto patch =
      override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_a}, {"MODE", std::string_view{}}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[2].signature, k_mode);
  EXPECT_EQ(result.subrecords[2].category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, ACriticalSubrecordIsReportedOnItsRow) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto plain = classify(order, master_key());
  EXPECT_EQ(plain.category, fo4::ConflictCategory::conflict);

  const fo4::ConflictPriorityRule rules[] = {{k_misc, k_data, fo4::ConflictPriority::critical}};
  const auto result = classify(order, master_key(), rules);
  EXPECT_EQ(result.category, fo4::ConflictCategory::critical);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[0].category, fo4::ConflictCategory::no_conflict);
  EXPECT_EQ(result.subrecords[1].signature, k_data);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::critical);
}

TEST(ConflictKind, TheRecordPriorityDrivesTheRecordCategory) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  EXPECT_EQ(classify(order, master_key()).category, fo4::ConflictCategory::override_);
  EXPECT_EQ(classify(order, master_key(), {}, fo4::ConflictPriority::critical).category,
            fo4::ConflictCategory::critical);
  EXPECT_EQ(classify(order, master_key(), {}, fo4::ConflictPriority::ignore).category,
            fo4::ConflictCategory::no_conflict);
}

TEST(ConflictKind, AnIgnoredSubrecordDoesNotCountAsADifference) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const fo4::ConflictPriorityRule rules[] = {{k_misc, k_data, fo4::ConflictPriority::ignore}};
  const auto result = classify(order, master_key(), rules);

  EXPECT_EQ(result.category, fo4::ConflictCategory::no_conflict);
  ASSERT_EQ(result.roles.size(), 2u);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::identical_to_master);
  ASSERT_EQ(result.subrecords.size(), 1u);
  EXPECT_EQ(result.subrecords[0].signature, k_edid);
}

TEST(ConflictKind, DefaultLeveledAndActorEffectCountsAreBenignAtRecordLevel) {
  for (const auto& [record, count] : std::vector<std::pair<std::string_view, std::string_view>>{
           {"LVLI", "LLCT"}, {"LVLN", "LLCT"}, {"LVSP", "LLCT"},
           {"NPC_", "SPCT"}, {"RACE", "SPCT"}}) {
    const auto width = count == "LLCT" ? 1u : 4u;
    const std::string_view one{"\x01\0\0\0", width};
    const std::string_view two{"\x02\0\0\0", width};
    const std::string_view three{"\x03\0\0\0", width};
    const auto base = base_plugin(record, "List", {{count, one}});
    const auto middle = override_plugin("Middle.esp", record, "List", {{count, two}});
    const auto patch = override_plugin("Patch.esp", record, "List", {{count, three}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record;
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record;
    const fo4::ConflictPriorityRule override_rule[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string(count), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), override_rule).category, fo4::ConflictCategory::conflict) << record;
  }
}

TEST(ConflictKind, DefaultStoryLinksAndCountsRemainBenignAndCallerCanOverride) {
  for (const auto& [record, member] : std::vector<std::pair<std::string_view, std::string_view>>{
           {"SMBN", "SNAM"}, {"SMQN", "SNAM"}, {"SMEN", "SNAM"}, {"SMQN", "QNAM"},
           {"INFO", "PNAM"}, {"NPC_", "PRKZ"}, {"SNDR", "ITMC"}, {"LENS", "LFSP"},
           {"TERM", "BSIZ"}, {"TERM", "ISIZ"}}) {
    constexpr std::string_view first{"\x01\0\0\0", 4};
    const auto base = base_plugin(record, "Thing", {{member, first}});
    const auto middle = override_plugin("Middle.esp", record, "Thing", {{member, k_value_a}});
    const auto patch = override_plugin("Patch.esp", record, "Thing", {{member, k_value_b}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record << member;
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record << member;
    const fo4::ConflictPriorityRule rule[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string(member), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), rule).category, fo4::ConflictCategory::conflict) << record << member;
  }
}

TEST(ConflictKind, ContainerAndWaterCountsAreBenignOnlyForDocumentedRecords) {
  constexpr std::string_view first{"\x01\0\0\0", 4};
  for (const auto& [record, member] : std::vector<std::pair<std::string_view, std::string_view>>{
           {"CONT", "COCT"}, {"FURN", "COCT"}, {"NPC_", "COCT"},
           {"CELL", "XWCN"}, {"ACHR", "XWCN"}, {"REFR", "XWCN"}}) {
    const auto base = base_plugin(record, "Thing", {{member, first}});
    const auto middle = override_plugin("Middle.esp", record, "Thing", {{member, k_value_a}});
    const auto patch = override_plugin("Patch.esp", record, "Thing", {{member, k_value_b}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record << member;
    const fo4::ConflictPriorityRule rule[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string(member), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), rule).category, fo4::ConflictCategory::conflict) << record << member;
  }
  const auto base = base_plugin("TERM", "Terminal", {{"COCT", first}});
  const auto patch = override_plugin("Patch.esp", "TERM", "Terminal", {{"COCT", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  EXPECT_EQ(classify(order, master_key()).category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, ConditionCountDefaultsAreLimitedToDocumentedRootScopes) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view three{"\x03\0\0\0", 4};
  for (const auto record : {"ACTI", "FACT", "FURN", "SMBN", "SMQN", "SMEN", "MUST"}) {
    const auto base = base_plugin(record, "Thing", {{"CITC", one}});
    const auto middle = override_plugin("Middle.esp", record, "Thing", {{"CITC", two}});
    const auto patch = override_plugin("Patch.esp", record, "Thing", {{"CITC", three}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record;
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record;
    const fo4::ConflictPriorityRule rule[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string("CITC"), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), rule).category, fo4::ConflictCategory::conflict) << record;
    const auto changed = override_plugin("Changed.esp", record, "Thing", {{"CITC", two}, {"DATA", k_value_a}});
    const std::vector<const fo4::Plugin*> mixed_order{&base, &changed};
    EXPECT_EQ(classify(mixed_order, master_key()).category, fo4::ConflictCategory::override_) << record;
  }
  for (const auto record : {"SCEN", "PACK", "MISC"}) {
    const auto base = base_plugin(record, "Thing", {{"CITC", one}});
    const auto patch = override_plugin("Patch.esp", record, "Thing", {{"CITC", two}});
    const std::vector<const fo4::Plugin*> order{&base, &patch};
    EXPECT_EQ(classify(order, master_key()).category, fo4::ConflictCategory::override_) << record;
  }
}

TEST(ConflictKind, RootKeywordCountsUseBenignDefaultWithoutCoveringNestedAliases) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view three{"\x03\0\0\0", 4};
  for (const auto record : {"ACTI", "TACT", "ALCH", "AMMO", "ARMO", "BOOK", "CONT", "DOOR",
                            "FURN", "MSTT", "IDLM", "LCTN", "SCEN", "ARTO", "INGR", "KEYM",
                            "LIGH", "MGEF", "MISC", "NPC_", "RACE", "SPEL", "FLOR", "TERM", "WEAP"}) {
    const auto base = base_plugin(record, "Thing", {{"KSIZ", one}});
    const auto middle = override_plugin("Middle.esp", record, "Thing", {{"KSIZ", two}});
    const auto patch = override_plugin("Patch.esp", record, "Thing", {{"KSIZ", three}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record;
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record;
    const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string("KSIZ"), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), normal).category, fo4::ConflictCategory::conflict) << record;
  }
  const auto base = base_plugin("QUST", "Quest", {{"ALST", one}, {"KSIZ", one}, {"ALED", {}}});
  const auto patch = override_plugin("Patch.esp", "QUST", "Quest", {{"ALST", one}, {"KSIZ", two}, {"ALED", {}}});
  const std::vector<const fo4::Plugin*> alias_order{&base, &patch};
  EXPECT_EQ(classify(alias_order, master_key()).category, fo4::ConflictCategory::override_);
  const auto named_base = base_plugin("INNR", "Rules", {{"WNAM", "Name"}, {"KSIZ", one}});
  const auto named_patch = override_plugin("Patch.esp", "INNR", "Rules", {{"WNAM", "Name"}, {"KSIZ", two}});
  const std::vector<const fo4::Plugin*> named_order{&named_base, &named_patch};
  EXPECT_EQ(classify(named_order, master_key()).category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, RootKeywordCountDoesNotHideNormalDataOverride) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view value_100{"\x64\0\0\0\0\0\0\0", 8};
  constexpr std::string_view value_200{"\xC8\0\0\0\0\0\0\0", 8};
  const auto base = base_plugin("MISC", "Thing", {{"KSIZ", one}, {"DATA", value_100}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"KSIZ", two}, {"DATA", value_200}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[1].signature, fo4::Signature::from_string("KSIZ"));
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.subrecords[2].signature, k_data);
  EXPECT_EQ(result.subrecords[2].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::override_}));
}

TEST(ConflictKind, StableReferenceAliasCountsUseScopedBenignPriority) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view three{"\x03\0\0\0", 4};
  const auto alias = [&](std::string_view value) {
    return std::vector<Sub>{{"ANAM", two}, {"ALST", one}, {"ALID", std::string_view{"Alias\0", 6}},
                            {"FNAM", one}, {"KSIZ", value}, {"COCT", value}, {"ALED", {}}};
  };
  const auto base = base_plugin("QUST", "Quest", alias(one));
  const auto middle = override_plugin("Middle.esp", "QUST", "Quest", alias(two));
  const auto patch = override_plugin("Patch.esp", "QUST", "Quest", alias(three));
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, master_key());
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
  for (const auto& row : result.subrecords)
    if (row.signature == fo4::Signature::from_string("KSIZ") || row.signature == fo4::Signature::from_string("COCT")) {
      EXPECT_EQ(row.category, fo4::ConflictCategory::benign);
    }
  const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string("QUST"),
      fo4::Signature::from_string("KSIZ"), fo4::ConflictPriority::normal}};
  EXPECT_EQ(classify(order, master_key(), normal).category, fo4::ConflictCategory::conflict);
}

TEST(ConflictKind, AmbiguousAliasCountsKeepNormalPriority) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view three{"\x03\0\0\0", 4};
  const auto base = base_plugin("QUST", "Quest", {{"ANAM", two}, {"ALST", one}, {"KSIZ", one}, {"ALED", {}}});
  for (const auto& subs : std::vector<std::vector<Sub>>{
           {{"ANAM", two}, {"ALST", one}, {"KSIZ", two}},
           {{"ANAM", two}, {"ALLS", one}, {"KSIZ", two}, {"ALED", {}}},
           {{"ANAM", two}, {"ALST", two}, {"KSIZ", two}, {"ALED", {}}},
           {{"KSIZ", two}, {"ANAM", two}, {"ALST", one}, {"ALED", {}}}}) {
    const auto patch = override_plugin("Patch.esp", "QUST", "Quest", subs);
    const std::vector<const fo4::Plugin*> order{&base, &patch};
    const auto result = classify(order, master_key());
    for (const auto& row : result.subrecords)
      if (row.signature == fo4::Signature::from_string("KSIZ")) {
        EXPECT_EQ(row.category, fo4::ConflictCategory::override_);
      }
  }
  const auto two_aliases = base_plugin("QUST", "Quest", {{"ANAM", three},
      {"ALST", one}, {"KSIZ", one}, {"ALED", {}}, {"ALST", two}, {"KSIZ", one}, {"ALED", {}}});
  const auto shifted = override_plugin("Shifted.esp", "QUST", "Quest", {{"ANAM", three},
      {"ALST", one}, {"ALED", {}}, {"ALST", two}, {"KSIZ", two}, {"ALED", {}}});
  const std::vector<const fo4::Plugin*> shifted_order{&two_aliases, &shifted};
  const auto shifted_result = classify(shifted_order, master_key());
  for (const auto& row : shifted_result.subrecords)
    if (row.signature == fo4::Signature::from_string("KSIZ")) {
      EXPECT_EQ(row.category, fo4::ConflictCategory::override_);
    }
}

TEST(ConflictKind, CollectionDuplicateAliasesAndDuplicateCountsRefuseScopedPriority) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  const auto base = base_plugin("QUST", "Quest", {{"ANAM", two}, {"ALST", one}, {"KSIZ", one}, {"ALED", {}}});
  for (const auto& subs : std::vector<std::vector<Sub>>{
           {{"ANAM", two}, {"ALCS", one}, {"KSIZ", two}},
           {{"ANAM", two}, {"ALST", one}, {"KSIZ", two}, {"ALED", {}},
            {"ALST", one}, {"KSIZ", two}, {"ALED", {}}},
           {{"ANAM", two}, {"ALST", one}, {"KSIZ", two}, {"KSIZ", two}, {"ALED", {}}}}) {
    const auto patch = override_plugin("Patch.esp", "QUST", "Quest", subs);
    const std::vector<const fo4::Plugin*> order{&base, &patch};
    const auto result = classify(order, master_key());
    const auto target = std::find_if(result.subrecords.begin(), result.subrecords.end(), [](const auto& row) {
      return row.signature == fo4::Signature::from_string("KSIZ") && row.ordinal == 0;
    });
    ASSERT_NE(target, result.subrecords.end());
    EXPECT_EQ(target->category, fo4::ConflictCategory::override_);
    EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  }
}

TEST(ConflictKind, PartialAliasDoesNotPreventStableFullAliasesUsingScopedPriority) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  const auto base = base_plugin("QUST", "Quest", {{"ANAM", two}, {"ALST", one}, {"KSIZ", one}, {"ALED", {}}});
  const auto partial = fo4::read_plugin(make_plugin({{"QUST", 0x800, "Quest",
      {{"ANAM", two}, {"ALCS", one}, {"KSIZ", two}}, 0x4000}}, true), "Partial.esp");
  const auto patch = override_plugin("Patch.esp", "QUST", "Quest", {{"ANAM", two}, {"ALST", one}, {"KSIZ", two}, {"ALED", {}}});
  const std::vector<const fo4::Plugin*> order{&base, &partial, &patch};
  const auto result = classify(order, master_key());
  const auto target = std::find_if(result.subrecords.begin(), result.subrecords.end(), [](const auto& row) {
    return row.signature == fo4::Signature::from_string("KSIZ");
  });
  ASSERT_NE(target, result.subrecords.end());
  EXPECT_EQ(target->category, fo4::ConflictCategory::benign);
  EXPECT_EQ(target->roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::ignored, fo4::ConflictRole::benign}));
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::ignored, fo4::ConflictRole::benign}));
}

TEST(ConflictKind, RootIdleAnimationCountIsBenignOnlyInIdleMarkersAndPackages) {
  constexpr std::string_view one{"\x01", 1}, two{"\x02", 1}, three{"\x03", 1};
  for (const auto record : {"IDLM", "PACK"}) {
    const auto base = base_plugin(record, "Idle", {{"IDLC", one}});
    const auto middle = override_plugin("Middle.esp", record, "Idle", {{"IDLC", two}});
    const auto patch = override_plugin("Patch.esp", record, "Idle", {{"IDLC", three}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record;
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record;
    const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string("IDLC"), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), normal).category, fo4::ConflictCategory::conflict) << record;
  }
  const auto base = base_plugin("MISC", "Thing", {{"IDLC", one}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"IDLC", two}});
  const std::vector<const fo4::Plugin*> other_order{&base, &patch};
  EXPECT_EQ(classify(other_order, master_key()).category, fo4::ConflictCategory::override_);
  const auto base_idle = base_plugin("IDLM", "Idle", {{"IDLC", one}, {"PNAM", k_value_a}});
  const auto changed = override_plugin("Changed.esp", "IDLM", "Idle", {{"IDLC", two}, {"PNAM", k_value_b}});
  const std::vector<const fo4::Plugin*> mixed_order{&base_idle, &changed};
  EXPECT_EQ(classify(mixed_order, master_key()).category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, RootObjectTemplateCountIsBenignOnlyInDocumentedRecords) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view three{"\x03\0\0\0", 4};
  for (const auto record : {"ARMO", "FURN", "NPC_", "WEAP"}) {
    const auto base = base_plugin(record, "Item", {{"OBTE", one}});
    const auto middle = override_plugin("Middle.esp", record, "Item", {{"OBTE", two}});
    const auto patch = override_plugin("Patch.esp", record, "Item", {{"OBTE", three}});
    const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
    const auto result = classify(order, master_key());
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record;
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record;
    const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string("OBTE"), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), normal).category, fo4::ConflictCategory::conflict) << record;
  }
  const auto base = base_plugin("MISC", "Thing", {{"OBTE", one}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"OBTE", two}});
  const std::vector<const fo4::Plugin*> other_order{&base, &patch};
  EXPECT_EQ(classify(other_order, master_key()).category, fo4::ConflictCategory::override_);
  const auto base_armor = base_plugin("ARMO", "Armor", {{"OBTE", one}, {"FULL", std::string_view{"Old\0", 4}}});
  const auto changed = override_plugin("Changed.esp", "ARMO", "Armor", {{"OBTE", two}, {"FULL", std::string_view{"New\0", 4}}});
  const std::vector<const fo4::Plugin*> mixed_order{&base_armor, &changed};
  const auto mixed = classify(mixed_order, master_key());
  EXPECT_EQ(mixed.category, fo4::ConflictCategory::override_);
  EXPECT_EQ(mixed.roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::override_}));
}

TEST(ConflictKind, NpcActorSoundCountIsBenignWithExplicitPrecedenceAndScope) {
  constexpr std::string_view one{"\x01\0\0\0", 4};
  constexpr std::string_view two{"\x02\0\0\0", 4};
  constexpr std::string_view three{"\x03\0\0\0", 4};
  const auto base = base_plugin("NPC_", "Actor", {{"CS2H", one}, {"CS2E", {}}});
  const auto middle = override_plugin("Middle.esp", "NPC_", "Actor", {{"CS2H", two}, {"CS2E", {}}});
  const auto patch = override_plugin("Patch.esp", "NPC_", "Actor", {{"CS2H", three}, {"CS2E", {}}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, master_key());
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
  const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string("NPC_"),
      fo4::Signature::from_string("CS2H"), fo4::ConflictPriority::normal}};
  EXPECT_EQ(classify(order, master_key(), normal).category, fo4::ConflictCategory::conflict);
  const auto other_base = base_plugin("MISC", "Thing", {{"CS2H", one}});
  const auto other_patch = override_plugin("Other.esp", "MISC", "Thing", {{"CS2H", two}});
  const std::vector<const fo4::Plugin*> other_order{&other_base, &other_patch};
  EXPECT_EQ(classify(other_order, master_key()).category, fo4::ConflictCategory::override_);
  const auto renamed = override_plugin("Renamed.esp", "NPC_", "OtherActor", {{"CS2H", two}, {"CS2E", {}}});
  const std::vector<const fo4::Plugin*> mixed_order{&base, &renamed};
  const auto mixed = classify(mixed_order, master_key());
  EXPECT_EQ(mixed.category, fo4::ConflictCategory::override_);
  EXPECT_EQ(mixed.roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::override_}));
}

TEST(ConflictKind, PlacedLocationReferenceIsBenignOnlyWhenAdded) {
  for (const auto record : {"ACHR", "REFR", "PARW", "PBAR", "PBEA", "PCON", "PFLA", "PGRE", "PHZD", "PMIS"}) {
    const auto base = base_plugin(record, "Placed", {});
    const auto patch = override_plugin("Patch.esp", record, "Placed", {{"XLRL", k_value_a}});
    const std::vector<const fo4::Plugin*> order{&base, &patch};
    const auto result = classify(order, master_key());
    ASSERT_EQ(result.subrecords.size(), 2u);
    EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign) << record;
    EXPECT_EQ(result.category, fo4::ConflictCategory::benign) << record;
    const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string("XLRL"), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), normal).category, fo4::ConflictCategory::override_) << record;
    const auto existing = base_plugin(record, "Placed", {{"XLRL", k_value_b}});
    const std::vector<const fo4::Plugin*> existing_order{&existing, &patch};
    EXPECT_EQ(classify(existing_order, master_key()).category, fo4::ConflictCategory::override_) << record;
  }
}

TEST(ConflictKind, IntermediateValueMatchingWinnerIsBenignWhenAdded) {
  const auto base = base_plugin("REFR", "Placed", {});
  const auto first = override_plugin("First.esp", "REFR", "Placed", {{"XLRL", k_value_a}});
  const auto middle = override_plugin("Middle.esp", "REFR", "Placed", {{"XLRL", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "REFR", "Placed", {{"XLRL", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &first, &middle, &patch};
  const auto result = classify(order, master_key());
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.subrecords[1].roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::unknown,
      fo4::ConflictRole::loses, fo4::ConflictRole::identical_to_master, fo4::ConflictRole::identical_to_master}));
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
}

TEST(ConflictKind, IntermediateValueMatchingWinnerIsAnOverrideRatherThanLoss) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, master_key());
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::override_, fo4::ConflictRole::override_}));
}

TEST(ConflictKind, ConditionalPriorityUsesLastNonpartialElementButNotMissingFinalElement) {
  const auto base = fo4::read_plugin(make_plugin({{"QUST", 0x900, "Quest", {}}}, false), "Master.esm");
  const auto first = fo4::read_plugin(make_plugin({{"QUST", 0x900, "Quest", {{"MODE", k_value_a}}}}, true), "First.esp");
  const auto same = fo4::read_plugin(make_plugin({{"QUST", 0x900, "Quest", {{"MODE", k_value_a}}}}, true), "Same.esp");
  const auto partial = fo4::read_plugin(make_plugin({{"QUST", 0x900, "Quest", {}, 0x4000}}, true), "Partial.esp");
  const auto missing = fo4::read_plugin(make_plugin({{"QUST", 0x900, "Quest", {}}}, true), "Missing.esp");
  const fo4::ConflictPriorityRule rules[] = {{fo4::Signature::from_string("QUST"), k_mode, fo4::ConflictPriority::benign_if_added}};
  const std::vector<const fo4::Plugin*> partial_order{&base, &first, &same, &partial};
  const auto partial_result = classify(partial_order, quest_key(), rules);
  ASSERT_EQ(partial_result.subrecords.size(), 2u);
  EXPECT_EQ(partial_result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(partial_result.subrecords[1].roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::unknown,
      fo4::ConflictRole::identical_to_master, fo4::ConflictRole::identical_to_master, fo4::ConflictRole::ignored}));
  const std::vector<const fo4::Plugin*> missing_order{&base, &first, &missing};
  const auto missing_result = classify(missing_order, quest_key(), rules);
  ASSERT_EQ(missing_result.subrecords.size(), 2u);
  EXPECT_EQ(missing_result.subrecords[1].category, fo4::ConflictCategory::conflict);
  EXPECT_EQ(missing_result.subrecords[1].roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::unknown,
      fo4::ConflictRole::loses, fo4::ConflictRole::identical_to_master_wins}));
}

TEST(ConflictKind, FormListEditorIdUsesBenignPriorityOnlyForFormLists) {
  const auto base = base_plugin("FLST", "ListA", {});
  const auto middle = override_plugin("Middle.esp", "FLST", "ListB", {});
  const auto patch = override_plugin("Patch.esp", "FLST", "ListC", {});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  EXPECT_EQ(classify(order, master_key()).category, fo4::ConflictCategory::benign);
  const auto normal_base = base_plugin("MISC", "ThingA", {});
  const auto normal_middle = override_plugin("Middle.esp", "MISC", "ThingB", {});
  const auto normal_patch = override_plugin("Patch.esp", "MISC", "ThingC", {});
  const std::vector<const fo4::Plugin*> normal_order{&normal_base, &normal_middle, &normal_patch};
  EXPECT_EQ(classify(normal_order, master_key()).category, fo4::ConflictCategory::conflict);
}

TEST(ConflictKind, IgnoredInfoCountDoesNotHideHeaderChangesOrApplyToOtherRecords) {
  const auto base = base_plugin("DIAL", "Topic", {{"TIFC", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "DIAL", "Topic", {{"TIFC", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  EXPECT_EQ(classify(order, master_key()).category, fo4::ConflictCategory::no_conflict);
  const auto changed_flags = fo4::read_plugin(
      make_plugin({{"DIAL", 0x800, "Topic", {{"TIFC", k_value_b}}, 0x4}}, true), "Flags.esp");
  const std::vector<const fo4::Plugin*> flags_order{&base, &changed_flags};
  EXPECT_EQ(classify(flags_order, master_key()).category, fo4::ConflictCategory::override_);
  const auto other_base = base_plugin("MISC", "Thing", {{"TIFC", k_value_a}});
  const auto other_patch = override_plugin("Other.esp", "MISC", "Thing", {{"TIFC", k_value_b}});
  const std::vector<const fo4::Plugin*> other_order{&other_base, &other_patch};
  EXPECT_EQ(classify(other_order, master_key()).category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, DefaultIgnoredSubrecordsCanBeRestoredByCaller) {
  for (const auto& [record, member] : std::vector<std::pair<std::string_view, std::string_view>>{
           {"TACT", "PNAM"}, {"TACT", "FNAM"}, {"CSTY", "CSMD"}, {"DIAL", "TIFC"},
           {"PROJ", "DATA"}, {"PERK", "PRKF"}, {"MESG", "INAM"}, {"MATO", "DNAM"},
           {"REFR", "XMBP"}, {"WRLD", "NNAM"}}) {
    const auto base = base_plugin(record, "Thing", {{member, k_value_a}});
    const auto patch = override_plugin("Patch.esp", record, "Thing", {{member, k_value_b}});
    const std::vector<const fo4::Plugin*> order{&base, &patch};
    const auto result = classify(order, master_key());
    EXPECT_EQ(result.category, fo4::ConflictCategory::no_conflict) << record << member;
    ASSERT_EQ(result.subrecords.size(), 1u);
    const fo4::ConflictPriorityRule rule[] = {{fo4::Signature::from_string(record),
        fo4::Signature::from_string(member), fo4::ConflictPriority::normal}};
    EXPECT_EQ(classify(order, master_key(), rule).category, fo4::ConflictCategory::override_) << record << member;
  }
}

TEST(ConflictKind, NormalChangesAndOrderStillRaiseRecordsAboveBenignCounts) {
  const auto base = base_plugin("LVLI", "List", {{"LLCT", k_value_c}, {"DATA", k_value_a}, {"FULL", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "LVLI", "List", {{"LLCT", k_value_b}, {"DATA", k_value_b}, {"FULL", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  EXPECT_EQ(classify(order, master_key()).category, fo4::ConflictCategory::override_);
  const auto reordered = override_plugin("Reordered.esp", "LVLI", "List", {{"FULL", k_value_a}, {"DATA", k_value_a}, {"LLCT", k_value_c}});
  const std::vector<const fo4::Plugin*> reordered_order{&base, &reordered};
  EXPECT_EQ(classify(reordered_order, master_key()).category, fo4::ConflictCategory::override_);
  const auto no_count = base_plugin("LVLI", "List", {{"DATA", k_value_a}, {"FULL", k_value_a}});
  const auto added_and_reordered = override_plugin("Reordered.esp", "LVLI", "List",
      {{"FULL", k_value_a}, {"DATA", k_value_a}, {"LLCT", k_value_c}});
  const std::vector<const fo4::Plugin*> expanded_order{&no_count, &added_and_reordered};
  EXPECT_EQ(classify(expanded_order, master_key()).category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, GameSettingEditorIdConflictsAreCriticalByDefault) {
  const auto base = base_plugin("GMST", "SettingA", {{"DATA", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "GMST", "SettingB", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());
  ASSERT_FALSE(result.subrecords.empty());
  EXPECT_EQ(result.subrecords[0].category, fo4::ConflictCategory::critical);
  EXPECT_EQ(result.category, fo4::ConflictCategory::critical);
}

TEST(ConflictKind, RepeatedSubrecordsAreAlignedByOrdinal) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}, {"DATA", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing",
                                     {{"DATA", k_value_a}, {"DATA", k_value_c}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[0].signature, k_edid);
  EXPECT_EQ(result.subrecords[1].signature, k_data);
  EXPECT_EQ(result.subrecords[1].ordinal, 0u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::no_conflict);
  EXPECT_EQ(result.subrecords[2].signature, k_data);
  EXPECT_EQ(result.subrecords[2].ordinal, 1u);
  EXPECT_EQ(result.subrecords[2].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.subrecords[2].roles[1], fo4::ConflictRole::override_);
}

TEST(ConflictKind, RemovingRepeatedMemberSurvivesParentAggregation) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}, {"DATA", k_value_b}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[2].signature, k_data);
  EXPECT_EQ(result.subrecords[2].ordinal, 1u);
  EXPECT_EQ(result.subrecords[2].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::override_);
}

TEST(ConflictKind, APartialFormVersionContributesNoValue) {
  const auto base = fo4::read_plugin(
      make_plugin({{"QUST", 0x900, "Quest", {{"DATA", k_value_a}}}}, false), "Master.esm");
  const auto partial = fo4::read_plugin(
      make_plugin({{"QUST", 0x900, "Quest", {{"DATA", k_value_b}}, 0x4000}}, true), "Middle.esp");
  const auto patch = fo4::read_plugin(
      make_plugin({{"QUST", 0x900, "Quest", {{"DATA", k_value_a}}}}, true), "Patch.esp");
  const std::vector<const fo4::Plugin*> order{&base, &partial, &patch};
  const auto result = classify(order, quest_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::no_conflict);
  ASSERT_EQ(result.roles.size(), 3u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::ignored);
  EXPECT_EQ(result.roles[2], fo4::ConflictRole::identical_to_master);
}

TEST(ConflictKind, PartialMiddleVersionStaysIgnoredWhenARealOverrideChanges) {
  const auto base = fo4::read_plugin(
      make_plugin({{"QUST", 0x900, "Quest", {{"DATA", k_value_a}}}}, false), "Master.esm");
  const auto partial = fo4::read_plugin(
      make_plugin({{"QUST", 0x900, "Quest", {{"DATA", k_value_c}}, 0x4000}}, true), "Middle.esp");
  const auto patch = fo4::read_plugin(
      make_plugin({{"QUST", 0x900, "Quest", {{"DATA", k_value_b}}}}, true), "Patch.esp");
  const std::vector<const fo4::Plugin*> order{&base, &partial, &patch};
  const auto result = classify(order, quest_key());
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::master,
      fo4::ConflictRole::ignored, fo4::ConflictRole::override_}));
}

TEST(ConflictKind, AKeyNoPluginDefinesIsUnknown) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base};
  const auto result = classify(order, fo4::FormKey::parse("000900:Master.esm"));

  EXPECT_EQ(result.category, fo4::ConflictCategory::unknown);
  EXPECT_TRUE(result.roles.empty());
  EXPECT_TRUE(result.subrecords.empty());
}

TEST(ConflictKind, BulkClassificationCoversEveryKeyIncludingSinglePluginOnes) {
  const auto base = fo4::read_plugin(
      make_plugin({{"MISC", 0x800, "Thing", {{"DATA", k_value_a}}},
                   {"MISC", 0x900, "Solo", {{"DATA", k_value_b}}}},
                  false),
      "Master.esm");
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DATA", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto graph = fo4::ConflictGraph::build(order);

  const auto results = fo4::classify_conflicts(graph, order);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].key, master_key());
  EXPECT_EQ(results[0].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(results[1].key, fo4::FormKey::parse("000900:Master.esm"));
  EXPECT_EQ(results[1].category, fo4::ConflictCategory::only_one);
  ASSERT_EQ(results[1].roles.size(), 1u);
  EXPECT_EQ(results[1].roles[0], fo4::ConflictRole::only_one);
}

const fo4::FieldConflict* find_field(const fo4::RecordConflict& conflict, std::string_view path) {
  for (const auto& field : conflict.fields) {
    if (field.path == path) return &field;
  }
  return nullptr;
}

TEST(ConflictKind, ScalarFieldRowsSplitAChangedSubrecord) {
  const auto base = base_plugin("MISC", "Thing", {{"DEST", k_dest_health_100}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DEST", k_dest_health_200}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);

  const auto* health = find_field(result, "MISC\\DestructionHealth");
  ASSERT_NE(health, nullptr);
  EXPECT_EQ(health->category, fo4::ConflictCategory::override_);
  ASSERT_EQ(health->roles.size(), 2u);
  EXPECT_EQ(health->roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(health->roles[1], fo4::ConflictRole::override_);

  const auto* stages = find_field(result, "MISC\\DestructionStageCount");
  ASSERT_NE(stages, nullptr);
  EXPECT_EQ(stages->category, fo4::ConflictCategory::no_conflict);
  ASSERT_EQ(stages->roles.size(), 2u);
  EXPECT_EQ(stages->roles[1], fo4::ConflictRole::identical_to_master);

  EXPECT_EQ(find_field(result, "MISC\\DestructionResistanceValue"), nullptr);
}

TEST(ConflictKind, UndecodableScalarRowsAreIgnoredWithoutHidingRawConflicts) {
  constexpr std::string_view short_a{"\x64\0\0\0\x01\0\0", 7};
  constexpr std::string_view short_b{"\x65\0\0\0\x01\0\0", 7};
  const auto base = base_plugin("MISC", "Thing", {{"DEST", short_a}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DEST", k_dest_health_200}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DEST", short_b}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const fo4::ConflictPriorityRule rules[] = {{k_misc, fo4::Signature::from_string("DEST"), fo4::ConflictPriority::override_}};
  const auto result = classify(order, master_key(), rules);
  const auto* health = find_field(result, "MISC\\DestructionHealth");
  ASSERT_NE(health, nullptr);
  EXPECT_EQ(health->category, fo4::ConflictCategory::only_one);
  EXPECT_EQ(health->roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::ignored,
      fo4::ConflictRole::only_one, fo4::ConflictRole::ignored}));
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, TrulyAbsentScalarRemainsDifferentFromAnUnavailableScalar) {
  const auto base = base_plugin("MISC", "Thing", {});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DEST", k_dest_health_200}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, master_key());
  const auto* health = find_field(result, "MISC\\DestructionHealth");
  ASSERT_NE(health, nullptr);
  EXPECT_EQ(health->category, fo4::ConflictCategory::override_);
  EXPECT_EQ(health->roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::unknown,
      fo4::ConflictRole::override_}));
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
}

TEST(ConflictKind, AnUndecodableMasterIsNotReplacedByAnOverrideAsFieldMaster) {
  constexpr std::string_view short_data{"\x64\0\0\0\x01\0\0", 7};
  const auto base = base_plugin("MISC", "Thing", {{"DEST", short_data}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DEST", k_dest_health_100}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DEST", k_dest_health_200}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, master_key());
  const auto* health = find_field(result, "MISC\\DestructionHealth");
  ASSERT_NE(health, nullptr);
  EXPECT_EQ(health->category, fo4::ConflictCategory::unknown);
  EXPECT_EQ(health->roles, (std::vector<fo4::ConflictRole>{fo4::ConflictRole::ignored,
      fo4::ConflictRole::unknown, fo4::ConflictRole::unknown}));
  EXPECT_EQ(result.category, fo4::ConflictCategory::conflict);
}

TEST(ConflictKind, AvailableScalarConflictRaisesItsOverridePriorityParent) {
  constexpr std::string_view restored_health{"\x64\0\0\0\x02\0\0\0", 8};
  const auto base = base_plugin("MISC", "Thing", {{"DEST", k_dest_health_100}});
  const auto middle = override_plugin("Middle.esp", "MISC", "Thing", {{"DEST", k_dest_health_200}});
  const auto patch = override_plugin("Patch.esp", "MISC", "Thing", {{"DEST", restored_health}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const fo4::ConflictPriorityRule rules[] = {{k_misc, fo4::Signature::from_string("DEST"), fo4::ConflictPriority::override_}};
  const auto result = classify(order, master_key(), rules);
  const auto* health = find_field(result, "MISC\\DestructionHealth");
  ASSERT_NE(health, nullptr);
  EXPECT_EQ(health->category, fo4::ConflictCategory::conflict);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.category, fo4::ConflictCategory::conflict);
}

TEST(ConflictKind, AnInjectedRecordIsPromotedToCritical) {
  const auto base = base_plugin("MISC", "Thing", {{"DATA", k_value_a}});
  const auto middle = fo4::read_plugin(
      make_plugin({{"MISC", 0x900, "Thing", {{"DATA", k_value_a}}}}, true), "Middle.esp");
  const auto patch = fo4::read_plugin(
      make_plugin({{"MISC", 0x900, "Thing", {{"DATA", k_value_b}}}}, true), "Patch.esp");
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order, fo4::FormKey::parse("000900:Master.esm"));

  EXPECT_EQ(result.category, fo4::ConflictCategory::critical);
  ASSERT_EQ(result.roles.size(), 2u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::wins);
}

TEST(ConflictKind, InjectedRecordRetainsBenignCountPriority) {
  constexpr std::string_view one{"\x01", 1}, two{"\x02", 1};
  const auto owner = base_plugin("MISC", "Other", {{"DATA", k_value_a}});
  const auto middle = fo4::read_plugin(
      make_plugin({{"LVLI", 0x900, "List", {{"LLCT", one}}}}, true), "Middle.esp");
  const auto patch = fo4::read_plugin(
      make_plugin({{"LVLI", 0x900, "List", {{"LLCT", two}}}}, true), "Patch.esp");
  const std::vector<const fo4::Plugin*> order{&owner, &middle, &patch};
  const auto result = classify(order, fo4::FormKey::parse("000900:Master.esm"));
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
  ASSERT_EQ(result.roles.size(), 2u);
  EXPECT_EQ(result.roles[0], fo4::ConflictRole::master);
  EXPECT_EQ(result.roles[1], fo4::ConflictRole::benign);
}

TEST(ConflictKind, ARecordItsOwnerDefinesIsNotInjected) {
  const auto base = fo4::read_plugin(
      make_plugin({{"MISC", 0x900, "Thing", {{"DATA", k_value_a}}}}, false), "Master.esm");
  const auto patch = fo4::read_plugin(
      make_plugin({{"MISC", 0x900, "Thing", {{"DATA", k_value_b}}}}, true), "Patch.esp");
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, fo4::FormKey::parse("000900:Master.esm"));

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
}
