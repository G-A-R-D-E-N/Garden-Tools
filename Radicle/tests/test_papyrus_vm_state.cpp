#include "fo4/papyrus/runtime.hpp"
#include "fo4/papyrus/vm_state.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::Array;
using fo4::papyrus::InstanceId;
using fo4::papyrus::InstanceIdentity;
using fo4::papyrus::InstanceStateCapture;
using fo4::papyrus::InstanceStateOptions;
using fo4::papyrus::InstanceStateRefusal;
using fo4::papyrus::InstanceStateReport;
using fo4::papyrus::InstanceStateResult;
using fo4::papyrus::OwnerId;
using fo4::papyrus::Runtime;
using fo4::papyrus::Struct;
using fo4::papyrus::Value;
using fo4::papyrus::ValueType;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexVariable;

PexVariable variable(std::string name, std::string type) {
  PexVariable out;
  out.name = std::move(name);
  out.type = std::move(type);
  return out;
}

struct World {
  PexObject base;
  PexObject quest;
  PexObject helper;
  std::unique_ptr<Runtime> runtime;

  World() {
    base.name = "Base";
    base.variables = {variable("Inherited", "Int")};
    quest.name = "Quest";
    quest.parentClassName = "Base";
    quest.variables = {variable("::Count_var", "Int"),   variable("Label", "String"), variable("Ratio", "Float"),
                       variable("Flag", "Bool"),         variable("Items", "Int[]"),  variable("Other", "Int[]"),
                       variable("Loop", "Var[]"),        variable("Link", "Helper"),  variable("Snap", "Pair"),
                       variable("Anything", "Var"),     variable("Up", "Base"),       variable("Reals", "Float[]")};
    PexState busy;
    busy.name = "Busy";
    quest.states.push_back(busy);
    helper.name = "Helper";
    helper.variables = {variable("Tag", "String"), variable("Back", "Quest")};
    runtime = std::make_unique<Runtime>(std::vector<const PexObject*>{&base, &quest, &helper});
  }

  InstanceId attach(const char* script, const OwnerId& owner) {
    const auto id = runtime->attach(script, owner);
    EXPECT_TRUE(id.has_value());
    return id.value_or(0);
  }

  void set(InstanceId id, const char* name, Value value) { ASSERT_TRUE(runtime->setInstanceVariable(id, name, std::move(value))); }
};

const OwnerId kQuest = OwnerId::forForm("000800:Quest.esp");
const OwnerId kHelper = OwnerId::forForm("000801:Helper.esp");
const OwnerId kAlias = OwnerId::forQuestAlias("000800:Quest.esp", 3);

std::string blobOf(const Runtime& runtime) {
  InstanceStateCapture capture;
  const InstanceStateResult result = fo4::papyrus::captureInstanceState(runtime, capture);
  EXPECT_TRUE(result.ok()) << result.reason;
  return capture.blob;
}

InstanceStateResult restore(Runtime& runtime, const std::string& blob, bool skip = false, InstanceStateReport* report = nullptr,
                            std::map<std::uint64_t, std::uint64_t> remap = {}) {
  InstanceStateOptions options;
  options.skipUnknown = skip;
  options.engineRemap = std::move(remap);
  InstanceStateReport local;
  return fo4::papyrus::restoreInstanceState(runtime, blob, options, report != nullptr ? *report : local);
}

InstanceStateResult restoreOnly(Runtime& runtime, const std::string& blob, std::vector<InstanceIdentity> only, bool skip = false,
                                InstanceStateReport* report = nullptr) {
  InstanceStateOptions options;
  options.skipUnknown = skip;
  options.only = std::move(only);
  InstanceStateReport local;
  return fo4::papyrus::restoreInstanceState(runtime, blob, options, report != nullptr ? *report : local);
}

std::uint32_t bitsOf(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

std::string replaceOnce(std::string text, const std::string& from, const std::string& to) {
  const std::size_t at = text.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

TEST(PapyrusVmState, RoundTripsScalarsStringsAndFloatEdgeCasesBitExactly) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  const std::string tricky = std::string("a\x01\"\\ \xff\x7f z") + '\0' + "end";
  a.set(quest, "Label", fo4::papyrus::stringValue(tricky));
  a.set(quest, "::Count_var", fo4::papyrus::integerValue(std::numeric_limits<std::int32_t>::min()));
  a.set(quest, "Ratio", fo4::papyrus::floatValue(-0.0F));
  a.set(quest, "Flag", fo4::papyrus::boolValue(true));
  a.set(quest, "Inherited", fo4::papyrus::integerValue(77));
  const std::string blob = blobOf(*a.runtime);
  for (const char ch : blob) EXPECT_TRUE(ch == '\n' || (ch >= 0x20 && ch < 0x7F));

  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  InstanceStateReport report;
  ASSERT_TRUE(restore(*b.runtime, blob, false, &report).ok());
  EXPECT_EQ(report.instances, 1);
  EXPECT_EQ(report.variables, 5);
  EXPECT_EQ(b.runtime->instanceVariable(target, "Label")->str, tricky);
  EXPECT_EQ(b.runtime->instanceVariable(target, "::count_var")->integer, std::numeric_limits<std::int32_t>::min());
  EXPECT_EQ(bitsOf(b.runtime->instanceVariable(target, "Ratio")->real), bitsOf(-0.0F));
  EXPECT_TRUE(b.runtime->instanceVariable(target, "Flag")->boolean);
  EXPECT_EQ(b.runtime->instanceVariable(target, "Inherited")->integer, 77);
  EXPECT_EQ(blobOf(*b.runtime), blob);
}

TEST(PapyrusVmState, FloatNanAndInfinityKeepTheirBitPatterns) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  a.set(quest, "Ratio", fo4::papyrus::floatValue(nan));
  const std::string blob = blobOf(*a.runtime);
  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  ASSERT_TRUE(restore(*b.runtime, blob).ok());
  EXPECT_EQ(bitsOf(b.runtime->instanceVariable(target, "Ratio")->real), bitsOf(nan));
  a.set(quest, "Ratio", fo4::papyrus::floatValue(std::numeric_limits<float>::infinity()));
  ASSERT_TRUE(restore(*b.runtime, blobOf(*a.runtime)).ok());
  EXPECT_EQ(bitsOf(b.runtime->instanceVariable(target, "Ratio")->real), bitsOf(std::numeric_limits<float>::infinity()));
}

TEST(PapyrusVmState, ObjectReferencesLandOnTheRightInstanceWhenIdsAndAttachOrderDiffer) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  const InstanceId helperA = a.attach("Helper", kHelper);
  a.set(questA, "Link", fo4::papyrus::objectValue(helperA, "Helper", ""));
  a.set(helperA, "Back", fo4::papyrus::objectValue(questA, "Quest", ""));
  a.set(questA, "Up", fo4::papyrus::objectValue(questA, "Base", ""));
  const std::string blob = blobOf(*a.runtime);

  World b;
  b.attach("Helper", OwnerId::forForm("0009ff:Decoy.esp"));
  b.attach("Quest", kAlias);
  const InstanceId helperB = b.attach("Helper", kHelper);
  const InstanceId questB = b.attach("Quest", kQuest);
  ASSERT_NE(helperB, helperA);
  ASSERT_NE(questB, questA);
  ASSERT_TRUE(restore(*b.runtime, blob).ok());
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Link")->object->handle, helperB);
  EXPECT_EQ(b.runtime->instanceVariable(helperB, "Back")->object->handle, questB);
  EXPECT_EQ(b.runtime->instanceVariable(helperB, "Back")->object->type, "Quest");
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Up")->object->handle, questB);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Up")->object->type, "Base");
}

TEST(PapyrusVmState, TheSameLogicalStateGivesAnIdenticalBlobAndDigestInAnyAttachOrder) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  const InstanceId helperA = a.attach("Helper", kHelper);
  a.set(questA, "Link", fo4::papyrus::objectValue(helperA, "Helper", ""));
  a.set(helperA, "Tag", fo4::papyrus::stringValue("t"));
  World b;
  b.attach("Quest", kAlias);
  const InstanceId helperB = b.attach("Helper", kHelper);
  const InstanceId questB = b.attach("Quest", kQuest);
  b.set(questB, "Link", fo4::papyrus::objectValue(helperB, "Helper", ""));
  b.set(helperB, "Tag", fo4::papyrus::stringValue("t"));
  InstanceStateCapture first;
  InstanceStateCapture second;
  World c;
  c.attach("Quest", kAlias);
  const InstanceId questC = c.attach("Quest", kQuest);
  const InstanceId helperC = c.attach("Helper", kHelper);
  c.set(questC, "Link", fo4::papyrus::objectValue(helperC, "Helper", ""));
  c.set(helperC, "Tag", fo4::papyrus::stringValue("t"));
  ASSERT_TRUE(fo4::papyrus::captureInstanceState(*b.runtime, first).ok());
  ASSERT_TRUE(fo4::papyrus::captureInstanceState(*c.runtime, second).ok());
  EXPECT_EQ(first.blob, second.blob);
  EXPECT_EQ(first.digest, second.digest);
  EXPECT_EQ(first.digest.size(), 64u);
}

TEST(PapyrusVmState, ArraysAndStructsKeepSharingAndCyclesRoundTrip) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  auto shared = std::make_shared<Array>();
  shared->elementType = ValueType::Integer;
  shared->elementTypeName = "Int";
  shared->elements = {fo4::papyrus::integerValue(1), fo4::papyrus::integerValue(2)};
  a.set(quest, "Items", fo4::papyrus::arrayValue(shared));
  a.set(quest, "Other", fo4::papyrus::arrayValue(shared));
  auto snap = std::make_shared<Struct>();
  snap->typeName = "Pair";
  snap->members["a"] = fo4::papyrus::integerValue(4);
  snap->members["b"] = fo4::papyrus::stringValue("x");
  a.set(quest, "Snap", fo4::papyrus::structValue(snap));
  auto loop = std::make_shared<Array>();
  loop->elements = {fo4::papyrus::noneValue()};
  loop->elements[0] = fo4::papyrus::arrayValue(loop);
  a.set(quest, "Loop", fo4::papyrus::arrayValue(loop));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  ASSERT_TRUE(restore(*b.runtime, blob).ok());
  const Value items = *b.runtime->instanceVariable(target, "Items");
  const Value other = *b.runtime->instanceVariable(target, "Other");
  ASSERT_EQ(items.type, ValueType::Array);
  EXPECT_EQ(items.array, other.array);
  items.array->elements.push_back(fo4::papyrus::integerValue(3));
  EXPECT_EQ(other.array->elements.size(), 3u);
  const Value restoredSnap = *b.runtime->instanceVariable(target, "Snap");
  EXPECT_EQ(restoredSnap.structure->typeName, "Pair");
  EXPECT_EQ(restoredSnap.structure->members.at("a").integer, 4);
  EXPECT_EQ(restoredSnap.structure->members.at("b").str, "x");
  const Value restoredLoop = *b.runtime->instanceVariable(target, "Loop");
  ASSERT_EQ(restoredLoop.array->elements.size(), 1u);
  EXPECT_EQ(restoredLoop.array->elements[0].array, restoredLoop.array);
  restoredLoop.array->elements.clear();
  loop->elements.clear();
}

TEST(PapyrusVmState, EngineHandlesAreListedAndNeedAMappingOnRestore) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  const std::uint64_t engine = fo4::papyrus::kEngineHandleBit | 0x1234567890ABCDULL;
  a.set(quest, "Anything", fo4::papyrus::objectValue(engine, "Actor", ""));
  InstanceStateCapture capture;
  ASSERT_TRUE(fo4::papyrus::captureInstanceState(*a.runtime, capture).ok());
  ASSERT_EQ(capture.engineHandles.size(), 1u);
  EXPECT_EQ(capture.engineHandles[0], engine);

  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  b.set(target, "Anything", fo4::papyrus::integerValue(9));
  const InstanceStateResult unmapped = restore(*b.runtime, capture.blob);
  EXPECT_EQ(unmapped.refusal, InstanceStateRefusal::NotFound);
  EXPECT_EQ(b.runtime->instanceVariable(target, "Anything")->integer, 9);

  InstanceStateReport report;
  ASSERT_TRUE(restore(*b.runtime, capture.blob, false, &report, {{engine, fo4::papyrus::kEngineHandleBit | 7}}).ok());
  EXPECT_EQ(b.runtime->instanceVariable(target, "Anything")->object->handle, fo4::papyrus::kEngineHandleBit | 7);
  EXPECT_EQ(b.runtime->instanceVariable(target, "Anything")->object->type, "Actor");
  EXPECT_EQ(report.unboundReferences, 0);

  ASSERT_TRUE(restore(*b.runtime, capture.blob, true, &report).ok());
  EXPECT_TRUE(fo4::papyrus::isNoneLike(*b.runtime->instanceVariable(target, "Anything")));
  EXPECT_EQ(report.unboundReferences, 1);
  EXPECT_EQ(restore(*b.runtime, capture.blob, false, nullptr, {{engine, 7}}).refusal, InstanceStateRefusal::NotFound);
}

TEST(PapyrusVmState, ARefusedRestoreLeavesEveryInstanceUntouched) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  const InstanceId helperA = a.attach("Helper", kHelper);
  a.set(questA, "Label", fo4::papyrus::stringValue("saved"));
  a.set(helperA, "Tag", fo4::papyrus::arrayValue(std::make_shared<Array>()));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  const InstanceId helperB = b.attach("Helper", kHelper);
  b.set(questB, "Label", fo4::papyrus::stringValue("live"));
  b.set(helperB, "Tag", fo4::papyrus::stringValue("live"));
  const std::string before = blobOf(*b.runtime);
  EXPECT_EQ(restore(*b.runtime, blob).refusal, InstanceStateRefusal::TypeMismatch);
  EXPECT_EQ(restore(*b.runtime, blob, true).refusal, InstanceStateRefusal::TypeMismatch);
  EXPECT_EQ(blobOf(*b.runtime), before);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "live");
}

TEST(PapyrusVmState, UnknownInstancesScriptsStatesAndVariablesRefuseUnlessSkipped) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  a.attach("Helper", kHelper);
  a.set(quest, "Label", fo4::papyrus::stringValue("saved"));
  const std::string blob = blobOf(*a.runtime);

  World missing;
  const InstanceId only = missing.attach("Quest", kQuest);
  missing.set(only, "Label", fo4::papyrus::stringValue("live"));
  const std::string before = blobOf(*missing.runtime);
  EXPECT_EQ(restore(*missing.runtime, blob).refusal, InstanceStateRefusal::NotFound);
  EXPECT_EQ(blobOf(*missing.runtime), before);
  InstanceStateReport report;
  ASSERT_TRUE(restore(*missing.runtime, blob, true, &report).ok());
  EXPECT_EQ(report.instances, 1);
  EXPECT_EQ(report.skippedInstances, 1);
  EXPECT_EQ(missing.runtime->instanceVariable(only, "Label")->str, "saved");

  const std::string badState = replaceOnce(blob, "\"script\":\"Helper\",\"state\":\"\"", "\"script\":\"Helper\",\"state\":\"Nope\"");
  World same;
  same.attach("Quest", kQuest);
  same.attach("Helper", kHelper);
  EXPECT_EQ(restore(*same.runtime, badState).refusal, InstanceStateRefusal::NotFound);
  ASSERT_TRUE(restore(*same.runtime, badState, true, &report).ok());
  EXPECT_EQ(report.skippedInstances, 1);

  const std::string goodState = replaceOnce(blob, "\"script\":\"Quest\",\"state\":\"\"", "\"script\":\"Quest\",\"state\":\"Busy\"");
  ASSERT_TRUE(restore(*same.runtime, goodState).ok());
  EXPECT_EQ(same.runtime->instanceState(1), "Busy");

  const std::string badVariable = replaceOnce(blob, "[\"label\",", "[\"nothere\",");
  EXPECT_EQ(restore(*same.runtime, badVariable).refusal, InstanceStateRefusal::NotFound);
  ASSERT_TRUE(restore(*same.runtime, badVariable, true, &report).ok());
  EXPECT_EQ(report.skippedVariables, 1);
}

TEST(PapyrusVmState, AReferenceToAnInstanceMissingFromTheRuntimeBecomesNoneWhenSkippedAndAStaleOneSavesUnbound) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  const InstanceId helperA = a.attach("Helper", kHelper);
  a.set(questA, "Link", fo4::papyrus::objectValue(helperA, "Helper", ""));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  b.set(questB, "Link", fo4::papyrus::objectValue(0, "Helper", ""));
  EXPECT_EQ(restore(*b.runtime, blob).refusal, InstanceStateRefusal::NotFound);
  InstanceStateReport report;
  ASSERT_TRUE(restore(*b.runtime, blob, true, &report).ok());
  EXPECT_EQ(report.unboundReferences, 1);
  EXPECT_EQ(report.skippedInstances, 1);
  EXPECT_TRUE(fo4::papyrus::isNoneLike(*b.runtime->instanceVariable(questB, "Link")));

  ASSERT_TRUE(a.runtime->detach(helperA));
  const std::string stale = blobOf(*a.runtime);
  World c;
  const InstanceId questC = c.attach("Quest", kQuest);
  ASSERT_TRUE(restore(*c.runtime, stale).ok());
  EXPECT_EQ(c.runtime->instanceVariable(questC, "Link")->object->handle, 0u);
}

TEST(PapyrusVmState, MalformedTruncatedAndWrongVersionBlobsAreRefusedWithoutTouchingTheRuntime) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  a.set(quest, "Label", fo4::papyrus::stringValue("saved"));
  auto items = std::make_shared<Array>();
  items->elements = {fo4::papyrus::integerValue(1)};
  a.set(quest, "Items", fo4::papyrus::arrayValue(items));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  b.set(target, "Label", fo4::papyrus::stringValue("live"));
  const std::string before = blobOf(*b.runtime);
  for (const std::size_t cut : {std::size_t{0}, std::size_t{1}, blob.size() / 4, blob.size() / 2, blob.size() * 3 / 4, blob.size() - 1}) {
    EXPECT_EQ(restore(*b.runtime, blob.substr(0, cut)).refusal, InstanceStateRefusal::Malformed) << cut;
  }
  const std::vector<std::string> bad = {
      replaceOnce(blob, "\"version\":1", "\"version\":2"),
      replaceOnce(blob, "\"version\":1", "\"version\":\"1\""),
      replaceOnce(blob, "radicle.papyrus.instance-state", "radicle.papyrus.other"),
      blob + "x",
      replaceOnce(blob, "\"engine\":[]", "\"engine\":[],\"engine\":[]"),
      replaceOnce(blob, "\"engine\":[]", "\"engine\":[],\"extra\":1"),
      replaceOnce(blob, "\"r\":0", "\"r\":9"),
      replaceOnce(blob, "\"t\":\"a\"", "\"t\":\"zz\""),
      replaceOnce(blob, "\"v\":\"saved\"", "\"v\":\"sa\xc3\xa9\""),
      replaceOnce(blob, "\"v\":\"saved\"", "\"v\":\"\\u0100\""),
      "[]",
      "{}",
      std::string("{\"format\":\"radicle.papyrus.instance-state\",\"version\":1.5}"),
  };
  for (const std::string& text : bad) {
    const InstanceStateResult result = restore(*b.runtime, text);
    EXPECT_EQ(result.refusal, InstanceStateRefusal::Malformed) << text << " -> " << result.reason;
  }
  EXPECT_EQ(blobOf(*b.runtime), before);
  EXPECT_EQ(b.runtime->instanceVariable(target, "Label")->str, "live");
}

TEST(PapyrusVmState, CaptureSkipsDetachedAndUndeclaredVariablesAndRefusesAmbiguousIdentities) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  const InstanceId helper = a.attach("Helper", kHelper);
  a.set(quest, "Label", fo4::papyrus::stringValue("kept"));
  a.set(quest, "NotDeclaredAnywhere", fo4::papyrus::integerValue(1));
  ASSERT_TRUE(a.runtime->detach(helper));
  const std::string blob = blobOf(*a.runtime);
  EXPECT_EQ(blob.find("Helper"), std::string::npos);
  EXPECT_EQ(blob.find("notdeclaredanywhere"), std::string::npos);
  EXPECT_NE(blob.find("\"label\""), std::string::npos);

  a.attach("Quest", kQuest);
  InstanceStateCapture capture;
  const InstanceStateResult result = fo4::papyrus::captureInstanceState(*a.runtime, capture);
  EXPECT_EQ(result.refusal, InstanceStateRefusal::Ambiguous);
  EXPECT_TRUE(capture.blob.empty());
}

TEST(PapyrusVmState, TypeChecksCoverArrayElementsAndStructTypeNames) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  auto items = std::make_shared<Array>();
  auto element = std::make_shared<Struct>();
  element->typeName = "Pair";
  items->elements = {fo4::papyrus::structValue(element)};
  a.set(quest, "Items", fo4::papyrus::arrayValue(items));
  World b;
  b.attach("Quest", kQuest);
  EXPECT_EQ(restore(*b.runtime, blobOf(*a.runtime), true).refusal, InstanceStateRefusal::TypeMismatch);

  auto wrong = std::make_shared<Struct>();
  wrong->typeName = "Different";
  a.set(quest, "Items", fo4::papyrus::arrayValue(std::make_shared<Array>()));
  a.set(quest, "Snap", fo4::papyrus::structValue(wrong));
  EXPECT_EQ(restore(*b.runtime, blobOf(*a.runtime)).refusal, InstanceStateRefusal::TypeMismatch);
}

TEST(PapyrusVmState, ALiveVariableAbsentFromTheBlobIsClearedByRestore) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  a.set(questA, "Label", fo4::papyrus::stringValue("saved"));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  b.set(questB, "Label", fo4::papyrus::stringValue("live"));
  b.set(questB, "Ratio", fo4::papyrus::floatValue(2.5F));
  b.set(questB, "Inherited", fo4::papyrus::integerValue(9));
  InstanceStateReport report;
  ASSERT_TRUE(restore(*b.runtime, blob, false, &report).ok());
  EXPECT_EQ(report.variables, 1);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "saved");
  EXPECT_FALSE(b.runtime->instanceVariable(questB, "Ratio").has_value());
  EXPECT_FALSE(b.runtime->instanceVariable(questB, "Inherited").has_value());
  EXPECT_EQ(blobOf(*b.runtime), blob);
}

TEST(PapyrusVmState, AnArrayTakesTheDeclaredElementTypeNotTheOneTheBlobCarries) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  auto items = std::make_shared<Array>();
  items->elementType = ValueType::Float;
  items->elementTypeName = "Float";
  items->elements = {fo4::papyrus::floatValue(2.0F), fo4::papyrus::floatValue(3.0F)};
  a.set(quest, "Items", fo4::papyrus::arrayValue(items));
  const std::string blob = blobOf(*a.runtime);
  EXPECT_NE(blob.find("\"etn\":\"Float\""), std::string::npos);

  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  ASSERT_TRUE(restore(*b.runtime, blob).ok());
  const Value restored = *b.runtime->instanceVariable(target, "Items");
  ASSERT_EQ(restored.type, ValueType::Array);
  EXPECT_EQ(restored.array->elementType, ValueType::Integer);
  EXPECT_EQ(restored.array->elementTypeName, "Int");
  ASSERT_EQ(restored.array->elements.size(), 2u);
  EXPECT_EQ(restored.array->elements[0].type, ValueType::Integer);
  EXPECT_EQ(restored.array->elements[1].integer, 3);
}

TEST(PapyrusVmState, AnArraySharedByVariablesOfDifferentElementTypesIsRefused) {
  World a;
  const InstanceId quest = a.attach("Quest", kQuest);
  auto shared = std::make_shared<Array>();
  shared->elements = {fo4::papyrus::integerValue(1)};
  a.set(quest, "Items", fo4::papyrus::arrayValue(shared));
  a.set(quest, "Reals", fo4::papyrus::arrayValue(shared));
  World b;
  const InstanceId target = b.attach("Quest", kQuest);
  b.set(target, "Label", fo4::papyrus::stringValue("live"));
  EXPECT_EQ(restore(*b.runtime, blobOf(*a.runtime)).refusal, InstanceStateRefusal::TypeMismatch);
  EXPECT_EQ(b.runtime->instanceVariable(target, "Label")->str, "live");
  EXPECT_FALSE(b.runtime->instanceVariable(target, "Items").has_value());
}

TEST(PapyrusVmState, StagingChangesNothingUntilTheCommit) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  a.set(questA, "Label", fo4::papyrus::stringValue("saved"));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  b.set(questB, "Label", fo4::papyrus::stringValue("live"));
  const std::string before = blobOf(*b.runtime);
  fo4::papyrus::InstanceStatePlan plan;
  InstanceStateReport report;
  ASSERT_TRUE(fo4::papyrus::stageInstanceState(*b.runtime, blob, InstanceStateOptions{}, plan, report).ok());
  EXPECT_EQ(report.instances, 1);
  EXPECT_EQ(blobOf(*b.runtime), before);
  fo4::papyrus::commitInstanceState(*b.runtime, plan);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "saved");
}

// Invariant: restoring one identity must not write the blob over instances restored earlier; detects the late-attach loss.
TEST(PapyrusVmState, ARestoreOfOneIdentityLeavesTheOtherInstancesAlone) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  const InstanceId helperA = a.attach("Helper", kHelper);
  a.set(questA, "Label", fo4::papyrus::stringValue("saved"));
  a.set(helperA, "Tag", fo4::papyrus::stringValue("saved tag"));
  a.set(helperA, "Back", fo4::papyrus::objectValue(questA, "Quest", ""));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  const InstanceId helperB = b.attach("Helper", kHelper);
  b.set(questB, "Label", fo4::papyrus::stringValue("ran since the load"));
  b.set(helperB, "Tag", fo4::papyrus::stringValue("live tag"));
  InstanceStateReport report;
  ASSERT_TRUE(restoreOnly(*b.runtime, blob, {InstanceIdentity{kHelper, "Helper"}}, false, &report).ok());
  EXPECT_EQ(report.instances, 1);
  EXPECT_EQ(report.skippedInstances, 0);
  EXPECT_EQ(b.runtime->instanceVariable(helperB, "Tag")->str, "saved tag");
  EXPECT_EQ(b.runtime->instanceVariable(helperB, "Back")->object->handle, questB);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "ran since the load");

  ASSERT_TRUE(restoreOnly(*b.runtime, blob, {InstanceIdentity{kAlias, "Quest"}}, false, &report).ok());
  EXPECT_EQ(report.instances, 0);
  EXPECT_EQ(report.skippedInstances, 0);
  ASSERT_TRUE(restoreOnly(*b.runtime, blob, {InstanceIdentity{kQuest, "quest"}}, false, &report).ok());
  EXPECT_EQ(report.instances, 1);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "saved");
}

TEST(PapyrusVmState, AChosenInstanceNotAttachedIsSkippedAndAnUnchosenOneIsNeitherCountedNorRefused) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  const InstanceId helperA = a.attach("Helper", kHelper);
  a.set(questA, "Label", fo4::papyrus::stringValue("saved"));
  a.set(questA, "Link", fo4::papyrus::objectValue(helperA, "Helper", ""));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  b.set(questB, "Label", fo4::papyrus::stringValue("live"));
  InstanceStateReport report;
  EXPECT_EQ(restoreOnly(*b.runtime, blob, {InstanceIdentity{kHelper, "Helper"}}).refusal, InstanceStateRefusal::NotFound);
  ASSERT_TRUE(restoreOnly(*b.runtime, blob, {InstanceIdentity{kHelper, "Helper"}}, true, &report).ok());
  EXPECT_EQ(report.instances, 0);
  EXPECT_EQ(report.skippedInstances, 1);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "live");

  ASSERT_TRUE(restoreOnly(*b.runtime, blob, {InstanceIdentity{kQuest, "Quest"}}, true, &report).ok());
  EXPECT_EQ(report.instances, 1);
  EXPECT_EQ(report.skippedInstances, 0);
  EXPECT_EQ(report.unboundReferences, 1);
  EXPECT_EQ(b.runtime->instanceVariable(questB, "Label")->str, "saved");
}

TEST(PapyrusVmState, ARestoreOfNoIdentitiesChangesNothing) {
  World a;
  const InstanceId questA = a.attach("Quest", kQuest);
  a.set(questA, "Label", fo4::papyrus::stringValue("saved"));
  const std::string blob = blobOf(*a.runtime);

  World b;
  const InstanceId questB = b.attach("Quest", kQuest);
  b.set(questB, "Label", fo4::papyrus::stringValue("live"));
  const std::string before = blobOf(*b.runtime);
  InstanceStateReport report;
  ASSERT_TRUE(restoreOnly(*b.runtime, blob, {}, false, &report).ok());
  EXPECT_EQ(report.instances, 0);
  EXPECT_EQ(blobOf(*b.runtime), before);
  EXPECT_EQ(restoreOnly(*b.runtime, blob.substr(0, blob.size() - 1), {}).refusal, InstanceStateRefusal::Malformed);
}

}  // namespace
