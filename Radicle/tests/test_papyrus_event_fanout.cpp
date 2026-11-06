#include "fo4/papyrus/runtime.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::FanOutResult;
using fo4::papyrus::InstanceId;
using fo4::papyrus::OwnerId;
using fo4::papyrus::Runtime;
using fo4::papyrus::Status;
using fo4::papyrus::integerValue;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexTypedName;
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

PexInstruction ins(std::string mnemonic, std::vector<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcodeOf(mnemonic);
  instruction.mnemonic = std::move(mnemonic);
  instruction.fixedArgCount = static_cast<int>(args.size());
  instruction.hasVarArgs = false;
  instruction.args = std::move(args);
  return instruction;
}

PexFunction method(std::string name, std::vector<PexTypedName> params, std::vector<PexInstruction> body) {
  PexFunction function;
  function.name = std::move(name);
  function.returnType = "None";
  function.params = std::move(params);
  function.instructions = std::move(body);
  return function;
}

PexObject script(std::string name, std::vector<PexFunction> functions) {
  PexState entry;
  entry.name = "";
  entry.functions = std::move(functions);
  PexObject object;
  object.name = std::move(name);
  object.autoStateName = "";
  object.states.push_back(std::move(entry));
  return object;
}

class FanOutTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::vector<PexInstruction> done{ins("return", {id("::NoneVar")})};
    scripts_.push_back(script("Listener", {method("OnInit", {}, done),
                                           method("::remote_Actor_OnDeath", {PexTypedName{"amount", "Int"}},
                                                  {ins("assign", {id("seen"), id("amount")}),
                                                   ins("return", {id("::NoneVar")})})}));
    scripts_.push_back(script("Deaf", {method("OnInit", {}, done)}));
    std::vector<const PexObject*> pointers;
    for (const PexObject& object : scripts_) pointers.push_back(&object);
    runtime_ = std::make_unique<Runtime>(std::move(pointers));
  }

  InstanceId attach(const char* scriptName, const std::string& form, bool initialize = true) {
    const auto instance = runtime_->attach(scriptName, OwnerId::forForm(form));
    EXPECT_TRUE(instance.has_value());
    if (initialize) {
      EXPECT_EQ(runtime_->initialize(*instance), Status::Ok);
    }
    return *instance;
  }

  std::vector<PexObject> scripts_;
  std::unique_ptr<Runtime> runtime_;
};

// Detects: a dotted remote event that must reach ::remote_Type_Event with its arguments once the scheduler runs.
TEST_F(FanOutTest, ADottedEventIsQueuedForTheRemoteHandlerWithItsArguments) {
  const InstanceId listener = attach("Listener", "A");

  const auto report = runtime_->fanOutEvent(OwnerId::forForm("A"), "Actor.OnDeath", {integerValue(7)});

  EXPECT_EQ(report.handler, "::remote_Actor_OnDeath");
  EXPECT_EQ(report.delivered, 1);
  ASSERT_EQ(report.outcomes.size(), 1u);
  EXPECT_EQ(report.outcomes[0].instance, listener);
  EXPECT_EQ(report.outcomes[0].script, "Listener");
  EXPECT_EQ(report.outcomes[0].result, FanOutResult::Delivered);
  EXPECT_EQ(runtime_->runStep(64).completed, 1);
  const auto seen = runtime_->instanceVariable(listener, "seen");
  ASSERT_TRUE(seen.has_value());
  EXPECT_EQ(seen->integer, 7);
}

// Detects: broadcast order that must follow the owner key so two runs over the same state give one digest.
TEST_F(FanOutTest, BroadcastOrdersOwnersByKeyAndGivesAStableDigest) {
  attach("Listener", "B");
  attach("Deaf", "C");
  attach("Listener", "A");

  const auto first = runtime_->broadcastEvent("Actor.OnDeath", {integerValue(1)});
  runtime_->runStep(64);
  const auto second = runtime_->broadcastEvent("Actor.OnDeath", {integerValue(1)});

  ASSERT_EQ(first.outcomes.size(), 3u);
  EXPECT_EQ(first.outcomes[0].owner, OwnerId::forForm("A"));
  EXPECT_EQ(first.outcomes[1].owner, OwnerId::forForm("B"));
  EXPECT_EQ(first.outcomes[2].owner, OwnerId::forForm("C"));
  EXPECT_EQ(first.delivered, 2);
  EXPECT_EQ(first.noHandler, 1);
  EXPECT_EQ(first.outcomes[2].result, FanOutResult::NoHandler);
  EXPECT_EQ(first.digest(), second.digest());
  EXPECT_EQ(first.digest().size(), 64u);
}

// Detects: parked or detached owners that must not receive events, and must receive them again once unparked.
TEST_F(FanOutTest, ParkedAndDetachedOwnersAreSkippedUntilUnparked) {
  const InstanceId listener = attach("Listener", "A");
  const OwnerId owner = OwnerId::forForm("A");

  runtime_->parkOwner(owner);
  const auto parked = runtime_->fanOutEvent(owner, "Actor.OnDeath", {integerValue(3)});
  EXPECT_EQ(parked.skippedParked, 1);
  EXPECT_EQ(runtime_->runStep(64).completed, 0);

  runtime_->unparkOwner(owner);
  EXPECT_EQ(runtime_->fanOutEvent(owner, "Actor.OnDeath", {integerValue(3)}).delivered, 1);

  runtime_->detach(listener);
  EXPECT_EQ(runtime_->fanOutEvent(owner, "Actor.OnDeath", {integerValue(3)}).skippedParked, 1);
}

// Detects: an unproven event that must be refused for every owner, including owners with no instance.
TEST_F(FanOutTest, AnEventTheGateRefusesIsRefusedForEveryOwner) {
  attach("Listener", "A");
  runtime_->setGate([](std::string_view key) { return key != "event.Actor.OnDeath"; });

  const auto known = runtime_->fanOutEvent(OwnerId::forForm("A"), "Actor.OnDeath", {integerValue(1)});
  const auto unknown = runtime_->fanOutEvent(OwnerId::forForm("Z"), "Actor.OnDeath", {integerValue(1)});

  EXPECT_EQ(known.refused, 1);
  EXPECT_EQ(unknown.refused, 1);
  EXPECT_EQ(runtime_->runStep(64).completed, 0);
}

// Detects: unknown owners and missing handlers that must stay distinct outcomes, not silent drops.
TEST_F(FanOutTest, UnknownOwnersAndMissingHandlersAreReportedSeparately) {
  attach("Deaf", "A");

  runtime_->parkOwner(OwnerId::forForm("P"));
  EXPECT_EQ(runtime_->fanOutEvent(OwnerId::forForm("P"), "OnHit", {}).skippedParked, 1);
  const auto unknown = runtime_->fanOutEvent(OwnerId::forForm("Z"), "OnHit", {});
  const auto noHandler = runtime_->fanOutEvent(OwnerId::forForm("A"), "OnHit", {});

  EXPECT_EQ(unknown.unknownOwner, 1);
  EXPECT_EQ(unknown.outcomes[0].instance, 0u);
  EXPECT_EQ(noHandler.noHandler, 1);
  EXPECT_EQ(noHandler.outcomes[0].script, "Deaf");
  EXPECT_EQ(fo4::papyrus::eventHandlerName("OnHit"), "OnHit");
  EXPECT_EQ(fo4::papyrus::eventHandlerName("Actor.OnDeath"), "::remote_Actor_OnDeath");
}

// Detects: an uninitialised instance that must be reported as refused with its status, not as delivered.
TEST_F(FanOutTest, AnInstanceThatIsNotReadyRefusesTheEventWithItsStatus) {
  attach("Listener", "A", false);

  const auto report = runtime_->fanOutEvent(OwnerId::forForm("A"), "Actor.OnDeath", {integerValue(1)});

  EXPECT_EQ(report.refused, 1);
  EXPECT_EQ(report.delivered, 0);
  EXPECT_NE(report.outcomes[0].status, Status::Ok);
  EXPECT_FALSE(report.outcomes[0].reason.empty());
}

}  // namespace
