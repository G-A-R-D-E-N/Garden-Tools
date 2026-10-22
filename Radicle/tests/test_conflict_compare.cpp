#include "fo4/graph/conflict_kind.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view k_value_a{"\xAA\xBB\0\0", 4};
constexpr std::string_view k_value_b{"\xCC\xDD\0\0", 4};
constexpr std::string_view k_value_c{"\x11\x22\0\0", 4};

const auto k_data = fo4::Signature::from_string("DATA");
const auto k_edid = fo4::Signature::from_string("EDID");
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

fo4::Plugin base_plugin(std::vector<Rec> records) {
  return fo4::read_plugin(make_plugin(records, false), "Master.esm");
}

fo4::Plugin override_plugin(std::string_view name, std::vector<Rec> records) {
  return fo4::read_plugin(make_plugin(records, true), name);
}

Rec misc(std::uint32_t form_id, std::string_view editor_id, std::vector<Sub> subs) {
  return Rec{"MISC", form_id, editor_id, std::move(subs), 0};
}

fo4::FormKey master_key() { return fo4::FormKey::parse("000800:Master.esm"); }

fo4::FormKey second_key() { return fo4::FormKey::parse("000801:Master.esm"); }

fo4::FormKey quest_key() { return fo4::FormKey::parse("000900:Master.esm"); }

std::vector<std::string> names(const fo4::FieldValueGroup& group) {
  std::vector<std::string> out;
  for (const auto& plugin : group.plugins) {
    out.push_back(std::string(plugin.filename()));
  }
  return out;
}

fo4::RecordComparison compare(std::span<const fo4::Plugin* const> order,
                              const fo4::FormKey& key,
                              std::span<const fo4::ConflictPriorityRule> rules = {}) {
  const auto graph = fo4::ConflictGraph::build(order);
  return fo4::compare_record_across_plugins(graph, order, key, rules);
}

const fo4::FieldComparison* field_of(const fo4::RecordComparison& comparison,
                                     fo4::Signature signature,
                                     std::size_t ordinal = 0) {
  for (const auto& field : comparison.fields) {
    if (field.signature == signature && field.ordinal == ordinal) return &field;
  }
  return nullptr;
}

}  // namespace

TEST(ConflictCompare, GroupsThePluginsThatAgreePerField) {
  const auto base = base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}})});
  const auto same = override_plugin("Same.esp", {misc(0x800, "Thing", {{"DATA", k_value_a}})});
  const auto other = override_plugin("Other.esp", {misc(0x800, "Thing", {{"DATA", k_value_b}})});
  const std::vector<const fo4::Plugin*> order{&base, &same, &other};

  const auto result = compare(order, master_key());

  ASSERT_EQ(result.record_groups.size(), 2u);
  EXPECT_EQ(names(result.record_groups[0]), (std::vector<std::string>{"Master.esm", "Same.esp"}));
  EXPECT_EQ(names(result.record_groups[1]), (std::vector<std::string>{"Other.esp"}));

  const auto* edid = field_of(result, k_edid);
  ASSERT_NE(edid, nullptr);
  ASSERT_EQ(edid->groups.size(), 1u);
  EXPECT_EQ(names(edid->groups[0]),
            (std::vector<std::string>{"Master.esm", "Same.esp", "Other.esp"}));

  const auto* data = field_of(result, k_data);
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->groups.size(), 2u);
  EXPECT_EQ(names(data->groups[0]), (std::vector<std::string>{"Master.esm", "Same.esp"}));
  EXPECT_EQ(names(data->groups[1]), (std::vector<std::string>{"Other.esp"}));
}

TEST(ConflictCompare, EveryPluginDiffersGetsItsOwnGroup) {
  const auto base = base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}})});
  const auto second = override_plugin("Second.esp", {misc(0x800, "Thing", {{"DATA", k_value_b}})});
  const auto third = override_plugin("Third.esp", {misc(0x800, "Thing", {{"DATA", k_value_c}})});
  const std::vector<const fo4::Plugin*> order{&base, &second, &third};

  const auto result = compare(order, master_key());

  ASSERT_EQ(result.record_groups.size(), 3u);
  const auto* data = field_of(result, k_data);
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->groups.size(), 3u);
  EXPECT_EQ(names(data->groups[0]), (std::vector<std::string>{"Master.esm"}));
  EXPECT_EQ(names(data->groups[1]), (std::vector<std::string>{"Second.esp"}));
  EXPECT_EQ(names(data->groups[2]), (std::vector<std::string>{"Third.esp"}));
}

TEST(ConflictCompare, RepeatedSubrecordsAreComparedPerOrdinal) {
  const auto base =
      base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}, {"DATA", k_value_b}})});
  const auto patch = override_plugin(
      "Patch.esp", {misc(0x800, "Thing", {{"DATA", k_value_a}, {"DATA", k_value_c}})});
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  const auto result = compare(order, master_key());

  const auto* first = field_of(result, k_data, 0);
  ASSERT_NE(first, nullptr);
  ASSERT_EQ(first->groups.size(), 1u);
  EXPECT_EQ(names(first->groups[0]), (std::vector<std::string>{"Master.esm", "Patch.esp"}));

  const auto* second = field_of(result, k_data, 1);
  ASSERT_NE(second, nullptr);
  ASSERT_EQ(second->groups.size(), 2u);
  EXPECT_EQ(names(second->groups[0]), (std::vector<std::string>{"Master.esm"}));
  EXPECT_EQ(names(second->groups[1]), (std::vector<std::string>{"Patch.esp"}));
}

TEST(ConflictCompare, AnIgnoredFieldDropsOutOfTheFieldRows) {
  const auto base = base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}})});
  const auto patch = override_plugin("Patch.esp", {misc(0x800, "Thing", {{"DATA", k_value_b}})});
  const std::vector<const fo4::Plugin*> order{&base, &patch};
  const std::vector<fo4::ConflictPriorityRule> rules{{k_misc, k_data, fo4::ConflictPriority::ignore}};

  const auto result = compare(order, master_key(), rules);

  EXPECT_EQ(field_of(result, k_data), nullptr);
  ASSERT_EQ(result.fields.size(), 1u);
  EXPECT_EQ(result.fields[0].signature, k_edid);
  ASSERT_EQ(result.record_groups.size(), 1u);
  EXPECT_EQ(names(result.record_groups[0]), (std::vector<std::string>{"Master.esm", "Patch.esp"}));
}

TEST(ConflictCompare, GroupsListOnlyThePluginsThatCarryTheVersion) {
  const auto base = base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}})});
  const auto other = override_plugin("Other.esp", {misc(0x800, "Thing", {{"DATA", k_value_b}})});
  const auto partial = override_plugin(
      "Partial.esp", {{"QUST", 0x800, "Thing", {{"DATA", k_value_c}}, 0x4000}});
  const std::vector<const fo4::Plugin*> order{&base, &other, &partial};

  const auto result = compare(order, master_key());

  ASSERT_EQ(result.record_groups.size(), 2u);
  EXPECT_EQ(names(result.record_groups[0]), (std::vector<std::string>{"Master.esm"}));
  EXPECT_EQ(names(result.record_groups[1]), (std::vector<std::string>{"Other.esp"}));
  const auto* data = field_of(result, k_data);
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->groups.size(), 2u);
  EXPECT_EQ(names(data->groups[0]), (std::vector<std::string>{"Master.esm"}));
  EXPECT_EQ(names(data->groups[1]), (std::vector<std::string>{"Other.esp"}));
}

TEST(ConflictCompare, AKeyNoPluginCarriesHasNoGroups) {
  const auto base = base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}})});
  const std::vector<const fo4::Plugin*> order{&base};

  const auto result = compare(order, quest_key());

  EXPECT_EQ(result.key, quest_key());
  EXPECT_TRUE(result.record_groups.empty());
  EXPECT_TRUE(result.fields.empty());
}

TEST(ConflictCompare, ComparesEveryConflictingKeyOfAnOrder) {
  const auto base = base_plugin({misc(0x800, "Thing", {{"DATA", k_value_a}}),
                                 misc(0x801, "Untouched", {{"DATA", k_value_a}})});
  const auto patch = override_plugin("Patch.esp", {misc(0x800, "Thing", {{"DATA", k_value_b}})});
  const std::vector<const fo4::Plugin*> order{&base, &patch};

  const auto graph = fo4::ConflictGraph::build(order);
  const auto result = fo4::compare_conflicts_across_plugins(graph, order);

  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].key, master_key());
  const auto* data = field_of(result[0], k_data);
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->groups.size(), 2u);
  EXPECT_EQ(names(data->groups[0]), (std::vector<std::string>{"Master.esm"}));
  EXPECT_EQ(names(data->groups[1]), (std::vector<std::string>{"Patch.esp"}));

  const auto untouched = compare(order, second_key());
  ASSERT_EQ(untouched.record_groups.size(), 1u);
  EXPECT_EQ(names(untouched.record_groups[0]), (std::vector<std::string>{"Master.esm"}));
}
