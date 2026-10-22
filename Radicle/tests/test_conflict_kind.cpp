#include "fo4/graph/conflict_kind.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

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

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  ASSERT_EQ(result.subrecords.size(), 3u);
  EXPECT_EQ(result.subrecords[2].signature, k_mode);
  EXPECT_EQ(result.subrecords[2].category, fo4::ConflictCategory::benign);
  ASSERT_EQ(result.subrecords[2].roles.size(), 2u);
  EXPECT_EQ(result.subrecords[2].roles[0], fo4::ConflictRole::unknown);
  EXPECT_EQ(result.subrecords[2].roles[1], fo4::ConflictRole::identical_to_master);
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
  EXPECT_EQ(result.category, fo4::ConflictCategory::conflict);
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

TEST(ConflictKind, ARecordItsOwnerDefinesIsNotInjected) {
  const auto base = fo4::read_plugin(
      make_plugin({{"MISC", 0x900, "Thing", {{"DATA", k_value_a}}}}, false), "Master.esm");
  const auto patch = fo4::read_plugin(
      make_plugin({{"MISC", 0x900, "Thing", {{"DATA", k_value_b}}}}, true), "Patch.esp");
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order, fo4::FormKey::parse("000900:Master.esm"));

  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
}
