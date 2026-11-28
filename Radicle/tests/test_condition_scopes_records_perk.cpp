#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"
#include "byte_helpers.hpp"
#include "condition_scope_records_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace fo4::test::scope;

using fo4::test::u32_bytes;

using fo4::test::zstr;

Body perk_tabs_body() {
  return {
      {"CTDA", ctda_bytes(100)},
      {"PRKE", zeros(3)},
      {"DATA", zeros(8)},
      {"PRKC", zeros(1)},
      {"CTDA", ctda_bytes(1)},
      {"PRKC", zeros(1)},
      {"CTDA", ctda_bytes(2)},
      {"CTDA", ctda_bytes(3)},
      {"EPFT", zeros(1)},
      {"EPFB", zeros(2)},
      {"PRKF", zeros(0)},
      {"PRKE", zeros(3)},
      {"DATA", zeros(8)},
      {"PRKC", zeros(1)},
      {"CTDA", ctda_bytes(4)},
      {"PRKF", zeros(0)},
  };
}

}  // namespace

TEST(ConditionScopesRecords, ResolvesPerkEffectTabPath) {
  for (const auto path : {"Tabs[0].Conditions", "Tabs[2].Conditions", " Tabs[1] . Conditions "}) {
    const auto descriptor = descriptor_for("PERK", path);
    ASSERT_TRUE(descriptor.has_value()) << path;
    EXPECT_EQ(descriptor->kind, fo4::schema::ConditionScopeKind::perk_effect_tab) << path;
  }

  EXPECT_FALSE(descriptor_for("PERK", "Branches[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("PERK", "Tabs[0].Keywords").has_value());
  EXPECT_FALSE(descriptor_for("PACK", "Tabs[0].Conditions").has_value());
}

TEST(ConditionScopesRecords, PerkTabScopesStopAtNextTabEffectAndFunctionParameters) {
  auto plugin = make_plugin("PERK", perk_tabs_body());
  const auto descriptor = descriptor_for("PERK", "Tabs[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(functions_in(*cond_record(plugin), *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*cond_record(plugin), *descriptor, 1), (Functions{2, 3}));
  EXPECT_EQ(functions_in(*cond_record(plugin), *descriptor, 2), Functions{4});
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*cond_record(plugin), *descriptor, 3).error,
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
}

TEST(ConditionScopesRecords, PerkTabReplaceLeavesTopLevelListAndOtherEffectMembersIdentical) {
  auto plugin = make_plugin("PERK", perk_tabs_body());
  const auto descriptor = descriptor_for("PERK", "Tabs[1].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto second_prkc = index_of_nth(*cond_record(plugin), "PRKC", 1);
  const auto epft = index_of_nth(*cond_record(plugin), "EPFT", 0);
  ASSERT_LT(epft, before.size());

  fo4::CtdaCondition replacement;
  replacement.function = 9;
  const std::vector<fo4::CtdaCondition> conditions{replacement};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, second_prkc + 1);
  expect_suffix_unchanged(before, epft, *record, index_of_nth(*record, "EPFT", 0));
  EXPECT_EQ(fo4::read_ctda_conditions_in(*record, 0, index_of_nth(*record, "PRKC", 0)).at(0).function,
            100u);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{9});
  EXPECT_EQ(functions_in(*record, *descriptor, 2), Functions{4});
}

TEST(ConditionScopesRecords, PerkTabInsertsIntoEmptyTabDirectlyAfterPrkc) {
  auto plugin = make_plugin("PERK", {
      {"PRKE", zeros(3)},
      {"DATA", zeros(8)},
      {"PRKC", zeros(1)},
      {"CTDA", ctda_bytes(1)},
      {"PRKC", zeros(1)},
      {"PRKE", zeros(3)},
      {"DATA", zeros(8)},
      {"PRKC", zeros(1)},
      {"CTDA", ctda_bytes(2)},
      {"PRKF", zeros(0)},
  });
  const auto descriptor = descriptor_for("PERK", "Tabs[1].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto second_prkc = index_of_nth(*cond_record(plugin), "PRKC", 1);

  fo4::CtdaCondition condition;
  condition.function = 7;
  const std::vector<fo4::CtdaCondition> conditions{condition};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, second_prkc + 1);
  EXPECT_EQ(index_of_nth(*record, "CTDA", 1), second_prkc + 1);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{7});
  EXPECT_EQ(functions_in(*record, *descriptor, 2), Functions{2});
  EXPECT_TRUE(citc_counts(*record).empty());
}

TEST(ConditionScopesRecords, PerkTabReplaceDropsCisWithItsCondition) {
  auto plugin = make_plugin("PERK", {
      {"CTDA", ctda_bytes(100)},
      {"PRKE", zeros(3)},
      {"DATA", zeros(8)},
      {"PRKC", zeros(1)},
      {"CTDA", ctda_bytes(1)},
      {"CIS1", zstr("A")},
      {"CTDA", ctda_bytes(2)},
      {"CIS2", zstr("A")},
      {"EPFT", zeros(1)},
      {"PRKF", zeros(0)},
  });
  const auto descriptor = descriptor_for("PERK", "Tabs[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(functions_in(*cond_record(plugin), *descriptor, 0), (Functions{1, 2}));
  const auto before = cond_record(plugin)->subrecords;
  const auto prkc = index_of_nth(*cond_record(plugin), "PRKC", 0);
  ASSERT_LT(prkc, before.size());

  fo4::CtdaCondition replacement;
  replacement.function = 9;
  const std::vector<fo4::CtdaCondition> conditions{replacement};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 0, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, prkc + 1);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{9});
  EXPECT_EQ(index_of_nth(*record, "CIS1", 0), record->subrecords.size());
  EXPECT_EQ(index_of_nth(*record, "CIS2", 0), record->subrecords.size());
  EXPECT_EQ(index_of_nth(*record, "CTDA", 1), prkc + 1);
  EXPECT_EQ(index_of_nth(*record, "EPFT", 0), prkc + 2);
}

TEST(ConditionScopesRecords, PerkTabRefusesOutOfRangeIndex) {
  auto plugin = make_plugin("PERK", perk_tabs_body());
  const auto descriptor = descriptor_for("PERK", "Tabs[3].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = fo4::write_plugin(plugin);

  const auto read = fo4::schema::read_scoped_conditions(*cond_record(plugin), *descriptor, 3);
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.error, fo4::schema::ScopedConditionStatus::container_index_out_of_range);

  const std::vector<fo4::CtdaCondition> conditions{fo4::CtdaCondition{}};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 3, conditions),
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(ConditionScopesRecords, PerkRootListBesideEffectTabsReadsAndWritesRoot) {
  auto plugin = make_plugin("PERK", perk_tabs_body());
  const auto tabs = descriptor_for("PERK", "Tabs[0].Conditions");
  ASSERT_TRUE(tabs.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto first_prke = index_of_nth(*cond_record(plugin), "PRKE", 0);
  EXPECT_EQ(root_functions(*cond_record(plugin)), Functions{100});

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_suffix_unchanged(before, *record, first_prke);
  EXPECT_EQ(root_functions(*record), Functions{9});
  EXPECT_EQ(functions_in(*record, *tabs, 1), (Functions{2, 3}));
}

TEST(ConditionScopesRecords, EffectOnlyRecordRefusesBareWriteAndKeepsEffectList) {
  auto plugin = make_plugin("SPEL", {
      {"EFID", zeros(4)},
      {"EFIT", zeros(12)},
      {"CTDA", ctda_bytes(8)},
  });
  const auto before = plugin;
  const auto read = fo4::schema::read_root_conditions(*cond_record(plugin));
  EXPECT_TRUE(read.ok);
  EXPECT_TRUE(read.conditions.empty());
  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::no_condition_list);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(before));
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), {}),
            fo4::ConditionMutationStatus::unchanged);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(before));
}

TEST(ConditionScopesRecords, ScenePhaseConditionScopesReadInsertAndReplacePerPhase) {
  auto plugin = make_plugin("SCEN", {
      {"HNAM", {}},
      {"NAM0", zstr("P1")},
      {"CTDA", ctda_bytes(1)},
      {"NEXT", {}},
      {"CTDA", ctda_bytes(2)},
      {"NEXT", {}},
      {"WNAM", u32_bytes(350)},
      {"HNAM", {}},
      {"HNAM", {}},
      {"NAM0", zstr("P2")},
      {"NEXT", {}},
      {"NEXT", {}},
      {"WNAM", u32_bytes(350)},
      {"HNAM", {}},
      {"PNAM", u32_bytes(0)},
      {"INAM", u32_bytes(0)},
  });
  const auto start = descriptor_for("SCEN", "Phases[0].Start Conditions");
  const auto completion = descriptor_for("SCEN", "Phases[0].Completion Conditions");
  const auto second = descriptor_for("SCEN", "Phases[1].Start Conditions");
  ASSERT_TRUE(start.has_value());
  ASSERT_TRUE(completion.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(descriptor_for("SCEN", "Faces[0].Start Conditions").has_value() == false);

  const auto key = fo4::FormKey::parse("000800:Cond.esp");
  const auto* record = cond_record(plugin);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(functions_in(*record, *start, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *completion, 0), Functions{2});
  EXPECT_TRUE(functions_in(*record, *second, 1).empty());
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, *start, 2).error,
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);

  const std::vector<fo4::CtdaCondition> inserted{function_condition(7)};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, *second, 1, inserted),
            fo4::schema::ScopedConditionStatus::updated);
  const std::vector<fo4::CtdaCondition> replaced{function_condition(9)};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, *completion, 0, replaced),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* after = cond_record(reread);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(functions_in(*after, *start, 0), Functions{1});
  EXPECT_EQ(functions_in(*after, *completion, 0), Functions{9});
  EXPECT_EQ(functions_in(*after, *second, 1), Functions{7});
  EXPECT_TRUE(functions_in(*after, *completion, 1).empty());
}
