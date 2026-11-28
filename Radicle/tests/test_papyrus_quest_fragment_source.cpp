#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/quest_fragment_source.hpp>

namespace {

using fo4::papyrus::Parser;
using fo4::papyrus::QuestFragmentAction;
using fo4::papyrus::QuestFragmentActionKind;
using fo4::papyrus::isManagedQuestFragmentSource;
using fo4::papyrus::removeQuestFragmentSource;
using fo4::papyrus::updateQuestFragmentSource;

QuestFragmentAction act(QuestFragmentActionKind kind, std::uint16_t value) {
  QuestFragmentAction action;
  action.kind = kind;
  action.value = value;
  return action;
}

bool contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

}

TEST(PapyrusQuestFragmentSource, UpdatesOneStageWithoutDroppingOtherStages) {
  const std::vector<QuestFragmentAction> actions{act(QuestFragmentActionKind::Display, 0),
                                                 act(QuestFragmentActionKind::Complete, 1),
                                                 act(QuestFragmentActionKind::SetStage, 20)};
  const auto first = updateQuestFragmentSource({}, "QF_Test", 10, 0, actions);
  ASSERT_TRUE(first.has_value());
  EXPECT_FALSE(Parser::parse(*first).hasErrors());
  EXPECT_TRUE(contains(*first, "SetObjectiveDisplayed(0)"));
  EXPECT_TRUE(contains(*first, "SetObjectiveCompleted(1)"));
  EXPECT_TRUE(contains(*first, "SetStage(20)"));
  const auto second = updateQuestFragmentSource(*first, "QF_Test", 20, 0, actions);
  ASSERT_TRUE(second.has_value());
  const auto changed =
      updateQuestFragmentSource(*second, "QF_Test", 10, 0, {act(QuestFragmentActionKind::SetStage, 30)});
  ASSERT_TRUE(changed.has_value());
  EXPECT_TRUE(contains(*changed, "Fragment_Stage_0020_Item_00"));
  EXPECT_TRUE(contains(*changed, "SetStage(20)"));
  EXPECT_TRUE(contains(*changed, "SetStage(30)"));
}

TEST(PapyrusQuestFragmentSource, RefusesUnownedOrAmbiguousScripts) {
  const std::vector<QuestFragmentAction> actions{act(QuestFragmentActionKind::SetStage, 20)};
  EXPECT_FALSE(updateQuestFragmentSource("Scriptname QF_Test Extends Quest\n", "QF_Test", 10, 0, actions));
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Test\nFunction Evil()", 10, 0, actions));
  EXPECT_FALSE(updateQuestFragmentSource({}, "Function", 10, 0, actions));
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Test", 10, 0, {}));
  const auto source = updateQuestFragmentSource({}, "QF_Test", 10, 0, actions);
  ASSERT_TRUE(source.has_value());
  EXPECT_FALSE(updateQuestFragmentSource(*source, "QF_Other", 10, 0, actions));
  EXPECT_FALSE(updateQuestFragmentSource(*source + "Function Broken(\n", "QF_Test", 10, 0, actions));
  const auto typed = std::string("; GardenTools managed quest fragments\n") +
                     "Scriptname QF_Test Extends Quest Hidden Const\nInt Function Fragment_Stage_0010_Item_00()\n"
                     "Return 7\nEndFunction\n";
  EXPECT_FALSE(updateQuestFragmentSource(typed, "QF_Test", 10, 0, actions));
}

TEST(PapyrusQuestFragmentSource, AcceptsNamespacedScriptNamesOnly) {
  const std::vector<QuestFragmentAction> actions{act(QuestFragmentActionKind::SetStage, 20)};
  EXPECT_TRUE(updateQuestFragmentSource({}, "Fragments:Quests:QF_Test", 10, 0, actions).has_value());
  EXPECT_FALSE(updateQuestFragmentSource({}, ":QF_Test", 10, 0, actions).has_value());
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Test:", 10, 0, actions).has_value());
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF Test", 10, 0, actions).has_value());
}

TEST(PapyrusQuestFragmentSource, EmitsObjectiveFlagsOnlyWhenGiven) {
  const auto plain = updateQuestFragmentSource(
      {}, "QF_Flags", 10, 0, {act(QuestFragmentActionKind::Display, 3), act(QuestFragmentActionKind::Complete, 4)});
  ASSERT_TRUE(plain.has_value());
  EXPECT_TRUE(contains(*plain, "SetObjectiveDisplayed(3)\n"));
  EXPECT_TRUE(contains(*plain, "SetObjectiveCompleted(4)\n"));
  auto hide = act(QuestFragmentActionKind::Display, 5);
  hide.visible = false;
  const auto hidden = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {hide});
  ASSERT_TRUE(hidden.has_value());
  EXPECT_TRUE(contains(*hidden, "SetObjectiveDisplayed(5, false, false)\n"));
  auto force = act(QuestFragmentActionKind::Display, 6);
  force.force = true;
  const auto forced = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {force});
  ASSERT_TRUE(forced.has_value());
  EXPECT_TRUE(contains(*forced, "SetObjectiveDisplayed(6, true, true)\n"));
  auto uncomplete = act(QuestFragmentActionKind::Complete, 7);
  uncomplete.completed = false;
  const auto reopened = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {uncomplete});
  ASSERT_TRUE(reopened.has_value());
  EXPECT_TRUE(contains(*reopened, "SetObjectiveCompleted(7, false)\n"));
  auto done = act(QuestFragmentActionKind::Complete, 8);
  done.completed = true;
  const auto completed = updateQuestFragmentSource({}, "QF_Flags", 10, 0, {done});
  ASSERT_TRUE(completed.has_value());
  EXPECT_TRUE(contains(*completed, "SetObjectiveCompleted(8, true)\n"));
}

TEST(PapyrusQuestFragmentSource, RefusesObjectiveFlagsOnTheWrongActionKind) {
  auto displayCompleted = act(QuestFragmentActionKind::Display, 1);
  displayCompleted.completed = true;
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {displayCompleted}).has_value());
  auto completeVisible = act(QuestFragmentActionKind::Complete, 1);
  completeVisible.visible = true;
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {completeVisible}).has_value());
  auto completeForce = act(QuestFragmentActionKind::Complete, 1);
  completeForce.force = false;
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {completeForce}).has_value());
  auto stageFlag = act(QuestFragmentActionKind::SetStage, 20);
  stageFlag.completed = true;
  EXPECT_FALSE(updateQuestFragmentSource({}, "QF_Flags", 10, 0, {stageFlag}).has_value());
}

TEST(PapyrusQuestFragmentSource, RemovesOneManagedFunctionAndRefusesUnmanagedSource) {
  const std::vector<QuestFragmentAction> actions{act(QuestFragmentActionKind::SetStage, 20)};
  const auto first = updateQuestFragmentSource({}, "QF_Test", 10, 0, actions);
  ASSERT_TRUE(first.has_value());
  const auto both = updateQuestFragmentSource(*first, "QF_Test", 20, 0, actions);
  ASSERT_TRUE(both.has_value());
  ASSERT_TRUE(isManagedQuestFragmentSource(*both));
  const auto removed = removeQuestFragmentSource(*both, "QF_Test", 10, 0);
  ASSERT_TRUE(removed.has_value());
  EXPECT_FALSE(contains(*removed, "Fragment_Stage_0010_Item_00"));
  EXPECT_TRUE(contains(*removed, "SetStage(20)"));
  EXPECT_FALSE(Parser::parse(*removed).hasErrors());
  EXPECT_FALSE(removeQuestFragmentSource(*removed, "QF_Test", 10, 0).has_value());
  EXPECT_FALSE(removeQuestFragmentSource("Scriptname QF_Test Extends Quest\n", "QF_Test", 10, 0).has_value());
  EXPECT_FALSE(isManagedQuestFragmentSource("Scriptname QF_Test Extends Quest\n"));
}
