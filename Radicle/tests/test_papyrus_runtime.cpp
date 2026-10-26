#include "fo4/papyrus/runtime.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::ClockSource;
using fo4::papyrus::InstanceId;
using fo4::papyrus::Lifecycle;
using fo4::papyrus::NativeRegistration;
using fo4::papyrus::NativeTargetKind;
using fo4::papyrus::OnInitArrivalPolicy;
using fo4::papyrus::OwnerId;
using fo4::papyrus::OwnerRegistry;
using fo4::papyrus::OverrideResult;
using fo4::papyrus::RegistrationRegistry;
using fo4::papyrus::Runtime;
using fo4::papyrus::SchedulerStepResult;
using fo4::papyrus::ScriptRegistration;
using fo4::papyrus::Status;
using fo4::papyrus::Value;
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

PexValue literal(std::string text) {
  PexValue value;
  value.type = PexValueType::String;
  value.str = std::move(text);
  return value;
}

PexValue number(std::int32_t amount) {
  PexValue value;
  value.type = PexValueType::Integer;
  value.int_ = amount;
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

PexFunction method(std::string name, std::string returnType, std::vector<PexTypedName> params,
                   std::vector<PexInstruction> body) {
  PexFunction function;
  function.name = std::move(name);
  function.returnType = std::move(returnType);
  function.params = std::move(params);
  function.instructions = std::move(body);
  return function;
}

PexState state(std::string name, std::vector<PexFunction> functions) {
  PexState entry;
  entry.name = std::move(name);
  entry.functions = std::move(functions);
  return entry;
}

PexObject script(std::string name, std::vector<PexState> states) {
  PexObject object;
  object.name = std::move(name);
  object.autoStateName = "";
  object.states = std::move(states);
  return object;
}

class RuntimeTest : public ::testing::Test {
 protected:
  void addScript(PexObject object) { scripts_.push_back(std::move(object)); }

  void buildRuntime() {
    std::vector<const PexObject*> pointers;
    for (const PexObject& object : scripts_) pointers.push_back(&object);
    runtime_ = std::make_unique<Runtime>(std::move(pointers));
  }

  Runtime& runtime() { return *runtime_; }

  std::vector<PexObject> scripts_;
  std::unique_ptr<Runtime> runtime_;
};

TEST_F(RuntimeTest, AttachInitializesOnceAndRunsAnEvent) {
  int hits = 0;
  addScript(script("Actor", {state("", {method("OnInit", "None", {}, {ins("return", {id("::NoneVar")})}),
                                      method("Hit", "None", {}, {ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  runtime().registerNative("Actor", "Hit",
                           NativeRegistration{NativeTargetKind::Static, "",
                                              [&hits](const Value&, const std::vector<Value>&, Value& out,
                                                      Status&) {
                                                hits += 1;
                                                out = fo4::papyrus::noneValue();
                                                return true;
                                              },
                                              false,
                                              {}});
  const OwnerId owner = OwnerId::forForm("000800:Actor.esp");
  const auto instance = runtime().attach("Actor", owner);
  ASSERT_TRUE(instance.has_value());
  EXPECT_EQ(runtime().lifecycle(*instance), Lifecycle::NotInitialized);
  EXPECT_EQ(runtime().initialize(*instance), Status::Ok);
  EXPECT_EQ(runtime().lifecycle(*instance), Lifecycle::Ready);
  EXPECT_EQ(runtime().initialize(*instance), Status::Ok);
  EXPECT_EQ(runtime().sendEvent(*instance, "Hit"), Status::Ok);
  const SchedulerStepResult step = runtime().runStep(512);
  EXPECT_EQ(step.completed, 1);
  EXPECT_EQ(hits, 1);
}

TEST_F(RuntimeTest, AFailedOnInitLeavesTheInstanceClosed) {
  addScript(script("Actor", {state("", {method("OnInit", "None", {},
                                             {ins("callstatic",
                                                  {literal("Missing"), literal("Boom"), id("::nonevar")}),
                                              ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  const auto instance = runtime().attach("Actor", OwnerId::forForm("000801:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  EXPECT_EQ(runtime().initialize(*instance), Status::FunctionNotFound);
  EXPECT_EQ(runtime().lifecycle(*instance), Lifecycle::Failed);
  EXPECT_EQ(runtime().sendEvent(*instance, "OnInit"), Status::HostError);
  EXPECT_GT(runtime().refusedEvents(), 0);
}

TEST_F(RuntimeTest, AnEventDuringOnInitIsRefusedOrQueuedByPolicy) {
  addScript(script("Actor", {state("", {method("OnInit", "None", {},
                                             {ins("callstatic",
                                                  {literal("Hook"), literal("Emit"), id("::nonevar")}),
                                              ins("return", {id("::NoneVar")})}),
                                      method("Ping", "None", {},
                                             {ins("callstatic",
                                                  {literal("Hook"), literal("Hit"), id("::nonevar")}),
                                              ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  int hits = 0;
  runtime().registerNative("Hook", "Hit",
                           NativeRegistration{NativeTargetKind::Static, "",
                                              [&hits](const Value&, const std::vector<Value>&, Value& out, Status&) {
                                                hits += 1;
                                                out = fo4::papyrus::noneValue();
                                                return true;
                                              },
                                              false,
                                              {}});
  const auto instance = runtime().attach("Actor", OwnerId::forForm("000802:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  InstanceId id = *instance;
  Status observed = Status::Ok;
  runtime().registerNative(
      "Hook", "Emit",
      NativeRegistration{NativeTargetKind::Static, "",
                         [&runtime = runtime(), id, &observed](const Value&, const std::vector<Value>&,
                                                               Value& out, Status&) {
                           observed = runtime.sendEvent(id, "Ping");
                           out = fo4::papyrus::noneValue();
                           return true;
                         },
                         false,
                         {}});
  EXPECT_EQ(runtime().initialize(id), Status::Ok);
  EXPECT_EQ(observed, Status::GateRefused);
  EXPECT_GT(runtime().refusedEvents(), 0);

  runtime().setOnInitArrivalPolicy(OnInitArrivalPolicy::QueueAsApproximation);
  const auto second = runtime().attach("Actor", OwnerId::forForm("000803:Actor.esp"));
  ASSERT_TRUE(second.has_value());
  id = *second;
  EXPECT_EQ(runtime().initialize(id), Status::Ok);
  EXPECT_GT(runtime().queuedDuringOnInit(), 0);
  runtime().runStep(512);
  EXPECT_EQ(hits, 1);
}

TEST_F(RuntimeTest, ASliceParksAndThenDropsARunawayScript) {
  addScript(script("Loop", {state("", {method("Spin", "None", {}, {ins("jmp", {number(-1)})})})}));
  buildRuntime();
  runtime().setMaxParksPerEvent(2);
  const auto instance = runtime().attach("Loop", OwnerId::forForm("000804:Loop.esp"));
  ASSERT_TRUE(instance.has_value());
  ASSERT_EQ(runtime().initialize(*instance), Status::Ok);
  EXPECT_EQ(runtime().sendEvent(*instance, "Spin"), Status::Ok);
  const SchedulerStepResult first = runtime().runStep(4);
  EXPECT_EQ(first.paused, 1);
  EXPECT_EQ(first.parked, 1);
  const SchedulerStepResult second = runtime().runStep(4);
  EXPECT_EQ(second.paused, 1);
  EXPECT_EQ(second.dropped, 1);
  EXPECT_EQ(runtime().droppedEvents(), 1);
}

TEST_F(RuntimeTest, ALatentNativeSuspendsAndResumesExactlyOnce) {
  addScript(script("Actor", {state("", {method("Run", "None", {},
                                              {ins("callstatic",
                                                   {literal("Waiter"), literal("Wait"), id("result")}),
                                               ins("callstatic",
                                                   {literal("Hook"), literal("Hit"), id("::nonevar"),
                                                    id("result")}),
                                               ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  int received = 0;
  runtime().registerNative(
      "Waiter", "Wait",
      NativeRegistration{NativeTargetKind::Static, "", {},
                         true,
                         [](const std::vector<Value>&, Status&) { return std::uint64_t{7}; }});
  runtime().registerNative(
      "Hook", "Hit",
      NativeRegistration{NativeTargetKind::Static, "",
                         [&received](const Value&, const std::vector<Value>& args, Value& out, Status&) {
                           if (!args.empty() && args[0].type == fo4::papyrus::ValueType::Integer) {
                             received = args[0].integer;
                           }
                           out = fo4::papyrus::noneValue();
                           return true;
                         },
                         false,
                         {}});
  const auto instance = runtime().attach("Actor", OwnerId::forForm("000805:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  ASSERT_EQ(runtime().initialize(*instance), Status::Ok);
  EXPECT_EQ(runtime().sendEvent(*instance, "Run"), Status::Ok);
  runtime().runStep(512);
  EXPECT_EQ(runtime().waitingCount(), 1u);
  EXPECT_EQ(received, 0);

  EXPECT_TRUE(runtime().completeLatent(7, fo4::papyrus::integerValue(42)));
  EXPECT_FALSE(runtime().completeLatent(7, fo4::papyrus::integerValue(1)));
  EXPECT_GT(runtime().refusedCompletions(), 0);
  runtime().runStep(512);
  EXPECT_EQ(runtime().waitingCount(), 0u);
  EXPECT_EQ(received, 42);
}

TEST_F(RuntimeTest, NativeRegistryRefusesAnUnknownNativeAndEnforcesRequiredScript) {
  addScript(script("Actor", {state("", {method("Run", "None", {},
                                             {ins("callstatic",
                                                  {literal("Other"), literal("Hit"), id("::nonevar")}),
                                              ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  int hits = 0;
  runtime().registerNative("Other", "Hit",
                           NativeRegistration{NativeTargetKind::ScriptInstance, "Other",
                                              [&hits](const Value&, const std::vector<Value>&, Value& out,
                                                      Status&) {
                                                hits += 1;
                                                out = fo4::papyrus::noneValue();
                                                return true;
                                              },
                                              false,
                                              {}});
  const auto instance = runtime().attach("Actor", OwnerId::forForm("000806:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  ASSERT_EQ(runtime().initialize(*instance), Status::Ok);
  EXPECT_EQ(runtime().sendEvent(*instance, "Run"), Status::Ok);
  runtime().runStep(512);
  EXPECT_EQ(hits, 0);
  EXPECT_GT(runtime().nativeTargetRefusals(), 0);
}

TEST_F(RuntimeTest, OverridesHandleDeclineAndRefuseBeforeBytecode) {
  addScript(script("Actor", {state("", {method("Ping", "None", {},
                                             {ins("callstatic",
                                                  {literal("Hook"), literal("Hit"), id("::nonevar")}),
                                              ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  int hits = 0;
  runtime().registerNative("Hook", "Hit",
                           NativeRegistration{NativeTargetKind::Static, "",
                                              [&hits](const Value&, const std::vector<Value>&, Value& out,
                                                      Status&) {
                                                hits += 1;
                                                out = fo4::papyrus::noneValue();
                                                return true;
                                              },
                                              false,
                                              {}});
  const auto instance = runtime().attach("Actor", OwnerId::forForm("000807:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  ASSERT_EQ(runtime().initialize(*instance), Status::Ok);
  runtime().registerOverride("Actor", "Ping", -1,
                             [](const Value&, const std::string&, const std::vector<Value>&) {
                               return OverrideResult::handled(fo4::papyrus::noneValue());
                             });
  EXPECT_EQ(runtime().sendEvent(*instance, "Ping"), Status::Ok);
  EXPECT_EQ(hits, 0);
  EXPECT_EQ(runtime().overridesHandled(), 1);

  runtime().registerOverride("Actor", "Ping", -1,
                             [](const Value&, const std::string&, const std::vector<Value>&) {
                               return OverrideResult::declined();
                             });
  EXPECT_EQ(runtime().sendEvent(*instance, "Ping"), Status::Ok);
  runtime().runStep(512);
  EXPECT_EQ(hits, 1);
  EXPECT_EQ(runtime().overridesDeclined(), 1);

  runtime().registerOverride("Actor", "Ping", -1,
                             [](const Value&, const std::string&, const std::vector<Value>&) {
                               return OverrideResult::refused("no");
                             });
  EXPECT_EQ(runtime().sendEvent(*instance, "Ping"), Status::HostError);
  EXPECT_EQ(runtime().overridesRefused(), 1);
}

TEST_F(RuntimeTest, GotoStateChangesTheStateTheNextEventDispatchesIn) {
  addScript(script("Actor", {state("", {method("Go", "None", {},
                                               {ins("callmethod",
                                                    {literal("GotoState"), id("self"), id("::nonevar"),
                                                     literal("Alert")}),
                                                ins("return", {id("::NoneVar")})})}),
                             state("Alert", {method("Ping", "None", {},
                                                    {ins("callstatic",
                                                         {literal("Hook"), literal("Hit"),
                                                          id("::nonevar")}),
                                                     ins("return", {id("::NoneVar")})})})}));
  buildRuntime();
  int hits = 0;
  runtime().registerNative("Hook", "Hit",
                           NativeRegistration{NativeTargetKind::Static, "",
                                              [&hits](const Value&, const std::vector<Value>&, Value& out,
                                                      Status&) {
                                                hits += 1;
                                                out = fo4::papyrus::noneValue();
                                                return true;
                                              },
                                              false,
                                              {}});
  const auto instance = runtime().attach("Actor", OwnerId::forForm("000808:Actor.esp"));
  ASSERT_TRUE(instance.has_value());
  ASSERT_EQ(runtime().initialize(*instance), Status::Ok);
  EXPECT_EQ(runtime().sendEvent(*instance, "Go"), Status::Ok);
  runtime().runStep(512);
  EXPECT_EQ(runtime().instanceState(*instance), "Alert");
  EXPECT_EQ(runtime().sendEvent(*instance, "Ping"), Status::Ok);
  runtime().runStep(512);
  EXPECT_EQ(hits, 1);
}

TEST_F(RuntimeTest, OwnerRegistryKeepsInstancesOfOneFormDistinct) {
  OwnerRegistry registry;
  const OwnerId first = registry.allocateRuntimeInstance("0001:Base.esp");
  const OwnerId second = registry.allocateRuntimeInstance("0001:Base.esp");
  EXPECT_NE(first.key(), second.key());
  EXPECT_EQ(registry.countFor("0001:Base.esp"), 2u);
  EXPECT_EQ(first.kind, fo4::papyrus::OwnerKind::RuntimeInstance);
}

TEST(RuntimeRegistryTest, CustomEventsMatchWithoutCaseAndTimersAdvance) {
  RegistrationRegistry registry;
  const OwnerId owner = OwnerId::forForm("0002:Quest.esp");
  const auto* custom = registry.registerRegistration(owner, ScriptRegistration::forCustomEvent("OnPing"));
  ASSERT_NE(custom, nullptr);
  EXPECT_EQ(registry.matchCustomEvent("onping").size(), 1u);
  EXPECT_EQ(registry.matchCustomEvent("other").size(), 0u);

  const auto* timer = registry.registerRegistration(
      owner, ScriptRegistration::forTimer(2.0, false, ClockSource::RealTime));
  ASSERT_NE(timer, nullptr);
  const std::int64_t handle = timer->handle;
  EXPECT_EQ(registry.advance("GameSession", 1.0, ClockSource::RealTime), 0);
  EXPECT_EQ(registry.advance("GameSession", 1.5, ClockSource::RealTime), 1);
  EXPECT_EQ(registry.count(), 1u);
  EXPECT_FALSE(registry.unregister(handle));

  EXPECT_THROW(registry.advance("SomeoneElse", 1.0, ClockSource::RealTime), std::invalid_argument);
  EXPECT_EQ(registry.refusedForeignAdvance(), 1);

  const auto* gameTimer = registry.registerRegistration(
      owner, ScriptRegistration::forTimer(1.0, true, ClockSource::GameTime));
  ASSERT_NE(gameTimer, nullptr);
  EXPECT_EQ(registry.advance("GameSession", 5.0, ClockSource::GameTime), 0);
  EXPECT_EQ(registry.unboundGameTime(), 1);
}

}  // namespace
