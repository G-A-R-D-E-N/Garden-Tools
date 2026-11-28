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
using fo4::papyrus::ValueType;
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

TEST(PapyrusQuestRules, BindsIntAndObjectArraysAndAStructAndRefusesVariables) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape ints;
  ints.name = "Counts";
  ints.type = "Array";
  ints.elementType = "Int";
  ints.elementCount = 3;
  for (int value : {4, 5, 6}) {
    VmadValueShape element;
    element.integer = value;
    ints.elements.push_back(element);
  }
  VmadValueShape refs;
  refs.name = "Refs";
  refs.type = "Array";
  refs.elementType = "Object";
  refs.elementCount = 2;
  for (const char* ref : {"0002:Actor.esp", "0003:Actor.esp"}) {
    VmadValueShape element;
    element.object = ref;
    refs.elements.push_back(element);
  }
  VmadValueShape speed;
  speed.name = "Speed";
  speed.type = "Int";
  speed.integer = 7;
  VmadValueShape label;
  label.name = "Label";
  label.type = "String";
  label.text = "go";
  VmadValueShape config;
  config.name = "Config";
  config.type = "Struct";
  config.elementCount = 2;
  config.members = {speed, label};
  VmadValueShape any;
  any.name = "Any";
  any.type = "Variable";
  VmadValueShape anys;
  anys.name = "Anys";
  anys.type = "Array";
  anys.elementType = "Variable";
  anys.elementCount = 0;

  const auto report = bindVmadValues(owner, {ints, refs, config, any, anys});
  ASSERT_EQ(report.properties.size(), 5u);
  EXPECT_EQ(report.properties[0].name, "Any");
  EXPECT_EQ(report.properties[0].kind, BoundKind::Unsupported);
  EXPECT_NE(report.properties[0].reason.value_or(std::string()).find("runtime type is unknown"),
            std::string::npos);
  EXPECT_EQ(report.properties[1].name, "Anys");
  EXPECT_EQ(report.properties[1].kind, BoundKind::Unsupported);
  EXPECT_NE(report.properties[1].reason.value_or(std::string()).find("not a bindable scalar type"),
            std::string::npos);

  EXPECT_EQ(report.properties[2].name, "Config");
  EXPECT_EQ(report.properties[2].kind, BoundKind::Struct);
  ASSERT_EQ(report.properties[2].value.type, ValueType::Struct);
  const auto& members = report.properties[2].value.structure->members;
  ASSERT_EQ(members.size(), 2u);
  EXPECT_EQ(members.at("speed").integer, 7);
  EXPECT_EQ(members.at("label").str, "go");

  EXPECT_EQ(report.properties[3].name, "Counts");
  EXPECT_EQ(report.properties[3].kind, BoundKind::Array);
  ASSERT_EQ(report.properties[3].value.type, ValueType::Array);
  EXPECT_EQ(report.properties[3].value.array->elementType, ValueType::Integer);
  const auto& counts = report.properties[3].value.array->elements;
  ASSERT_EQ(counts.size(), 3u);
  EXPECT_EQ(counts[0].integer, 4);
  EXPECT_EQ(counts[2].integer, 6);

  EXPECT_EQ(report.properties[4].name, "Refs");
  EXPECT_EQ(report.properties[4].kind, BoundKind::Array);
  const auto& targets = report.properties[4].value.array->elements;
  ASSERT_EQ(targets.size(), 2u);
  EXPECT_EQ(targets[1].object->type, "0003:Actor.esp");

  EXPECT_EQ(report.bound, 3);
  EXPECT_EQ(report.unsupported, 2);
  EXPECT_TRUE(report.clean());
}

VmadValueShape scalarShape(const std::string& name, const std::string& type) {
  VmadValueShape shape;
  shape.name = name;
  shape.type = type;
  return shape;
}

std::string reasonOf(const fo4::papyrus::VmadBindReport& report, const std::string& name) {
  for (const auto& property : report.properties) {
    if (property.name == name) return property.reason.value_or(std::string());
  }
  return "<missing>";
}

TEST(PapyrusQuestRules, RefusalReasonsStartWithThePascalCaseKindAndNameTheCount) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape items = scalarShape("Items", "Array");
  items.elementCount = 3;
  VmadValueShape pair = scalarShape("Pair", "Struct");
  pair.elementCount = 2;
  VmadValueShape alias = scalarShape("Target", "Object");
  alias.hasAlias = true;
  VmadValueShape missing = scalarShape("Count", "Int");
  const auto report = bindVmadValues(
      owner, {items, pair, alias, scalarShape("Anything", "Variable"), scalarShape("Odd", "Mystery"), missing});
  EXPECT_EQ(reasonOf(report, "Items").rfind("Array: ", 0), 0u);
  EXPECT_NE(reasonOf(report, "Items").find("3 elements"), std::string::npos);
  EXPECT_EQ(reasonOf(report, "Pair").rfind("Struct: ", 0), 0u);
  EXPECT_NE(reasonOf(report, "Pair").find("2 members"), std::string::npos);
  EXPECT_EQ(reasonOf(report, "Target").rfind("AliasBound: ", 0), 0u);
  EXPECT_EQ(reasonOf(report, "Anything").rfind("Variable: ", 0), 0u);
  EXPECT_EQ(reasonOf(report, "Odd").rfind("Unknown: ", 0), 0u);
  EXPECT_EQ(reasonOf(report, "Count").rfind("Int: ", 0), 0u);
}

TEST(PapyrusQuestRules, AnEmptyStructIsRefusedAndAnEmptyArrayIsAnExplicitValue) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape empty = scalarShape("Empty", "Struct");
  empty.elementCount = 0;
  VmadValueShape none = scalarShape("None", "Array");
  none.elementType = "Int";
  none.elementCount = 0;
  const auto report = bindVmadValues(owner, {empty, none});
  EXPECT_EQ(report.properties[0].name, "Empty");
  EXPECT_EQ(report.properties[0].kind, BoundKind::Unsupported);
  EXPECT_NE(reasonOf(report, "Empty").find("0 members"), std::string::npos);
  EXPECT_EQ(report.properties[1].kind, BoundKind::Array);
  EXPECT_EQ(report.bound, 1);
  EXPECT_EQ(report.unsupported, 1);
}

TEST(PapyrusQuestRules, TheReportListsEveryUnsupportedShapeWithItsOwner) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape alias = scalarShape("Target", "Object");
  alias.hasAlias = true;
  VmadValueShape count = scalarShape("Count", "Int");
  count.integer = 1;
  const auto report = bindVmadValues(owner, {alias, count, scalarShape("Anything", "Variable")});
  ASSERT_EQ(report.unsupportedShapes.size(), 2u);
  EXPECT_EQ(report.unsupportedShapes[0].rfind(owner.key() + " Anything: Variable: ", 0), 0u);
  EXPECT_EQ(report.unsupportedShapes[1].rfind(owner.key() + " Target: AliasBound: ", 0), 0u);
  EXPECT_EQ(static_cast<std::int32_t>(report.unsupportedShapes.size()), report.unsupported);
}

TEST(PapyrusQuestRules, ValuesThatCannotBeKeptExactlyAreRefusedNotTruncated) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape big = scalarShape("Big", "Int");
  big.integer = 4294967296LL;
  VmadValueShape low = scalarShape("Low", "Int");
  low.integer = -2147483648LL;
  VmadValueShape high = scalarShape("High", "Int");
  high.integer = 2147483647LL;
  VmadValueShape tenth = scalarShape("Tenth", "Float");
  tenth.real = 0.1;
  VmadValueShape half = scalarShape("Half", "Float");
  half.real = 1.5;
  VmadValueShape huge = scalarShape("Huge", "Float");
  huge.real = 1e300;
  const auto report = bindVmadValues(owner, {big, low, high, tenth, half, huge});
  EXPECT_EQ(report.bound, 3);
  EXPECT_EQ(report.unsupported, 3);
  EXPECT_NE(reasonOf(report, "Big").find("does not fit a 32-bit"), std::string::npos);
  EXPECT_EQ(reasonOf(report, "Big").rfind("Int: ", 0), 0u);
  EXPECT_EQ(reasonOf(report, "Tenth").rfind("Float: ", 0), 0u);
  EXPECT_EQ(reasonOf(report, "Huge").rfind("Float: ", 0), 0u);
  for (const auto& property : report.properties) {
    if (property.name == "Low") {
      EXPECT_EQ(property.value.integer, -2147483647 - 1);
    }
    if (property.name == "High") {
      EXPECT_EQ(property.value.integer, 2147483647);
    }
    if (property.name == "Half") {
      EXPECT_FLOAT_EQ(property.value.real, 1.5F);
    }
  }
}

VmadValueShape structElement(int speed, const std::string& label) {
  VmadValueShape element = scalarShape("", "Struct");
  element.elementCount = 2;
  VmadValueShape speedMember = scalarShape("Speed", "Int");
  speedMember.integer = speed;
  VmadValueShape labelMember = scalarShape("Label", "String");
  labelMember.text = label;
  element.members = {speedMember, labelMember};
  return element;
}

// Constraint: detects a quest's array-of-struct property being refused as a non-scalar element type, or its structs losing their members.
TEST(PapyrusQuestRules, ABoundStructArrayKeepsEachElementsMembersIncludingNestedOnes) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape rows = scalarShape("Rows", "Array");
  rows.elementType = "struct";
  VmadValueShape nested = structElement(9, "inner");
  VmadValueShape holder = scalarShape("", "Struct");
  holder.elementCount = 2;
  VmadValueShape inner = nested;
  inner.name = "Inner";
  VmadValueShape tags = scalarShape("Tags", "Array");
  tags.elementType = "Int";
  tags.elementCount = 2;
  for (int value : {3, 4}) {
    VmadValueShape tag = scalarShape("", "Int");
    tag.integer = value;
    tags.elements.push_back(tag);
  }
  holder.members = {inner, tags};
  rows.elements = {structElement(1, "a"), structElement(2, "b"), holder};
  rows.elementCount = 3;
  VmadValueShape none = scalarShape("None", "Array");
  none.elementType = "Struct";
  none.elementCount = 0;

  const auto report = bindVmadValues(owner, {rows, none});
  EXPECT_EQ(report.bound, 2);
  EXPECT_EQ(report.unsupported, 0);
  for (const auto& property : report.properties) {
    if (property.name == "None") {
      EXPECT_EQ(property.kind, BoundKind::Array);
      EXPECT_TRUE(property.value.array->elements.empty());
      continue;
    }
    ASSERT_EQ(property.name, "Rows");
    ASSERT_EQ(property.kind, BoundKind::Array);
    const auto& array = *property.value.array;
    EXPECT_EQ(array.elementType, fo4::papyrus::ValueType::Struct);
    ASSERT_EQ(array.elements.size(), 3u);
    EXPECT_EQ(array.elements[0].structure->members.at("speed").integer, 1);
    EXPECT_EQ(array.elements[1].structure->members.at("label").str, "b");
    const auto& held = array.elements[2].structure->members;
    EXPECT_EQ(held.at("inner").structure->members.at("speed").integer, 9);
    ASSERT_EQ(held.at("tags").array->elements.size(), 2u);
    EXPECT_EQ(held.at("tags").array->elements[1].integer, 4);
  }
}

// Constraint: detects a struct array binding partially or silently defaulting when one element or member cannot be bound.
TEST(PapyrusQuestRules, AStructArrayIsRefusedWholeWithThePreciseReasonOfTheBadElement) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape variableMember = structElement(1, "a");
  variableMember.members[1] = scalarShape("Label", "Variable");
  VmadValueShape undecoded = structElement(2, "b");
  undecoded.elementCount = 5;
  VmadValueShape duplicate = structElement(3, "c");
  duplicate.members[1] = duplicate.members[0];
  VmadValueShape aliasMember = structElement(4, "d");
  aliasMember.members[1] = scalarShape("Label", "Object");
  aliasMember.members[1].hasAlias = true;

  VmadValueShape badMember = scalarShape("BadMember", "Array");
  badMember.elementType = "Struct";
  badMember.elementCount = 2;
  badMember.elements = {structElement(0, "ok"), variableMember};
  VmadValueShape badCount = scalarShape("BadCount", "Array");
  badCount.elementType = "Struct";
  badCount.elementCount = 1;
  badCount.elements = {undecoded};
  VmadValueShape repeated = scalarShape("Repeated", "Array");
  repeated.elementType = "Struct";
  repeated.elementCount = 1;
  repeated.elements = {duplicate};
  VmadValueShape aliased = scalarShape("Aliased", "Array");
  aliased.elementType = "Struct";
  aliased.elementCount = 1;
  aliased.elements = {aliasMember};
  VmadValueShape undeclared = scalarShape("Undeclared", "Array");
  undeclared.elementType = "Struct";
  undeclared.elementCount = 2;
  undeclared.elements = {structElement(0, "x")};

  const auto report = bindVmadValues(owner, {badMember, badCount, repeated, aliased, undeclared});
  EXPECT_EQ(report.bound, 0);
  EXPECT_EQ(report.unsupported, 5);
  EXPECT_EQ(reasonOf(report, "BadMember"),
            "Array: element 1 Struct: member Label Variable: a variable property's runtime type is unknown");
  EXPECT_EQ(reasonOf(report, "BadCount").rfind("Array: element 0 Struct: ", 0), 0u);
  EXPECT_NE(reasonOf(report, "BadCount").find("5 members"), std::string::npos);
  EXPECT_NE(reasonOf(report, "Repeated").find("repeats member Speed"), std::string::npos);
  EXPECT_NE(reasonOf(report, "Aliased").find("quest alias"), std::string::npos);
  EXPECT_NE(reasonOf(report, "Undeclared").find("2 elements are not decoded yet"), std::string::npos);
  for (const auto& property : report.properties) EXPECT_FALSE(property.isBound());
}

// Constraint: detects a dangling or null object FormID refusing a whole quest's property batch, when the VM stores such an object as None.
TEST(PapyrusQuestRules, AnObjectWithNoReferenceBindsAsNoneAtEveryDepth) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape dangling = scalarShape("Dangling", "Object");
  VmadValueShape refs = scalarShape("Refs", "Array");
  refs.elementType = "Object";
  refs.elementCount = 2;
  VmadValueShape element = scalarShape("", "Object");
  element.object = "0002:Actor.esp";
  refs.elements.push_back(element);
  refs.elements.push_back(scalarShape("", "Object"));
  VmadValueShape member = scalarShape("Owner", "Object");
  VmadValueShape config = scalarShape("Config", "Struct");
  config.elementCount = 1;
  config.members.push_back(member);
  const auto report = bindVmadValues(owner, {dangling, refs, config});
  EXPECT_EQ(report.bound, 3);
  EXPECT_EQ(report.unsupported, 0);
  for (const auto& property : report.properties) {
    if (property.name == "Dangling") {
      EXPECT_EQ(property.kind, BoundKind::Object);
      EXPECT_EQ(property.value.type, fo4::papyrus::ValueType::Object);
      EXPECT_TRUE(fo4::papyrus::isNoneLike(property.value));
    }
    if (property.name == "Refs") {
      ASSERT_EQ(property.value.array->elements.size(), 2u);
      EXPECT_FALSE(fo4::papyrus::isNoneLike(property.value.array->elements[0]));
      EXPECT_TRUE(fo4::papyrus::isNoneLike(property.value.array->elements[1]));
    }
    if (property.name == "Config") {
      EXPECT_TRUE(fo4::papyrus::isNoneLike(property.value.structure->members.at("owner")));
    }
  }
}

TEST(PapyrusQuestRules, ABlankObjectReferenceIsRefused) {
  const OwnerId owner = OwnerId::forForm("0001:Q.esp");
  VmadValueShape blank = scalarShape("Blank", "Object");
  blank.object = "  ";
  const auto report = bindVmadValues(owner, {blank});
  EXPECT_EQ(report.unsupported, 1);
  EXPECT_EQ(reasonOf(report, "Blank").rfind("Object: ", 0), 0u);
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
