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

Body pack_body(const Functions& top_level, const std::vector<Functions>& branches) {
  Body body;
  for (const auto function : top_level) body.push_back({"CTDA", ctda_bytes(function)});
  for (const auto& branch : branches) {
    body.push_back({"ANAM", zstr("Sandbox")});
    body.push_back({"CITC", u32_bytes(static_cast<std::uint32_t>(branch.size()))});
    for (const auto function : branch) body.push_back({"CTDA", ctda_bytes(function)});
    body.push_back({"PRCB", zeros(8)});
  }
  return body;
}

}  // namespace

TEST(ConditionScopesRecords, ResolvesPackBranchAndQuestTargetPaths) {
  for (const auto path : {"Branches[0].Conditions", "branches[2].conditions", " Branches[1] . Conditions "}) {
    EXPECT_TRUE(descriptor_for("PACK", path).has_value()) << path;
  }
  EXPECT_TRUE(descriptor_for("QUST", "Targets[0].Conditions").has_value());
  EXPECT_TRUE(descriptor_for("QUST", "Aliases[0].Conditions").has_value());

  EXPECT_FALSE(descriptor_for("PACK", "Targets[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("QUST", "Branches[0].Conditions").has_value());
  EXPECT_FALSE(descriptor_for("PACK", "Conditions").has_value());
  EXPECT_FALSE(descriptor_for("PACK", "Branches[0].Keywords").has_value());
  EXPECT_FALSE(descriptor_for("PACK", "Branches[x].Conditions").has_value());
}

TEST(ConditionScopesRecords, PackBranchInsertsIntoEmptyBranchAndUpdatesCount) {
  auto plugin = make_plugin("PACK", pack_body({100}, {{1}, {}}));
  const auto descriptor = descriptor_for("PACK", "Branches[1].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto key = fo4::FormKey::parse("000800:Cond.esp");

  const std::vector<fo4::CtdaCondition> conditions{[] {
    fo4::CtdaCondition condition;
    condition.function = 7;
    return condition;
  }()};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(plugin, key, *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{7});
  EXPECT_EQ(citc_counts(*record), (std::vector<std::uint32_t>{1, 1}));
  EXPECT_EQ(fo4::schema::read_scoped_conditions(*record, *descriptor, 2).error,
            fo4::schema::ScopedConditionStatus::container_index_out_of_range);
}

TEST(ConditionScopesRecords, PackBranchReplaceLeavesTopLevelAndEarlierBranchIdentical) {
  auto plugin = make_plugin("PACK", pack_body({100}, {{1}, {2}}));
  const auto descriptor = descriptor_for("PACK", "Branches[1].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto second_anam = index_of_nth(*cond_record(plugin), "ANAM", 1);
  ASSERT_LT(second_anam, before.size());

  fo4::CtdaCondition replacement;
  replacement.function = 9;
  const std::vector<fo4::CtdaCondition> conditions{replacement};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, second_anam);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{9});
  EXPECT_EQ(fo4::read_ctda_conditions_in(*record, 0, index_of_nth(*record, "ANAM", 0)).at(0).function,
            100u);
}

TEST(ConditionScopesRecords, PackBranchRefusesBranchHoldingSeveralLists) {
  auto plugin = make_plugin("PACK", {
      {"ANAM", zstr("Sandbox")},
      {"CITC", u32_bytes(2)},
      {"CTDA", ctda_bytes(1)},
      {"PRCB", zeros(8)},
      {"CTDA", ctda_bytes(2)},
  });
  const auto descriptor = descriptor_for("PACK", "Branches[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = plugin;

  const auto read = fo4::schema::read_scoped_conditions(*cond_record(plugin), *descriptor, 0);
  EXPECT_FALSE(read.ok);
  EXPECT_EQ(read.error, fo4::schema::ScopedConditionStatus::multiple_lists);

  const std::vector<fo4::CtdaCondition> conditions{fo4::CtdaCondition{}};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 0, conditions),
            fo4::schema::ScopedConditionStatus::multiple_lists);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(before));
}

TEST(ConditionScopesRecords, QuestTargetInsertsIntoEmptyTargetAfterQsta) {
  auto plugin = make_plugin("QUST", {
      {"QOBJ", zeros(2)},
      {"FNAM", zeros(4)},
      {"NNAM", zstr("Find")},
      {"QSTA", zeros(12)},
      {"CTDA", ctda_bytes(1)},
      {"QSTA", zeros(12)},
      {"QOBJ", zeros(2)},
      {"FNAM", zeros(4)},
      {"NNAM", zstr("Kill")},
      {"QSTA", zeros(12)},
      {"CTDA", ctda_bytes(3)},
      {"ANAM", u32_bytes(0)},
  });
  const auto descriptor = descriptor_for("QUST", "Targets[1].Conditions");
  ASSERT_TRUE(descriptor.has_value());

  const std::vector<fo4::CtdaCondition> conditions{[] {
    fo4::CtdaCondition condition;
    condition.function = 7;
    return condition;
  }()};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{7});
  EXPECT_EQ(functions_in(*record, *descriptor, 2), Functions{3});
  EXPECT_LT(index_of_nth(*record, "CTDA", 1), index_of_nth(*record, "QOBJ", 1));
}

TEST(ConditionScopesRecords, QuestTargetReplaceLeavesDialogueAndEarlierTargetIdentical) {
  auto plugin = make_plugin("QUST", {
      {"CTDA", ctda_bytes(100)},
      {"QOBJ", zeros(2)},
      {"FNAM", zeros(4)},
      {"NNAM", zstr("Find")},
      {"QSTA", zeros(12)},
      {"CITC", u32_bytes(1)},
      {"CTDA", ctda_bytes(1)},
      {"QSTA", zeros(12)},
      {"CITC", u32_bytes(1)},
      {"CTDA", ctda_bytes(2)},
      {"ANAM", u32_bytes(0)},
  });
  const auto descriptor = descriptor_for("QUST", "Targets[1].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto second_qsta = index_of_nth(*cond_record(plugin), "QSTA", 1);
  ASSERT_LT(second_qsta, before.size());

  fo4::CtdaCondition replacement;
  replacement.function = 9;
  const std::vector<fo4::CtdaCondition> conditions{replacement};
  EXPECT_EQ(fo4::schema::set_record_scoped_conditions(
                plugin, fo4::FormKey::parse("000800:Cond.esp"), *descriptor, 1, conditions),
            fo4::schema::ScopedConditionStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_prefix_unchanged(before, *record, second_qsta);
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
  EXPECT_EQ(functions_in(*record, *descriptor, 1), Functions{9});
  EXPECT_EQ(citc_counts(*record), (std::vector<std::uint32_t>{1, 1}));
}

TEST(ConditionScopesRecords, PackRootListReadAndWriteLeavesBranchIdentical) {
  auto plugin = make_plugin("PACK", pack_body({100}, {{1}}));
  const auto descriptor = descriptor_for("PACK", "Branches[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto branch_anam = index_of_nth(*cond_record(plugin), "ANAM", 0);
  ASSERT_LT(branch_anam, before.size());
  EXPECT_EQ(root_functions(*cond_record(plugin)), Functions{100});

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_suffix_unchanged(before, *record, branch_anam);
  EXPECT_EQ(root_functions(*record), Functions{9});
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
}

TEST(ConditionScopesRecords, PackRootInsertsBeforeCombatStyleSlotAndKeepsBranch) {
  auto plugin = make_plugin("PACK", {
      {"PKDT", zeros(12)},
      {"PKCU", zeros(12)},
      {"ANAM", zstr("Sandbox")},
      {"CITC", u32_bytes(1)},
      {"CTDA", ctda_bytes(1)},
      {"PRCB", zeros(8)},
  });
  const auto descriptor = descriptor_for("PACK", "Branches[0].Conditions");
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_TRUE(root_functions(*cond_record(plugin)).empty());

  const std::vector<fo4::CtdaCondition> conditions{function_condition(5)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(index_of_nth(*record, "CTDA", 0) + 1, index_of_nth(*record, "PKCU", 0));
  EXPECT_EQ(root_functions(*record), Functions{5});
  EXPECT_EQ(functions_in(*record, *descriptor, 0), Functions{1});
}

TEST(ConditionScopesRecords, InfoRootInsertsBeforePrompt) {
  auto plugin = make_plugin("INFO", {
      {"TRDT", zeros(24)},
      {"RNAM", zstr("Prompt")},
  });
  const std::vector<fo4::CtdaCondition> conditions{function_condition(7)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(index_of_nth(*record, "CTDA", 0) + 1, index_of_nth(*record, "RNAM", 0));
  EXPECT_EQ(root_functions(*record), Functions{7});
}

TEST(ConditionScopesRecords, RecordWithoutNestedScopesKeepsWholeRecordRules) {
  auto single = make_plugin("MISC", {{"CTDA", ctda_bytes(1)}, {"ANAM", u32_bytes(0)}});
  EXPECT_EQ(root_functions(*cond_record(single)), Functions{1});
  const std::vector<fo4::CtdaCondition> replacement{function_condition(5)};
  EXPECT_EQ(fo4::set_record_conditions(single, fo4::FormKey::parse("000800:Cond.esp"), replacement),
            fo4::ConditionMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(single), "Cond.esp");
  ASSERT_NE(cond_record(reread), nullptr);
  EXPECT_EQ(root_functions(*cond_record(reread)), Functions{5});

  auto several = make_plugin("SCEN", {
      {"PNAM", zeros(4)},
      {"CTDA", ctda_bytes(1)},
      {"ANAM", u32_bytes(0)},
      {"CTDA", ctda_bytes(2)},
  });
  const auto several_before = several;
  EXPECT_EQ(fo4::schema::read_root_conditions(*cond_record(several)).error,
            fo4::schema::ScopedConditionStatus::multiple_lists);
  EXPECT_EQ(fo4::set_record_conditions(several, fo4::FormKey::parse("000800:Cond.esp"), replacement),
            fo4::ConditionMutationStatus::multiple_lists);
  EXPECT_EQ(fo4::write_plugin(several), fo4::write_plugin(several_before));

  auto none = make_plugin("SPEL", {{"ANAM", u32_bytes(0)}});
  const auto none_before = none;
  EXPECT_EQ(fo4::set_record_conditions(none, fo4::FormKey::parse("000800:Cond.esp"), replacement),
            fo4::ConditionMutationStatus::no_condition_list);
  EXPECT_EQ(fo4::write_plugin(none), fo4::write_plugin(none_before));
}

TEST(ConditionScopesRecords, QuestRootListBesideTargetReadsAndWritesRoot) {
  auto plugin = make_plugin("QUST", {
      {"CTDA", ctda_bytes(100)},
      {"QSTA", zeros(12)},
      {"CTDA", ctda_bytes(1)},
  });
  const auto target = descriptor_for("QUST", "Targets[0].Conditions");
  ASSERT_TRUE(target.has_value());
  const auto before = cond_record(plugin)->subrecords;
  const auto first_qsta = index_of_nth(*cond_record(plugin), "QSTA", 0);
  EXPECT_EQ(root_functions(*cond_record(plugin)), Functions{100});

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_suffix_unchanged(before, *record, first_qsta);
  EXPECT_EQ(root_functions(*record), Functions{9});
  EXPECT_EQ(functions_in(*record, *target, 0), Functions{1});
}

TEST(ConditionScopesRecords, QuestStoryManagerOnlyReadsEmptyAndInsertsDialogueBeforeNext) {
  auto plugin = make_plugin("QUST", {
      {"NEXT", zeros(0)},
      {"CTDA", ctda_bytes(200)},
  });
  const auto before = cond_record(plugin)->subrecords;
  EXPECT_TRUE(root_functions(*cond_record(plugin)).empty());

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(index_of_nth(*record, "CTDA", 0) + 1, index_of_nth(*record, "NEXT", 0));
  EXPECT_EQ(root_functions(*record), Functions{9});
  expect_suffix_unchanged(before, 1, *record, 2);
}

TEST(ConditionScopesRecords, QuestDialogueOnlyBeforeNextReadsAndWrites) {
  auto plugin = make_plugin("QUST", {
      {"CTDA", ctda_bytes(100)},
      {"NEXT", zeros(0)},
  });
  const auto before = cond_record(plugin)->subrecords;
  EXPECT_EQ(root_functions(*cond_record(plugin)), Functions{100});

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(root_functions(*record), Functions{9});
  expect_suffix_unchanged(before, 2, *record, 2);
}

TEST(ConditionScopesRecords, QuestDialogueAndStoryManagerListsEditDialogueOnly) {
  auto plugin = make_plugin("QUST", {
      {"CTDA", ctda_bytes(100)},
      {"NEXT", zeros(0)},
      {"CTDA", ctda_bytes(200)},
  });
  const auto before = cond_record(plugin)->subrecords;
  EXPECT_EQ(root_functions(*cond_record(plugin)), Functions{100});

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(root_functions(*record), Functions{9});
  expect_suffix_unchanged(before, 2, *record, 2);
}

TEST(ConditionScopesRecords, QuestRootExcludesStageLogEntryLists) {
  auto plugin = make_plugin("QUST", {
      {"CTDA", ctda_bytes(100)},
      {"INDX", zeros(4)},
      {"QSDT", zeros(1)},
      {"CTDA", ctda_bytes(7)},
  });
  const auto before = cond_record(plugin)->subrecords;
  const auto first_indx = index_of_nth(*cond_record(plugin), "INDX", 0);
  EXPECT_EQ(root_functions(*cond_record(plugin)), Functions{100});

  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::updated);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Cond.esp");
  const auto* record = cond_record(reread);
  ASSERT_NE(record, nullptr);
  expect_suffix_unchanged(before, *record, first_indx);
  EXPECT_EQ(root_functions(*record), Functions{9});
}

TEST(ConditionScopesRecords, QuestWithOnlyLogEntryListHasNoRootList) {
  auto plugin = make_plugin("QUST", {
      {"INDX", zeros(4)},
      {"QSDT", zeros(1)},
      {"CTDA", ctda_bytes(7)},
  });
  const auto before = plugin;
  const auto read = fo4::schema::read_root_conditions(*cond_record(plugin));
  EXPECT_TRUE(read.ok);
  EXPECT_TRUE(read.conditions.empty());
  const std::vector<fo4::CtdaCondition> conditions{function_condition(9)};
  EXPECT_EQ(fo4::set_record_conditions(plugin, fo4::FormKey::parse("000800:Cond.esp"), conditions),
            fo4::ConditionMutationStatus::no_condition_list);
  EXPECT_EQ(fo4::write_plugin(plugin), fo4::write_plugin(before));
}
