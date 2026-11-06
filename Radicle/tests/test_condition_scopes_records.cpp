#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

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

using Body = std::vector<std::pair<std::string, std::vector<std::byte>>>;
using Functions = std::vector<std::uint16_t>;

std::vector<std::byte> zeros(std::size_t count) { return std::vector<std::byte>(count); }

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter writer;
  writer.u32(value);
  return writer.bytes;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> ctda_bytes(std::uint16_t function) {
  fo4::test::ByteWriter ctda;
  ctda.u32(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.i32(0);
  ctda.i32(0);
  ctda.u32(0);
  ctda.u32(0);
  ctda.i32(-1);
  return ctda.bytes;
}

fo4::Plugin make_plugin(std::string_view signature, const Body& body) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondRecord\0", 12});
  for (const auto& [sig, data] : body) {
    fo4::test::write_subrecord(out, sig, std::span<const std::byte>{data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

const fo4::Record* cond_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

std::optional<fo4::schema::ConditionScopeDescriptor> descriptor_for(std::string_view record,
                                                                    std::string_view path) {
  const auto scoped =
      fo4::schema::scoped_condition_path(fo4::Signature::from_string(record), path);
  if (!scoped.has_value()) return std::nullopt;
  return scoped->descriptor;
}

Functions functions_in(const fo4::Record& record,
                       const fo4::schema::ConditionScopeDescriptor& descriptor,
                       std::size_t index) {
  const auto read = fo4::schema::read_scoped_conditions(record, descriptor, index);
  EXPECT_TRUE(read.ok) << "scope " << index << " status " << static_cast<int>(read.error);
  Functions out;
  for (const auto& condition : read.conditions) out.push_back(condition.function);
  return out;
}

std::vector<std::uint32_t> citc_counts(const fo4::Record& record) {
  std::vector<std::uint32_t> counts;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != fo4::Signature::from_string("CITC")) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data(), sizeof(value));
    counts.push_back(value);
  }
  return counts;
}

std::size_t index_of_nth(const fo4::Record& record, std::string_view signature, std::size_t n) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != sig) continue;
    if (seen++ == n) return i;
  }
  return record.subrecords.size();
}

void expect_prefix_unchanged(const std::vector<fo4::Subrecord>& before, const fo4::Record& after,
                             std::size_t count) {
  ASSERT_GE(after.subrecords.size(), count);
  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_EQ(after.subrecords[i].signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(after.subrecords[i].data, before[i].data) << "subrecord " << i;
  }
}

void expect_suffix_unchanged(const std::vector<fo4::Subrecord>& before, const fo4::Record& after,
                             std::size_t from) {
  ASSERT_EQ(after.subrecords.size(), before.size());
  for (std::size_t i = from; i < before.size(); ++i) {
    EXPECT_EQ(after.subrecords[i].signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(after.subrecords[i].data, before[i].data) << "subrecord " << i;
  }
}

Functions root_functions(const fo4::Record& record) {
  const auto read = fo4::schema::read_root_conditions(record);
  EXPECT_TRUE(read.ok) << "root status " << static_cast<int>(read.error);
  Functions out;
  for (const auto& condition : read.conditions) out.push_back(condition.function);
  return out;
}

fo4::CtdaCondition function_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

void expect_suffix_unchanged(const std::vector<fo4::Subrecord>& before, std::size_t before_start,
                             const fo4::Record& after, std::size_t after_start) {
  ASSERT_EQ(after.subrecords.size() - after_start, before.size() - before_start);
  for (std::size_t i = before_start; i < before.size(); ++i) {
    const auto& actual = after.subrecords[after_start + (i - before_start)];
    EXPECT_EQ(actual.signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(actual.data, before[i].data) << "subrecord " << i;
  }
}

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

Body pack_body(const Functions& top_level, const std::vector<Functions>& branches) {
  Body body;
  for (const auto function : top_level) body.push_back({"CTDA", ctda_bytes(function)});
  for (const auto& branch : branches) {
    body.push_back({"ANAM", zstring("Sandbox")});
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
      {"ANAM", zstring("Sandbox")},
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
      {"NNAM", zstring("Find")},
      {"QSTA", zeros(12)},
      {"CTDA", ctda_bytes(1)},
      {"QSTA", zeros(12)},
      {"QOBJ", zeros(2)},
      {"FNAM", zeros(4)},
      {"NNAM", zstring("Kill")},
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
      {"NNAM", zstring("Find")},
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
      {"ANAM", zstring("Sandbox")},
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
      {"RNAM", zstring("Prompt")},
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
      {"CIS1", zstring("A")},
      {"CTDA", ctda_bytes(2)},
      {"CIS2", zstring("A")},
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
      {"NAM0", zstring("P1")},
      {"CTDA", ctda_bytes(1)},
      {"NEXT", {}},
      {"CTDA", ctda_bytes(2)},
      {"NEXT", {}},
      {"WNAM", u32_bytes(350)},
      {"HNAM", {}},
      {"HNAM", {}},
      {"NAM0", zstring("P2")},
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
