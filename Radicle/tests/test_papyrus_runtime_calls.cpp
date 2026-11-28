#include "fo4/c_api_papyrus_runtime.h"
#include "fo4/c_api_pex.h"
#include "fo4/papyrus/runtime.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::CallRecord;
using fo4::papyrus::CallState;
using fo4::papyrus::NativeCall;
using fo4::papyrus::NativeRegistration;
using fo4::papyrus::NativeResult;
using fo4::papyrus::NativeTargetKind;
using fo4::papyrus::OwnerId;
using fo4::papyrus::Runtime;
using fo4::papyrus::SchedulerStepResult;
using fo4::papyrus::Status;
using fo4::papyrus::Value;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexState;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex::PexVariable;

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

PexObject script(std::string name, std::vector<PexFunction> functions) {
  PexObject object;
  object.name = std::move(name);
  object.autoStateName = "";
  PexState entry;
  entry.name = "";
  entry.functions = std::move(functions);
  object.states.push_back(std::move(entry));
  return object;
}

PexInstruction hit(PexValue argument) {
  return ins("callstatic", {literal("Hook"), literal("Hit"), id("::nonevar"), std::move(argument)});
}

PexInstruction done() { return ins("return", {id("::NoneVar")}); }

PexObject probeWithAutoCount() {
  PexObject probe = script("Probe", {method("Read", "None", {}, {hit(id("::Count_var")), done()}),
                                     method("Write", "None", {PexTypedName{"target", "Var"}},
                                            {ins("propset", {literal("Count"), id("target"), number(9)}), done()}),
                                     method("Poke", "None", {PexTypedName{"target", "Var"}},
                                            {ins("propset", {literal("Missing"), id("target"), number(1)}), done()})});
  PexVariable backing;
  backing.name = "::Count_var";
  backing.type = "Int";
  probe.variables.push_back(backing);
  PexProperty count;
  count.name = "Count";
  count.type = "Int";
  count.flags = 0x07;
  count.autoVarName = "::Count_var";
  probe.properties.push_back(count);
  return probe;
}

PexObject actor() {
  return script("Actor", {method("Give", "Int", {}, {ins("return", {number(41)})}),
                          method("Ping", "None", {}, {hit(number(1)), done()}),
                          method("Run", "None", {},
                                 {ins("callstatic", {literal("Waiter"), literal("Wait"), id("::nonevar")}), done()}),
                          method("Spin", "None", {}, {ins("jmp", {number(0)})})});
}

PexObject scaled() {
  PexObject object = script("Scaled", {method("Read", "None", {}, {done()})});
  PexVariable backing;
  backing.name = "::Ratio_var";
  backing.type = "Float";
  object.variables.push_back(backing);
  PexProperty ratio;
  ratio.name = "Ratio";
  ratio.type = "Float";
  ratio.flags = 0x07;
  ratio.autoVarName = "::Ratio_var";
  object.properties.push_back(ratio);
  return object;
}

class RuntimeCallsTest : public ::testing::Test {
 protected:
  void build(std::vector<PexObject> objects) {
    scripts_ = std::move(objects);
    std::vector<const PexObject*> pointers;
    for (const PexObject& object : scripts_) pointers.push_back(&object);
    runtime_ = std::make_unique<Runtime>(std::move(pointers));
    runtime_->registerNative("Hook", "Hit",
                             NativeRegistration{NativeTargetKind::Static, "",
                                                [this](const Value&, const std::vector<Value>& args, Value& out,
                                                       Status&) {
                                                  hits_ += 1;
                                                  if (!args.empty()) received_ = args[0].integer;
                                                  out = fo4::papyrus::noneValue();
                                                  return true;
                                                },
                                                false,
                                                {}, {}});
  }

  fo4::papyrus::InstanceId ready(const std::string& name, const std::string& form,
                                 const std::vector<std::pair<std::string, Value>>& properties = {}) {
    const fo4::papyrus::AttachResult attached =
        runtime_->attachWithProperties(name, OwnerId::forForm(form), properties);
    EXPECT_TRUE(attached.instance.has_value());
    if (!attached.instance.has_value()) return 0;
    EXPECT_EQ(runtime_->initialize(*attached.instance), Status::Ok);
    return *attached.instance;
  }

  std::uint64_t call(fo4::papyrus::InstanceId instance, const std::string& function, std::vector<Value> args = {}) {
    Status status = Status::HostError;
    const std::uint64_t ticket = runtime_->submitCall(instance, function, std::move(args), status);
    EXPECT_EQ(status, Status::Ok);
    EXPECT_NE(ticket, 0u);
    return ticket;
  }

  CallRecord record(std::uint64_t ticket) {
    CallRecord out;
    EXPECT_TRUE(runtime_->takeResult(ticket, false, out));
    return out;
  }

  std::vector<PexObject> scripts_;
  std::unique_ptr<Runtime> runtime_;
  int hits_{0};
  std::int32_t received_{0};
};

// Detects: an own-script read of an attached Auto property returning the default, and host reads going stale after a script write.
TEST_F(RuntimeCallsTest, AttachedAutoPropertyIsWhatTheOwningScriptReadsAndCrossScriptWritesStayConsistent) {
  build({probeWithAutoCount()});
  const auto reader = ready("Probe", "000900:Probe.esp", {{"Count", fo4::papyrus::integerValue(5)}});
  const auto target = ready("Probe", "000901:Probe.esp", {{"count", fo4::papyrus::integerValue(3)}});

  call(reader, "Read");
  runtime_->runStep(512);
  EXPECT_EQ(received_, 5);

  call(reader, "Write", {fo4::papyrus::objectValue(target, "Probe", "")});
  runtime_->runStep(512);
  const std::optional<Value> written = runtime_->instanceProperty(target, "Count");
  ASSERT_TRUE(written.has_value());
  EXPECT_EQ(written->integer, 9);
  call(target, "Read");
  runtime_->runStep(512);
  EXPECT_EQ(received_, 9);

  EXPECT_TRUE(runtime_->setInstanceVariable(reader, "::Count_var", fo4::papyrus::integerValue(11)));
  const std::optional<Value> hostWritten = runtime_->instanceProperty(reader, "count");
  ASSERT_TRUE(hostWritten.has_value());
  EXPECT_EQ(hostWritten->integer, 11);
}

// Detects: a script-instance property write being counted and reported as a native-target refusal.
TEST_F(RuntimeCallsTest, WritingAnUndeclaredPropertyOnAScriptInstanceFailsAsUnknownNotAsANativeRefusal) {
  build({probeWithAutoCount()});
  const auto writer = ready("Probe", "000902:Probe.esp");
  const auto target = ready("Probe", "000903:Probe.esp");
  const std::uint64_t ticket = call(writer, "Poke", {fo4::papyrus::objectValue(target, "Probe", "")});
  runtime_->runStep(512);
  const CallRecord poked = record(ticket);
  EXPECT_EQ(poked.state, CallState::Failed);
  EXPECT_EQ(poked.status, Status::UnknownIdentifier);
  EXPECT_EQ(runtime_->nativeTargetRefusals(), 0);
}

// Detects: a synchronous host run stepping other ready calls, or losing the park limit.
TEST_F(RuntimeCallsTest, RunCallRunsOnlyItsOwnTicket) {
  build({actor()});
  const auto first = ready("Actor", "000904:Actor.esp");
  const auto second = ready("Actor", "000905:Actor.esp");
  const std::uint64_t ping = call(second, "Ping");
  const std::uint64_t give = call(first, "Give");

  const std::optional<SchedulerStepResult> ran = runtime_->runCall(give, 512);
  ASSERT_TRUE(ran.has_value());
  EXPECT_EQ(ran->completed, 1);
  EXPECT_EQ(record(give).state, CallState::Done);
  EXPECT_EQ(record(give).value.integer, 41);
  EXPECT_EQ(hits_, 0);
  EXPECT_EQ(record(ping).state, CallState::Pending);
  EXPECT_EQ(runtime_->readyCount(), 1u);
  EXPECT_FALSE(runtime_->runCall(give, 512).has_value());
  EXPECT_FALSE(runtime_->runCall(0, 512).has_value());

  runtime_->setMaxParksPerEvent(2);
  const std::uint64_t spin = call(first, "Spin");
  const std::optional<SchedulerStepResult> parked = runtime_->runCall(spin, 4);
  ASSERT_TRUE(parked.has_value());
  EXPECT_EQ(parked->parked, 1);
  EXPECT_EQ(record(spin).state, CallState::Pending);
  const std::optional<SchedulerStepResult> dropped = runtime_->runCall(spin, 4);
  ASSERT_TRUE(dropped.has_value());
  EXPECT_EQ(dropped->dropped, 1);
  EXPECT_EQ(record(spin).state, CallState::Dropped);
  EXPECT_EQ(hits_, 0);
  EXPECT_EQ(record(ping).state, CallState::Pending);
}

// Detects: calls of a detached instance kept in the ready and waiting queues and a late completion accepted for them.
TEST_F(RuntimeCallsTest, DetachedCallsLeaveTheQueuesAndTheirTokenIsRefused) {
  build({actor()});
  runtime_->registerNative("Waiter", "Wait",
                           NativeRegistration{NativeTargetKind::Static, "", {}, true,
                                              [](const std::vector<Value>&, Status&) { return std::uint64_t{7}; },
                                              {}});
  const auto instance = ready("Actor", "000906:Actor.esp");
  const std::uint64_t run = call(instance, "Run");
  runtime_->runStep(512);
  ASSERT_EQ(runtime_->waitingCount(), 1u);
  const std::uint64_t ping = call(instance, "Ping");

  ASSERT_TRUE(runtime_->detach(instance));
  runtime_->runStep(512);
  EXPECT_EQ(runtime_->readyCount(), 0u);
  EXPECT_EQ(runtime_->waitingCount(), 0u);
  EXPECT_EQ(record(run).state, CallState::Dropped);
  EXPECT_EQ(record(ping).state, CallState::Dropped);
  EXPECT_EQ(hits_, 0);
  EXPECT_FALSE(runtime_->completeLatent(7, fo4::papyrus::integerValue(1)));
}

// Detects: a wait token begun inside OnInit staying issued, so a later completion is stored instead of refused.
TEST_F(RuntimeCallsTest, AWaitBegunInOnInitIsReleasedWhenOnInitFails) {
  std::vector<PexObject> objects{actor()};
  objects[0].states[0].functions.push_back(
      method("OnInit", "None", {}, {ins("callstatic", {literal("Waiter"), literal("Wait"), id("::nonevar")}), done()}));
  build(std::move(objects));
  std::uint64_t token = 0;
  runtime_->registerNative("Waiter", "Wait",
                           NativeRegistration{NativeTargetKind::Static, "", {}, false, {},
                                              [this, &token](const NativeCall&, NativeResult& result) {
                                                token = runtime_->beginWait();
                                                result.latentToken = token;
                                                return true;
                                              }});
  const auto attached = runtime_->attach("Actor", OwnerId::forForm("000907:Actor.esp"));
  ASSERT_TRUE(attached.has_value());
  EXPECT_EQ(runtime_->initialize(*attached), Status::Latent);
  ASSERT_NE(token, 0u);
  EXPECT_FALSE(runtime_->completeLatent(token, fo4::papyrus::integerValue(1)));
}

// Detects: a gate refusal of the instruction budget leaving the ready call unsettled in run_call.
TEST_F(RuntimeCallsTest, RunCallSettlesAGateRefusedCallAsFailed) {
  build({actor()});
  const auto instance = ready("Actor", "000912:Actor.esp");
  const std::uint64_t give = call(instance, "Give");
  runtime_->setGate([](std::string_view key) { return key != "vm.scheduler.instruction_budget"; });

  const std::optional<SchedulerStepResult> ran = runtime_->runCall(give, 512);
  ASSERT_TRUE(ran.has_value());
  EXPECT_EQ(ran->refused, 1);
  EXPECT_EQ(record(give).state, CallState::Failed);
  EXPECT_EQ(record(give).status, Status::GateRefused);
  EXPECT_EQ(runtime_->readyCount(), 0u);
}

// Detects: a ready call of a detached instance being run instead of dropped by run_call.
TEST_F(RuntimeCallsTest, RunCallDropsAReadyCallOfADetachedInstance) {
  build({actor()});
  const auto instance = ready("Actor", "000913:Actor.esp");
  const std::uint64_t give = call(instance, "Give");
  ASSERT_TRUE(runtime_->detach(instance));

  const std::optional<SchedulerStepResult> ran = runtime_->runCall(give, 512);
  ASSERT_TRUE(ran.has_value());
  EXPECT_EQ(record(give).state, CallState::Dropped);
  EXPECT_EQ(record(give).status, Status::HostError);
  EXPECT_EQ(runtime_->readyCount(), 0u);
  EXPECT_EQ(hits_, 0);
}

// Detects: run_call resuming a call that is still parked in the waiting queue.
TEST_F(RuntimeCallsTest, RunCallRefusesAParkedLatentCallAndMovesNothing) {
  build({actor()});
  runtime_->registerNative("Waiter", "Wait",
                           NativeRegistration{NativeTargetKind::Static, "", {}, true,
                                              [](const std::vector<Value>&, Status&) { return std::uint64_t{7}; },
                                              {}});
  const auto instance = ready("Actor", "000914:Actor.esp");
  const std::uint64_t run = call(instance, "Run");
  const std::uint64_t give = call(instance, "Give");
  const std::optional<SchedulerStepResult> parked = runtime_->runCall(run, 512);
  ASSERT_TRUE(parked.has_value());
  ASSERT_EQ(runtime_->waitingCount(), 1u);
  EXPECT_EQ(record(run).state, CallState::Waiting);
  EXPECT_EQ(runtime_->readyCount(), 1u);

  EXPECT_FALSE(runtime_->runCall(run, 512).has_value());
  EXPECT_EQ(runtime_->waitingCount(), 1u);
  EXPECT_EQ(runtime_->readyCount(), 1u);
  EXPECT_EQ(record(run).state, CallState::Waiting);
  EXPECT_EQ(record(give).state, CallState::Pending);
}

// Detects: an Auto property seed stored without its declared type, or not seeded through an inherited declaration.
TEST_F(RuntimeCallsTest, AutoPropertySeedsAreCoercedAndInheritedThroughTheParentChain) {
  PexObject child = script("Child", {});
  child.parentClassName = "Probe";
  build({probeWithAutoCount(), child, scaled()});

  const auto scaledInstance = ready("Scaled", "000915:Scaled.esp", {{"Ratio", fo4::papyrus::integerValue(5)}});
  const std::optional<Value> ratio = runtime_->instanceVariable(scaledInstance, "::Ratio_var");
  ASSERT_TRUE(ratio.has_value());
  EXPECT_EQ(ratio->type, fo4::papyrus::ValueType::Float);
  EXPECT_FLOAT_EQ(ratio->real, 5.0F);

  const auto inherited = ready("Child", "000916:Child.esp", {{"Count", fo4::papyrus::integerValue(7)}});
  const std::optional<Value> seeded = runtime_->instanceVariable(inherited, "::Count_var");
  ASSERT_TRUE(seeded.has_value());
  EXPECT_EQ(seeded->integer, 7);

  const auto mismatched = ready("Scaled", "000917:Scaled.esp", {{"Ratio", fo4::papyrus::stringValue("wide")}});
  ASSERT_NE(mismatched, 0U);
  const std::optional<Value> unseeded = runtime_->instanceVariable(mismatched, "::Ratio_var");
  if (unseeded.has_value()) {
    EXPECT_EQ(unseeded->type, fo4::papyrus::ValueType::Float);
  }
}

struct WaitThenFail {
  std::uint64_t token{0};
};

int32_t beginWaitThenFail(void* user, radicle_papyrus_native_call* call) {
  auto* state = static_cast<WaitThenFail*>(user);
  if (radicle_papyrus_native_call_begin_wait(call, &state->token) != RADICLE_STATUS_OK) return 0;
  radicle_papyrus_native_call_fail(call, RADICLE_PAPYRUS_STATUS_HOST_ERROR);
  return 0;
}

class CApiRuntimeCallsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(radicle_papyrus_runtime_create(nullptr, &runtime_), RADICLE_STATUS_OK);
    PexFile model;
    model.majorVersion = 3;
    model.minorVersion = 9;
    model.gameId = 2;
    model.sourceFileName = "Actor.psc";
    model.objects.push_back(actor());
    model.rebuildStringTable();
    const std::vector<std::uint8_t> bytes = model.toBytes();
    ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex_), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime_, pex_), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_papyrus_runtime_destroy(runtime_);
    radicle_pex_destroy(pex_);
  }

  uint64_t ready(const char* form) {
    uint64_t instance = 0;
    EXPECT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, form, "", &instance),
              RADICLE_STATUS_OK);
    radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
    EXPECT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
    return instance;
  }

  uint64_t call(uint64_t instance, const char* function) {
    uint64_t ticket = 0;
    radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
    EXPECT_EQ(radicle_papyrus_runtime_call_function(runtime_, instance, function, nullptr, 0, &ticket, &status),
              RADICLE_STATUS_OK);
    EXPECT_NE(ticket, 0u);
    return ticket;
  }

  radicle_papyrus_call_state state(uint64_t ticket) {
    radicle_papyrus_call_info info{};
    info.struct_size = sizeof(info);
    EXPECT_EQ(radicle_papyrus_runtime_call_result(runtime_, ticket, 0, &info, nullptr), RADICLE_STATUS_OK);
    return info.state;
  }

  radicle_papyrus_runtime* runtime_{nullptr};
  radicle_pex* pex_{nullptr};
};

// Detects: run_call moving another ready call or accepting a finished ticket.
TEST_F(CApiRuntimeCallsTest, RunCallFinishesOneTicketAndRefusesATicketThatIsNotReady) {
  const uint64_t instance = ready("000910:Actor.esp");
  const uint64_t spin = call(instance, "Spin");
  const uint64_t give = call(instance, "Give");

  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_run_call(runtime_, give, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);
  EXPECT_EQ(state(give), RADICLE_PAPYRUS_CALL_DONE);
  EXPECT_EQ(state(spin), RADICLE_PAPYRUS_CALL_PENDING);

  EXPECT_EQ(radicle_papyrus_runtime_run_call(runtime_, give, 512, &step), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_papyrus_runtime_run_call(runtime_, give, 512, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_run_call(nullptr, give, 512, &step), RADICLE_STATUS_INVALID_ARGUMENT);
}

// Detects: a token from begin_wait followed by a 0 return staying issued, so a later completion is stored instead of refused.
TEST_F(CApiRuntimeCallsTest, ATokenFromABeginWaitThatReturnsZeroIsReleased) {
  WaitThenFail waiter;
  radicle_papyrus_engine_callbacks engine{};
  engine.struct_size = sizeof(engine);
  engine.user = &waiter;
  engine.call_native_ex = beginWaitThenFail;
  ASSERT_EQ(radicle_papyrus_runtime_set_engine_callbacks(runtime_, &engine), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Waiter", "Wait", RADICLE_PAPYRUS_NATIVE_STATIC, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT),
            RADICLE_STATUS_OK);
  const uint64_t run = call(ready("000911:Actor.esp"), "Run");
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_run_call(runtime_, run, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.refused, 1);
  EXPECT_EQ(state(run), RADICLE_PAPYRUS_CALL_FAILED);
  ASSERT_NE(waiter.token, 0u);

  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(3, &value), RADICLE_STATUS_OK);
  int32_t accepted = 1;
  ASSERT_EQ(radicle_papyrus_runtime_complete_latent(runtime_, waiter.token, value, &accepted), RADICLE_STATUS_OK);
  EXPECT_EQ(accepted, 0);
  radicle_papyrus_value_destroy(value);
}

struct Reenter {
  radicle_papyrus_runtime* runtime{nullptr};
  uint64_t other{0};
  radicle_status runStatus{RADICLE_STATUS_OK};
  radicle_status stepStatus{RADICLE_STATUS_OK};
};

int32_t reenterFromNative(void* user, radicle_papyrus_native_call* /*call*/) {
  auto* state = static_cast<Reenter*>(user);
  radicle_papyrus_step_result step{};
  state->runStatus = radicle_papyrus_runtime_run_call(state->runtime, state->other, 512, &step);
  state->stepStatus = radicle_papyrus_runtime_step(state->runtime, 512, &step);
  return 0;
}

// Detects: a native callback stepping the runtime again, which would shrink the ready queue under the running step.
TEST_F(CApiRuntimeCallsTest, ANativeCallbackCannotStepTheRuntimeAgain) {
  Reenter reenter;
  reenter.runtime = runtime_;
  radicle_papyrus_engine_callbacks engine{};
  engine.struct_size = sizeof(engine);
  engine.user = &reenter;
  engine.call_native_ex = reenterFromNative;
  ASSERT_EQ(radicle_papyrus_runtime_set_engine_callbacks(runtime_, &engine), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Waiter", "Wait", RADICLE_PAPYRUS_NATIVE_STATIC, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT),
            RADICLE_STATUS_OK);
  const uint64_t instance = ready("000917:Actor.esp");
  const uint64_t run = call(instance, "Run");
  const uint64_t give = call(instance, "Give");
  reenter.other = give;

  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_run_call(runtime_, run, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(reenter.runStatus, RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(reenter.stepStatus, RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(state(give), RADICLE_PAPYRUS_CALL_PENDING);

  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(state(give), RADICLE_PAPYRUS_CALL_DONE);
  EXPECT_EQ(state(run), RADICLE_PAPYRUS_CALL_FAILED);
}

}  // namespace
