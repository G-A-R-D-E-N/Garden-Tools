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
constexpr std::string_view k_first{"\x01\0\0\0", 4};
constexpr std::string_view k_second{"\x02\0\0\0", 4};

struct Sub {
  std::string_view signature;
  std::string_view data;
};

std::vector<std::byte> make_plugin(std::string_view signature,
                                   std::string_view editor_id,
                                   const std::vector<Sub>& subs,
                                   bool with_master) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1u);
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

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x800);
  std::vector<std::byte> edid;
  for (const char ch : editor_id) edid.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  edid.push_back(std::byte{0});
  fo4::test::write_subrecord(out, "EDID", edid);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::Plugin base_plugin(std::string_view signature,
                        std::string_view editor_id,
                        std::vector<Sub> subs) {
  return fo4::read_plugin(make_plugin(signature, editor_id, subs, false), "Master.esm");
}

fo4::Plugin override_plugin(std::string_view name,
                            std::string_view signature,
                            std::string_view editor_id,
                            std::vector<Sub> subs) {
  return fo4::read_plugin(make_plugin(signature, editor_id, subs, true), name);
}

fo4::FormKey master_key() { return fo4::FormKey::parse("000800:Master.esm"); }

fo4::RecordConflict classify(std::span<const fo4::Plugin* const> order,
                             std::span<const fo4::ConflictPriorityRule> rules = {}) {
  const auto graph = fo4::ConflictGraph::build(order);
  return fo4::classify_record_conflict(
      graph, fo4::ConflictQuery{order, master_key(), fo4::ConflictPriority::normal, rules});
}

constexpr std::string_view k_omod_a{"\x01\0\0\0\x02\0\0\0" "\0\0\0\0\0\0\0\0\0\0\0\0", 20};
constexpr std::string_view k_omod_include{"\x02\0\0\0\x02\0\0\0" "\0\0\0\0\0\0\0\0\0\0\0\0", 20};
constexpr std::string_view k_omod_property{"\x01\0\0\0\x03\0\0\0" "\0\0\0\0\0\0\0\0\0\0\0\0", 20};

const fo4::FieldConflict* find_field(const fo4::RecordConflict& result, std::string_view path) {
  const auto found = std::find_if(result.fields.begin(), result.fields.end(),
                                  [&](const fo4::FieldConflict& field) { return field.path == path; });
  return found == result.fields.end() ? nullptr : &*found;
}

}  // namespace

TEST(ConflictPriorityGenerated, GeneratedBenignSubrecordIsBenignUntilCallerOverrides) {
  const auto base = base_plugin("WEAP", "Thing", {{"MO4T", k_first}});
  const auto middle = override_plugin("Middle.esp", "WEAP", "Thing", {{"MO4T", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "WEAP", "Thing", {{"MO4T", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].signature, fo4::Signature::from_string("MO4T"));
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);

  const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string("WEAP"),
                                               fo4::Signature::from_string("MO4T"),
                                               fo4::ConflictPriority::normal}};
  EXPECT_EQ(classify(order, normal).category, fo4::ConflictCategory::conflict);
}

TEST(ConflictPriorityGenerated, GeneratedIgnoreSubrecordDoesNotCountAsADifference) {
  const auto base = base_plugin("WRLD", "Thing", {{"CLSZ", k_value_a}});
  const auto patch = override_plugin("Patch.esp", "WRLD", "Thing", {{"CLSZ", k_value_b}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order);
  EXPECT_EQ(result.category, fo4::ConflictCategory::no_conflict);
  ASSERT_EQ(result.subrecords.size(), 1u);
  EXPECT_EQ(result.subrecords[0].signature, fo4::Signature::from_string("EDID"));
}

TEST(ConflictPriorityGenerated, GeneratedCriticalEditorIdIsCriticalOnItsRow) {
  const auto base = base_plugin("GMST", "SettingA", {});
  const auto middle = override_plugin("Middle.esp", "GMST", "SettingB", {});
  const auto patch = override_plugin("Patch.esp", "GMST", "SettingA", {});
  const std::vector<const fo4::Plugin*> order{&base, &middle, &patch};
  const auto result = classify(order);
  EXPECT_EQ(result.category, fo4::ConflictCategory::critical);
  ASSERT_FALSE(result.subrecords.empty());
  EXPECT_EQ(result.subrecords[0].signature, fo4::Signature::from_string("EDID"));
  EXPECT_EQ(result.subrecords[0].category, fo4::ConflictCategory::critical);
}

TEST(ConflictPriorityGenerated, GeneratedBenignIfAddedIsBenignOnlyWhenAdded) {
  const auto base = base_plugin("PCON", "Placed", {});
  const auto patch = override_plugin("Patch.esp", "PCON", "Placed", {{"XLRL", k_value_a}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  EXPECT_EQ(classify(order).category, fo4::ConflictCategory::benign);
}

TEST(ConflictPriorityGenerated, ExcludedHandScopedPairKeepsNormalPriority) {
  const auto base = base_plugin("PACK", "Thing", {{"CITC", k_first}});
  const auto patch = override_plugin("Patch.esp", "PACK", "Thing", {{"CITC", k_second}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  EXPECT_EQ(classify(order).category, fo4::ConflictCategory::override_);
}

TEST(ConflictPriorityGenerated, GeneratedFieldRuleMakesOmodIncludeCountBenignRow) {
  const auto base = base_plugin("OMOD", "Thing", {{"DATA", k_omod_a}});
  const auto patch = override_plugin("Patch.esp", "OMOD", "Thing", {{"DATA", k_omod_include}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order);
  const auto* field = find_field(result, "OMOD\\IncludeCount");
  ASSERT_NE(field, nullptr);
  EXPECT_EQ(field->category, fo4::ConflictCategory::benign);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].signature, fo4::Signature::from_string("DATA"));
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
}

TEST(ConflictPriorityGenerated, OmodFieldRuleDoesNotCoverAnotherDifferingByte) {
  constexpr std::string_view k_include_and_bool{"\x02\0\0\0\x02\0\0\0\x01" "\0\0\0\0\0\0\0\0\0\0\0", 20};
  const auto base = base_plugin("OMOD", "Thing", {{"DATA", k_omod_a}});
  const auto patch = override_plugin("Patch.esp", "OMOD", "Thing", {{"DATA", k_include_and_bool}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::override_);
  EXPECT_EQ(result.category, fo4::ConflictCategory::override_);
  const auto* field = find_field(result, "OMOD\\IncludeCount");
  ASSERT_NE(field, nullptr);
  EXPECT_EQ(field->category, fo4::ConflictCategory::benign);
}

TEST(ConflictPriorityGenerated, OmodFieldRulesCoverEveryDifferingByteOfTheSubrecord) {
  constexpr std::string_view k_include_and_property{"\x02\0\0\0\x03\0\0\0" "\0\0\0\0\0\0\0\0\0\0\0\0", 20};
  const auto base = base_plugin("OMOD", "Thing", {{"DATA", k_omod_a}});
  const auto patch = override_plugin("Patch.esp", "OMOD", "Thing", {{"DATA", k_include_and_property}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order);
  ASSERT_EQ(result.subrecords.size(), 2u);
  EXPECT_EQ(result.subrecords[1].category, fo4::ConflictCategory::benign);
  EXPECT_EQ(result.category, fo4::ConflictCategory::benign);
}

TEST(ConflictPriorityGenerated, GeneratedFieldRuleMakesOmodPropertyCountBenignRow) {
  const auto base = base_plugin("OMOD", "Thing", {{"DATA", k_omod_a}});
  const auto patch = override_plugin("Patch.esp", "OMOD", "Thing", {{"DATA", k_omod_property}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const auto result = classify(order);
  const auto* field = find_field(result, "OMOD\\PropertyCount");
  ASSERT_NE(field, nullptr);
  EXPECT_EQ(field->category, fo4::ConflictCategory::benign);
}

TEST(ConflictPriorityGenerated, CallerSubrecordRulesStillWinOverGeneratedFieldRule) {
  const auto base = base_plugin("OMOD", "Thing", {{"DATA", k_omod_a}});
  const auto patch = override_plugin("Patch.esp", "OMOD", "Thing", {{"DATA", k_omod_include}});
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  const fo4::ConflictPriorityRule normal[] = {{fo4::Signature::from_string("OMOD"),
                                               fo4::Signature::from_string("DATA"),
                                               fo4::ConflictPriority::normal}};
  const auto kept_result = classify(order, normal);
  const auto* kept = find_field(kept_result, "OMOD\\IncludeCount");
  ASSERT_NE(kept, nullptr);
  EXPECT_EQ(kept->category, fo4::ConflictCategory::override_);

  const fo4::ConflictPriorityRule ignored[] = {{fo4::Signature::from_string("OMOD"),
                                                fo4::Signature::from_string("DATA"),
                                                fo4::ConflictPriority::ignore}};
  const auto hidden = classify(order, ignored);
  EXPECT_EQ(find_field(hidden, "OMOD\\IncludeCount"), nullptr);
  EXPECT_EQ(hidden.category, fo4::ConflictCategory::no_conflict);
}
