#include "fo4/papyrus/quest_lifecycle.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::ActorAliasInput;
using fo4::papyrus::ActorBindState;
using fo4::papyrus::AliasAttachState;
using fo4::papyrus::AliasScriptInput;
using fo4::papyrus::Lifecycle;
using fo4::papyrus::OwnerId;
using fo4::papyrus::QuestLifecycle;
using fo4::papyrus::QuestStartupInput;
using fo4::papyrus::QuestStartupProperty;
using fo4::papyrus::QuestStartupState;
using fo4::papyrus::Runtime;
using fo4::papyrus::VmadValueShape;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

std::uint8_t opcodeOf(const std::string& mnemonic) {
  const std::size_t count = sizeof(PexFile::kOpCodes) / sizeof(PexFile::kOpCodes[0]);
  for (std::size_t i = 0; i < count; ++i) {
    if (mnemonic == PexFile::kOpCodes[i].name) return static_cast<std::uint8_t>(i);
  }
  return 0xFF;
}

PexValue id(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexValue literal(std::string text) {
  PexValue value;
  value.type = PexValueType::String;
  value.str = std::move(text);
  return value;
}

PexInstruction ins(std::string mnemonic, std::vector<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcodeOf(mnemonic);
  instruction.mnemonic = std::move(mnemonic);
  instruction.fixedArgCount = static_cast<int>(args.size());
  instruction.hasVarArgs = false;
  instruction.args = std::move(args);
  return instruction;
}

PexObject script(std::string name, std::vector<PexInstruction> onInit, std::string parent = std::string()) {
  PexFunction function;
  function.name = "OnInit";
  function.returnType = "None";
  function.instructions = std::move(onInit);
  PexState entry;
  entry.name = "";
  entry.functions.push_back(std::move(function));
  PexObject object;
  object.name = std::move(name);
  object.parentClassName = std::move(parent);
  object.autoStateName = "";
  object.states.push_back(std::move(entry));
  return object;
}

std::vector<PexInstruction> doNothing() { return {ins("return", {id("::NoneVar")})}; }

VmadValueShape intProperty(std::string name, std::int64_t number) {
  VmadValueShape shape;
  shape.name = std::move(name);
  shape.type = "int";
  shape.integer = number;
  return shape;
}

class LifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    scripts_.push_back(script("Reader", {ins("propget", {id("Count"), id("self"), id("result")}),
                                         ins("return", {id("::NoneVar")})}));
    scripts_.push_back(script("Plain", doNothing()));
    scripts_.push_back(script("Second", doNothing()));
    scripts_.push_back(script("Boom", {ins("callstatic", {literal("Missing"), literal("Boom"), id("::nonevar")}),
                                       ins("return", {id("::NoneVar")})}));
    scripts_.push_back(script("Orphan", doNothing(), "NotLoaded"));
    std::vector<const PexObject*> pointers;
    for (const PexObject& object : scripts_) pointers.push_back(&object);
    runtime_ = std::make_unique<Runtime>(std::move(pointers));
  }

  void gateOff(const std::string& blocked) {
    lifecycle_ = std::make_unique<QuestLifecycle>(*runtime_, [blocked](std::string_view key) { return key != blocked; });
  }

  QuestLifecycle& lifecycle() {
    if (!lifecycle_) lifecycle_ = std::make_unique<QuestLifecycle>(*runtime_, nullptr);
    return *lifecycle_;
  }

  static QuestStartupInput quest(std::string key, std::vector<std::string> scripts) {
    QuestStartupInput input;
    input.quest = std::move(key);
    input.scripts = std::move(scripts);
    return input;
  }

  std::vector<PexObject> scripts_;
  std::unique_ptr<Runtime> runtime_;
  std::unique_ptr<QuestLifecycle> lifecycle_;
};

// Detects: quest properties that must be bound before OnInit, so OnInit never reads an unset property.
TEST_F(LifecycleTest, QuestPropertiesAreBoundBeforeOnInitRuns) {
  QuestStartupInput input = quest("000800:Quest.esp", {"Reader"});
  input.properties.push_back(QuestStartupProperty{"Reader", intProperty("Count", 5)});

  const auto result = lifecycle().start({input});

  ASSERT_TRUE(result.applied);
  EXPECT_EQ(result.started, 1);
  EXPECT_EQ(result.attachedScripts, 1);
  const auto instances = lifecycle().instancesFor("000800:Quest.esp");
  ASSERT_EQ(instances.size(), 1u);
  EXPECT_EQ(runtime_->lifecycle(instances[0]), Lifecycle::Ready);
  const auto seen = runtime_->instanceVariable(instances[0], "result");
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->integer, 5);
}

// Detects: a batch that must attach nothing while any quest in it is blocked by the gate.
TEST_F(LifecycleTest, ABlockedQuestKeepsTheWholeBatchFromAttaching) {
  gateOff("quest.runtime.fragment_dispatch_timing");
  QuestStartupInput fragmented = quest("QB", {"Plain"});
  fragmented.fragments = 2;

  const auto plan = lifecycle().preflight({quest("QA", {"Plain"}), fragmented});
  const auto result = lifecycle().start({quest("QA", {"Plain"}), fragmented});

  EXPECT_FALSE(plan.canApply());
  EXPECT_EQ(plan.ready, 1);
  EXPECT_EQ(plan.blocked, 1);
  EXPECT_EQ(plan.outcomes[1].state, QuestStartupState::Blocked);
  EXPECT_FALSE(result.applied);
  ASSERT_EQ(result.reasons.size(), 1u);
  EXPECT_EQ(result.reasons[0], "QB: the dispatch timing of stage fragments is unproven");
  EXPECT_TRUE(lifecycle().instancesFor("QA").empty());
  EXPECT_FALSE(lifecycle().isStarted("QA"));
  EXPECT_EQ(lifecycle().counters().blocked, 1);
  EXPECT_EQ(lifecycle().counters().approximations, 1);
  EXPECT_EQ(plan.digest().size(), 64u);
}

// Detects: script defects that must refuse the batch before any quest attaches.
TEST_F(LifecycleTest, ScriptDefectsRefuseTheBatchWithNothingAttached) {
  const auto missing = lifecycle().start({quest("QA", {"Plain"}), quest("QB", {"Nope"})});
  ASSERT_FALSE(missing.applied);
  ASSERT_EQ(missing.reasons.size(), 1u);
  EXPECT_EQ(missing.reasons[0], "missing script Nope");

  const auto orphan = lifecycle().start({quest("QA", {"Plain"}), quest("QC", {"Orphan"})});
  ASSERT_FALSE(orphan.applied);
  ASSERT_EQ(orphan.reasons.size(), 1u);
  EXPECT_EQ(orphan.reasons[0], "QC: missing or cyclic parent chain in a quest script");

  QuestStartupInput textless = quest("QD", {"Reader"});
  VmadValueShape noText;
  noText.name = "Label";
  noText.type = "string";
  textless.properties.push_back(QuestStartupProperty{"Reader", noText});
  const auto bad = lifecycle().start({quest("QA", {"Plain"}), textless});
  ASSERT_FALSE(bad.applied);
  EXPECT_EQ(bad.reasons[0], "QD: a string property carried no value");

  EXPECT_TRUE(lifecycle().instancesFor("QA").empty());
  EXPECT_TRUE(runtime_->instancesForOwner(OwnerId::forForm("QA")).empty());
}

// Detects: scripted quests that must be refused while the OnInit barrier is unproven.
TEST_F(LifecycleTest, AnUnprovenOnInitBarrierRefusesScriptedQuests) {
  gateOff("vm.lifecycle.oninit_barrier");

  const auto result = lifecycle().start({quest("QA", {"Plain"})});

  EXPECT_FALSE(result.applied);
  ASSERT_EQ(result.reasons.size(), 1u);
  EXPECT_EQ(result.reasons[0], "QA: vm.lifecycle.oninit_barrier: the OnInit barrier is unproven");
  EXPECT_TRUE(lifecycle().instancesFor("QA").empty());
}

// Detects: duplicate quests that must block, and started or disabled quests that must be skipped.
TEST_F(LifecycleTest, DuplicatesBlockWhileStartedAndDisabledQuestsAreSkipped) {
  const auto duplicate = lifecycle().preflight({quest("QA", {"Plain"}), quest("QA", {"Plain"})});
  EXPECT_EQ(duplicate.blocked, 2);
  EXPECT_EQ(duplicate.outcomes[0].reason, "duplicate quest inputs are unsupported");

  ASSERT_TRUE(lifecycle().start({quest("QA", {"Plain"})}).applied);
  QuestStartupInput disabled = quest("QB", {"Plain"});
  disabled.startGameEnabled = false;

  const auto again = lifecycle().start({quest("QA", {"Plain"}), disabled});

  EXPECT_TRUE(again.applied);
  EXPECT_EQ(again.started, 0);
  EXPECT_EQ(lifecycle().counters().skipped, 2);
  EXPECT_EQ(lifecycle().instancesFor("QA").size(), 1u);
  EXPECT_FALSE(lifecycle().isStarted("QB"));
}

// Detects: a failed OnInit that must stay memoised until stop clears it.
TEST_F(LifecycleTest, AFailedOnInitIsMemoisedUntilTheQuestIsStopped) {
  const auto result = lifecycle().start({quest("QBoom", {"Boom"})});

  ASSERT_FALSE(result.applied);
  ASSERT_EQ(result.reasons.size(), 1u);
  EXPECT_EQ(result.reasons[0].rfind("QBoom Boom: ", 0), 0u);
  EXPECT_FALSE(lifecycle().isStarted("QBoom"));
  const auto memo = lifecycle().preflight({quest("QBoom", {"Plain"})});
  EXPECT_EQ(memo.blocked, 1);

  EXPECT_FALSE(lifecycle().stop("QBoom"));
  EXPECT_EQ(lifecycle().preflight({quest("QBoom", {"Plain"})}).ready, 1);
  EXPECT_TRUE(lifecycle().start({quest("QBoom", {"Plain"})}).applied);
}

// Detects: stop that must detach the quest scripts and allow a restart.
TEST_F(LifecycleTest, StopDetachesTheScriptsAndAllowsARestart) {
  ASSERT_TRUE(lifecycle().start({quest("QA", {"Plain", "Second"})}).applied);
  const auto first = lifecycle().instancesFor("QA");
  ASSERT_EQ(first.size(), 2u);

  EXPECT_TRUE(lifecycle().stop("QA"));

  EXPECT_FALSE(lifecycle().isStarted("QA"));
  EXPECT_TRUE(lifecycle().instancesFor("QA").empty());
  EXPECT_TRUE(runtime_->instanceDetached(first[0]));
  EXPECT_TRUE(runtime_->instanceDetached(first[1]));
  EXPECT_FALSE(lifecycle().stop("QA"));
  ASSERT_TRUE(lifecycle().start({quest("QA", {"Plain", "Second"})}).applied);
  EXPECT_NE(lifecycle().instancesFor("QA")[0], first[0]);
}

// Detects: alias attach counts, ordering and refusal reasons that must match the verified GE alias runtime.
TEST_F(LifecycleTest, AliasScriptsAttachInKeyOrderWithCountedRefusals) {
  AliasScriptInput plain{"Q", 2, std::string("Plain"), {}, {3, 1, 3}};
  AliasScriptInput none{"Q", 1, std::nullopt, {}, {}};
  AliasScriptInput missing{"Q", 5, std::string("Nope"), {}, {}};
  AliasScriptInput counted{"Q", 3, std::string("Reader"), {intProperty("Count", 9)}, {}};

  const auto report = lifecycle().attachAliases({plain, missing, counted, none});

  EXPECT_EQ(report.attached, 2);
  EXPECT_EQ(report.skipped, 1);
  EXPECT_EQ(report.refused, 1);
  EXPECT_EQ(report.properties, 1);
  EXPECT_EQ(report.natives, 1);
  EXPECT_EQ(report.objectives, 2);
  ASSERT_EQ(report.aliases.size(), 4u);
  EXPECT_EQ(report.aliases[0].owner, OwnerId::forQuestAlias("Q", 1));
  EXPECT_EQ(report.aliases[0].state, AliasAttachState::Skipped);
  EXPECT_EQ(report.aliases[3].state, AliasAttachState::Refused);
  EXPECT_EQ(report.aliases[3].reason, "missing script Nope");
  EXPECT_EQ(lifecycle().objectiveOf(OwnerId::forQuestAlias("Q", 2), 3), 1);
  EXPECT_EQ(lifecycle().objectiveOf(OwnerId::forQuestAlias("Q", 2), 2), -1);
  const auto reader = lifecycle().aliasInstance(OwnerId::forQuestAlias("Q", 3));
  ASSERT_TRUE(reader.has_value());
  EXPECT_EQ(runtime_->instanceVariable(*reader, "result")->integer, 9);

  const auto second = lifecycle().attachAliases({plain});
  EXPECT_EQ(second.refused, 1);
  EXPECT_EQ(second.aliases[0].reason, "the alias already has an attached script");
  EXPECT_EQ(report.digest().size(), 64u);
}

// Detects: alias scripts that must be refused while the alias runtime evidence is unproven.
TEST_F(LifecycleTest, AnUnprovenAliasRuntimeRefusesEveryAliasScript) {
  gateOff("quest.alias.runtime");

  const auto report = lifecycle().attachAliases({AliasScriptInput{"Q", 1, std::string("Plain"), {}, {}}});

  EXPECT_EQ(report.refused, 1);
  EXPECT_EQ(report.aliases[0].reason, "unverified quest.alias.runtime");
  EXPECT_FALSE(lifecycle().aliasInstance(OwnerId::forQuestAlias("Q", 1)).has_value());
}

// Detects: actor aliases that must run their script only when bound.
TEST_F(LifecycleTest, ActorAliasesRunTheirScriptOnlyWhenBound) {
  ActorAliasInput boundWithScript{AliasScriptInput{"Q", 1, std::string("Plain"), {}, {}}, true, std::string("Actor:0A")};
  ActorAliasInput boundNoScript{AliasScriptInput{"Q", 2, std::nullopt, {}, {}}, true, std::string("Actor:0B")};
  ActorAliasInput unbound{AliasScriptInput{"Q", 3, std::string("Plain"), {}, {}}, false, std::nullopt};
  ActorAliasInput refused{AliasScriptInput{"Q", 4, std::string("Nope"), {}, {}}, true, std::string("Actor:0C")};

  const auto report = lifecycle().bindActors({refused, unbound, boundNoScript, boundWithScript});

  EXPECT_EQ(report.bound, 3);
  EXPECT_EQ(report.unbound, 1);
  EXPECT_EQ(report.attached, 1);
  EXPECT_EQ(report.refused, 1);
  ASSERT_EQ(report.unresolved.size(), 1u);
  EXPECT_EQ(report.aliases[0].state, ActorBindState::Attached);
  EXPECT_EQ(report.aliases[0].reason, "the alias runs its script against its actor");
  EXPECT_EQ(report.aliases[1].state, ActorBindState::Bound);
  EXPECT_EQ(report.aliases[2].state, ActorBindState::Unbound);
  EXPECT_EQ(report.aliases[3].reason, "the alias is bound to its actor but script properties refused: missing script Nope");
  EXPECT_EQ(lifecycle().aliasActor(OwnerId::forQuestAlias("Q", 1)), std::string("Actor:0A"));
  EXPECT_FALSE(lifecycle().aliasInstance(OwnerId::forQuestAlias("Q", 3)).has_value());
}

// Detects: clearing an alias that must detach its script, and stop that must clear the quest aliases.
TEST_F(LifecycleTest, ClearingAnAliasDetachesItsScriptAndStopClearsTheQuestAliases) {
  ASSERT_TRUE(lifecycle().start({quest("Q", {"Plain"})}).applied);
  lifecycle().attachAliases({AliasScriptInput{"Q", 1, std::string("Plain"), {}, {1}},
                             AliasScriptInput{"Q", 2, std::string("Plain"), {}, {}},
                             AliasScriptInput{"Other", 1, std::string("Plain"), {}, {}}});
  const auto one = lifecycle().aliasInstance(OwnerId::forQuestAlias("Q", 1));
  ASSERT_TRUE(one.has_value());

  EXPECT_TRUE(lifecycle().clearAlias(OwnerId::forQuestAlias("Q", 1)));
  EXPECT_FALSE(lifecycle().clearAlias(OwnerId::forQuestAlias("Q", 1)));
  EXPECT_TRUE(runtime_->instanceDetached(*one));
  EXPECT_EQ(lifecycle().objectiveOf(OwnerId::forQuestAlias("Q", 1), 1), -1);

  EXPECT_TRUE(lifecycle().stop("Q"));
  EXPECT_FALSE(lifecycle().aliasInstance(OwnerId::forQuestAlias("Q", 2)).has_value());
  EXPECT_TRUE(lifecycle().aliasInstance(OwnerId::forQuestAlias("Other", 1)).has_value());
  EXPECT_EQ(lifecycle().clearQuestAliases("Other"), 1);
}

}  // namespace
