#include "fo4/papyrus/quest_rules.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using fo4::papyrus::AliasFillKind;
using fo4::papyrus::AliasFillReport;
using fo4::papyrus::AliasState;
using fo4::papyrus::BoundKind;
using fo4::papyrus::OwnerId;
using fo4::papyrus::OwnerRegistry;
using fo4::papyrus::QuestAliasInput;
using fo4::papyrus::QuestStageInput;
using fo4::papyrus::QuestStages;
using fo4::papyrus::StageReport;
using fo4::papyrus::StageState;
using fo4::papyrus::VmadValueShape;
using fo4::papyrus::bindVmadValues;
using fo4::papyrus::fillQuestAliases;
using fo4::papyrus::identifyStartupStages;

std::function<bool(std::string_view)> allowAll() { return [](std::string_view) { return true; }; }

TEST(PapyrusQuestRules, IdentifiesTheSingleMarkedStartupStage) {
  const std::vector<QuestStages> quests = {
      {"Q2", {{10, true, 0}}}, {"Q1", {{0, false, 0}, {5, true, 2}, {6, false, 0}}}};
  const StageReport report = identifyStartupStages(quests, allowAll());
  ASSERT_EQ(report.quests.size(), 2u);
  EXPECT_EQ(report.quests[0].quest, "Q1");
  EXPECT_EQ(report.quests[0].state, StageState::Identified);
  ASSERT_TRUE(report.quests[0].stage.has_value());
  EXPECT_EQ(*report.quests[0].stage, 5);
  EXPECT_EQ(report.identified, 2);
  EXPECT_NE(report.toReport().find("identified=2"), std::string::npos);
}

TEST(PapyrusQuestRules, RefusesToGuessNoneOrSeveralMarkedStages) {
  const std::vector<QuestStages> quests = {{"None", {{1, false, 0}}},
                                           {"Two", {{1, true, 0}, {2, true, 0}}}};
  const StageReport report = identifyStartupStages(quests, allowAll());
  EXPECT_EQ(report.unresolved, 2);
  EXPECT_EQ(report.quests[0].state, StageState::Unidentified);
  EXPECT_EQ(report.quests[1].state, StageState::Ambiguous);
}

TEST(PapyrusQuestRules, FragmentTimingAndIdentificationGatesBlock) {
  const std::vector<QuestStages> quests = {{"Q", {{4, true, 3}}}};
  const StageReport blocked = identifyStartupStages(
      quests, [](std::string_view key) { return key != "quest.runtime.fragment_dispatch_timing"; });
  EXPECT_EQ(blocked.refused, 1);
  EXPECT_EQ(blocked.quests[0].state, StageState::BlockedByFragments);
  ASSERT_TRUE(blocked.quests[0].stage.has_value());
  EXPECT_EQ(*blocked.quests[0].stage, 4);
  const StageReport refused = identifyStartupStages(
      quests, [](std::string_view key) { return key != "quest.startup.stage_identification"; });
  EXPECT_EQ(refused.refused, 1);
  EXPECT_EQ(refused.quests[0].state, StageState::Unidentified);
}

TEST(PapyrusQuestRules, BindsEveryScalarShapeAndRefusesTheRest) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  std::vector<VmadValueShape> shapes;
  VmadValueShape integer;
  integer.name = "Count";
  integer.type = "Int";
  integer.integer = 3;
  shapes.push_back(integer);
  VmadValueShape text;
  text.name = "Label";
  text.type = "String";
  text.text = "hi";
  shapes.push_back(text);
  VmadValueShape object;
  object.name = "Ref";
  object.type = "Object";
  object.object = "0002:Actor.esp";
  shapes.push_back(object);
  VmadValueShape alias;
  alias.name = "AliasRef";
  alias.type = "Object";
  alias.hasAlias = true;
  shapes.push_back(alias);
  VmadValueShape array;
  array.name = "List";
  array.type = "Array";
  array.elementCount = 2;
  shapes.push_back(array);
  VmadValueShape noValue;
  noValue.name = "Missing";
  noValue.type = "Float";
  shapes.push_back(noValue);
  const auto report = bindVmadValues(owner, shapes);
  ASSERT_EQ(report.properties.size(), 6u);
  EXPECT_EQ(report.properties[0].name, "AliasRef");
  EXPECT_EQ(report.properties[0].kind, BoundKind::Unsupported);
  EXPECT_EQ(report.bound, 3);
  EXPECT_EQ(report.unsupported, 3);
  EXPECT_EQ(report.silentlyDefaulted, 0);
  EXPECT_TRUE(report.clean());
  EXPECT_NE(report.toReport().find("bound=3 unsupported=3"), std::string::npos);
  EXPECT_EQ(report.digest(), report.digest());
}

TEST(PapyrusQuestRules, FillsTheProvenAliasKindsAndLeavesTheActorsOpen) {
  OwnerRegistry references;
  const std::vector<QuestAliasInput> aliases = {
      {"Q", 2, AliasFillKind::Location, std::nullopt, "0003:Loc.esp"},
      {"Q", 1, AliasFillKind::ForcedReference, "0004:Ref.esp", std::nullopt},
      {"Q", 3, AliasFillKind::CreateReference, "0005:Base.esp", std::nullopt},
      {"Q", 4, AliasFillKind::FindMatching, std::nullopt, std::nullopt},
      {"Q", 5, AliasFillKind::UniqueActor, std::nullopt, std::nullopt}};
  const AliasFillReport report = fillQuestAliases(aliases, allowAll(), references);
  ASSERT_EQ(report.aliases.size(), 5u);
  EXPECT_EQ(report.filled, 3);
  EXPECT_EQ(report.open, 2);
  EXPECT_EQ(report.aliases[0].owner.key(), "alias:Q#1");
  EXPECT_EQ(report.aliases[0].state, AliasState::Filled);
  EXPECT_EQ(report.aliases[3].state, AliasState::Open);
  EXPECT_NE(report.toReport().find("filled=3 open=2"), std::string::npos);
  EXPECT_EQ(references.countFor("0005:Base.esp"), 1u);
}

TEST(PapyrusQuestRules, AGateRefusalLeavesTheAliasOpenAndCountsTheKind) {
  OwnerRegistry references;
  const std::vector<QuestAliasInput> aliases = {{"Q", 1, AliasFillKind::ForcedReference, "0004:Ref.esp",
                                                 std::nullopt}};
  const AliasFillReport report = fillQuestAliases(
      aliases, [](std::string_view key) { return key != "quest.alias.fill.forced_reference"; }, references);
  EXPECT_EQ(report.open, 1);
  ASSERT_EQ(report.openKinds.size(), 1u);
  EXPECT_EQ(report.openKinds[0], "forcedreference");
}

TEST(PapyrusQuestRules, AProvenFillStillNeedsItsValue) {
  OwnerRegistry references;
  const std::vector<QuestAliasInput> aliases = {{"Q", 1, AliasFillKind::ForcedReference, std::nullopt,
                                                 std::nullopt}};
  EXPECT_THROW(fillQuestAliases(aliases, allowAll(), references), std::invalid_argument);
}

}  // namespace
