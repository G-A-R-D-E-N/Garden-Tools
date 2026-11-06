#include "fo4/papyrus/runtime.hpp"
#include "fo4/papyrus/save_state.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::AttachRefusal;
using fo4::papyrus::ClockSource;
using fo4::papyrus::OwnerId;
using fo4::papyrus::RegistrationKind;
using fo4::papyrus::RegistrationRegistry;
using fo4::papyrus::Runtime;
using fo4::papyrus::SavedRegistration;
using fo4::papyrus::SaveState;
using fo4::papyrus::ScriptRegistration;
using fo4::papyrus::Value;
using fo4::pex::PexObject;

class AttachRemoteTest : public ::testing::Test {
 protected:
  void SetUp() override {
    object_.name = "Actor";
    object_.autoStateName = "";
    runtime_ = std::make_unique<Runtime>(std::vector<const PexObject*>{&object_});
  }

  PexObject object_;
  std::unique_ptr<Runtime> runtime_;
  const OwnerId owner_ = OwnerId::forForm("000800:Actor.esp");
  const OwnerId source_ = OwnerId::forForm("000801:Quests.esp");
};

TEST_F(AttachRemoteTest, PlainAttachFoldsPropertyNamesSoMixedCaseKeysAreFound) {
  std::map<std::string, Value> properties;
  properties["Count"] = fo4::papyrus::integerValue(3);
  const auto instance = runtime_->attach("Actor", owner_, std::move(properties));
  ASSERT_TRUE(instance.has_value());
  const auto value = runtime_->instanceProperty(*instance, "COUNT");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value->integer, 3);
}

TEST_F(AttachRemoteTest, AttachWithPropertiesStoresEveryValueAndReadsThemBackWithoutCase) {
  const auto result = runtime_->attachWithProperties(
      "actor", owner_, {{"Count", fo4::papyrus::integerValue(5)}, {"Label", fo4::papyrus::stringValue("a")}});
  ASSERT_TRUE(result.instance.has_value());
  EXPECT_EQ(result.refusal, AttachRefusal::None);
  EXPECT_EQ(runtime_->instanceProperty(*result.instance, "count")->integer, 5);
  EXPECT_EQ(runtime_->instanceProperty(*result.instance, "LABEL")->str, "a");
  EXPECT_FALSE(runtime_->instanceProperty(*result.instance, "Missing").has_value());
  EXPECT_EQ(runtime_->instanceForOwner(owner_), result.instance);
}

TEST_F(AttachRemoteTest, ARefusedAttachLeavesNoInstanceAndConsumesNoId) {
  const auto duplicate = runtime_->attachWithProperties(
      "Actor", owner_, {{"Count", fo4::papyrus::integerValue(1)}, {"COUNT", fo4::papyrus::integerValue(2)}});
  EXPECT_FALSE(duplicate.instance.has_value());
  EXPECT_EQ(duplicate.refusal, AttachRefusal::DuplicatePropertyName);

  const auto empty = runtime_->attachWithProperties("Actor", owner_, {{"Count", fo4::papyrus::integerValue(1)}, {"  ", fo4::papyrus::integerValue(2)}});
  EXPECT_FALSE(empty.instance.has_value());
  EXPECT_EQ(empty.refusal, AttachRefusal::EmptyPropertyName);

  const auto unknown = runtime_->attachWithProperties("Nothing", owner_, {});
  EXPECT_FALSE(unknown.instance.has_value());
  EXPECT_EQ(unknown.refusal, AttachRefusal::UnknownScript);

  EXPECT_FALSE(runtime_->instanceForOwner(owner_).has_value());
  const auto good = runtime_->attachWithProperties("Actor", owner_, {});
  ASSERT_TRUE(good.instance.has_value());
  EXPECT_EQ(*good.instance, 1u);
}

TEST_F(AttachRemoteTest, RemoteMatchNeedsTheExactSourceAndEventWithoutCaseAndKeepsHandleOrder) {
  RegistrationRegistry registry;
  const std::int64_t firstHandle =
      registry.registerRegistration(owner_, ScriptRegistration::forRemoteEvent(source_, "Quest.OnStageSet"))->handle;
  const std::int64_t secondHandle =
      registry.registerRegistration(owner_, ScriptRegistration::forRemoteEvent(source_, "quest.onstageset"))->handle;
  const OwnerId other = OwnerId::forDynamicReference("000801:Quests.esp", "token");
  registry.registerRegistration(owner_, ScriptRegistration::forRemoteEvent(other, "Quest.OnStageSet"));
  registry.registerRegistration(owner_, ScriptRegistration::forCustomEvent("Quest.OnStageSet"));

  const auto matches = registry.matchRemoteEvent(source_, "QUEST.ONSTAGESET");
  ASSERT_EQ(matches.size(), 2u);
  EXPECT_EQ(matches[0]->handle, firstHandle);
  EXPECT_EQ(matches[1]->handle, secondHandle);
  EXPECT_EQ(registry.matchRemoteEvent(other, "Quest.OnStageSet").size(), 1u);
  EXPECT_EQ(registry.matchCustomEvent("Quest.OnStageSet").size(), 1u);
  EXPECT_TRUE(registry.matchRemoteEvent(source_, "Quest.Other").empty());
  EXPECT_EQ(registry.advance("GameSession", 100.0, ClockSource::RealTime), 0);
  EXPECT_EQ(registry.count(), 4u);

  EXPECT_TRUE(registry.unregister(firstHandle));
  ASSERT_EQ(registry.matchRemoteEvent(source_, "Quest.OnStageSet").size(), 1u);
  EXPECT_EQ(registry.unregisterAll(owner_), 3u);
  EXPECT_TRUE(registry.matchRemoteEvent(source_, "Quest.OnStageSet").empty());
}

TEST_F(AttachRemoteTest, InvalidRemotePayloadsRefuseBeforeTheRegistryChanges) {
  RegistrationRegistry registry;
  EXPECT_THROW(ScriptRegistration::forRemoteEvent(OwnerId{}, "Quest.OnStageSet"), std::invalid_argument);
  EXPECT_THROW(ScriptRegistration::forRemoteEvent(source_, " "), std::invalid_argument);
  ScriptRegistration noSource;
  noSource.kind = RegistrationKind::RemoteEvent;
  noSource.eventName = "Quest.OnStageSet";
  EXPECT_THROW(registry.registerRegistration(owner_, noSource), std::invalid_argument);
  ScriptRegistration noName = ScriptRegistration::forRemoteEvent(source_, "x");
  noName.eventName = " ";
  EXPECT_THROW(registry.registerRegistration(owner_, noName), std::invalid_argument);
  EXPECT_EQ(registry.count(), 0u);
  EXPECT_EQ(registry.registerRegistration(owner_, ScriptRegistration::forRemoteEvent(source_, "Quest.OnStageSet"))->handle, 1);
}

TEST_F(AttachRemoteTest, DetachDropsTheOwnersRemoteRegistrations) {
  const auto instance = runtime_->attach("Actor", owner_);
  ASSERT_TRUE(instance.has_value());
  runtime_->registrations().registerRegistration(owner_, ScriptRegistration::forRemoteEvent(source_, "Quest.OnStageSet"));
  EXPECT_TRUE(runtime_->detach(*instance));
  EXPECT_TRUE(runtime_->registrations().matchRemoteEvent(source_, "Quest.OnStageSet").empty());
}

TEST_F(AttachRemoteTest, RemoteSourceSurvivesCaptureSerializationAndRestore) {
  SavedRegistration remote;
  remote.ownerKey = owner_.key();
  remote.kind = "RemoteEvent";
  remote.eventName = "Quest.OnStageSet";
  remote.clock = "RealTime";
  remote.sourceKey = source_.key();
  const SaveState state = fo4::papyrus::captureSaveState({}, {remote}, {}, {});
  const std::string text = state.serialize();
  std::string error;
  const auto decoded = SaveState::deserialize(text, error);
  ASSERT_TRUE(decoded.has_value()) << error;
  ASSERT_EQ(decoded->registrations.size(), 1u);
  EXPECT_EQ(decoded->registrations[0].sourceKey, source_.key());
  EXPECT_EQ(decoded->serialize(), text);
  EXPECT_EQ(decoded->digest(), state.digest());
  const auto report = fo4::papyrus::restoreSaveState(*decoded, {});
  ASSERT_EQ(report.registrations.size(), 1u);
  EXPECT_EQ(report.registrations[0].sourceKey, source_.key());
}

TEST_F(AttachRemoteTest, ARemoteRegistrationWithoutASourceIsRefusedEverywhere) {
  SavedRegistration remote;
  remote.ownerKey = owner_.key();
  remote.kind = "remoteevent";
  remote.eventName = "Quest.OnStageSet";
  remote.clock = "RealTime";
  EXPECT_THROW(fo4::papyrus::captureSaveState({}, {remote}, {}, {}), std::invalid_argument);
  SaveState raw;
  raw.registrations.push_back(remote);
  EXPECT_THROW(raw.serialize(), std::invalid_argument);
  EXPECT_THROW(fo4::papyrus::restoreSaveState(raw, {}), std::invalid_argument);

  const std::string json =
      "{\"version\":2,\"threads\":[],\"timers\":[],\"objectives\":[],\"registrations\":[{\"owner\":\"o\","
      "\"kind\":\"RemoteEvent\",\"event\":\"e\",\"interval\":0,\"repeats\":false,\"clock\":\"RealTime\",\"source\":null}]}";
  std::string error;
  EXPECT_FALSE(SaveState::deserialize(json, error).has_value());
  EXPECT_NE(error.find("source owner key"), std::string::npos);
}

}  // namespace
