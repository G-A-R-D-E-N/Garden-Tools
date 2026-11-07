#include "fo4/c_api_papyrus_runtime.h"

#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

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

PexInstruction ins(std::string mnemonic, std::vector<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcodeOf(mnemonic);
  instruction.mnemonic = std::move(mnemonic);
  instruction.fixedArgCount = static_cast<int>(args.size());
  instruction.hasVarArgs = false;
  instruction.args = std::move(args);
  return instruction;
}

PexFunction method(std::string name, std::string returnType, std::vector<PexInstruction> body) {
  PexFunction function;
  function.name = std::move(name);
  function.returnType = std::move(returnType);
  function.instructions = std::move(body);
  return function;
}

struct CallbackState {
  int hits{0};
  int overrides{0};
  bool allow{true};
  std::int64_t latentToken{9};
  int engineCalls{0};
  std::uint64_t selfHandle{0};
  std::uint64_t waitToken{0};
  std::uint64_t propertyHandle{0};
  std::int32_t stored{0};
};

int32_t gateCallback(void* user, const char*) {
  return static_cast<CallbackState*>(user)->allow ? 1 : 0;
}

int32_t nativeCallback(void* user, const char*, const char*, const radicle_papyrus_value* const* args,
                       size_t count, radicle_papyrus_value** out_value) {
  auto* state = static_cast<CallbackState*>(user);
  state->hits += 1;
  if (count > 0) (void)args;
  return radicle_papyrus_value_int(42, out_value) == RADICLE_STATUS_OK ? 1 : 0;
}

int32_t overrideCallback(void* user, uint64_t, const char*, const radicle_papyrus_value* const*, size_t,
                         int32_t* out_outcome, radicle_papyrus_value** out_value) {
  auto* state = static_cast<CallbackState*>(user);
  state->overrides += 1;
  *out_outcome = RADICLE_PAPYRUS_OVERRIDE_HANDLED;
  return radicle_papyrus_value_int(7, out_value) == RADICLE_STATUS_OK ? 1 : 0;
}

int64_t beginLatentCallback(void* user, const char*, const char*, const radicle_papyrus_value* const*, size_t) {
  return static_cast<CallbackState*>(user)->latentToken;
}

PexValue number(std::int32_t amount) {
  PexValue value;
  value.type = PexValueType::Integer;
  value.int_ = amount;
  return value;
}

PexFunction withTarget(PexFunction function) {
  function.params.push_back(PexTypedName{"target", "Var"});
  return function;
}

PexFile modModel() {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Mod.psc";
  PexObject mod;
  mod.name = "Mod";
  mod.autoStateName = "";
  PexState entry;
  entry.name = "";
  entry.functions.push_back(withTarget(method("Poke", "None",
                                              {ins("callmethod", {literal("Touch"), id("target"), id("::nonevar")}),
                                               ins("return", {id("::NoneVar")})})));
  entry.functions.push_back(withTarget(method("PokePlain", "None",
                                              {ins("callmethod", {literal("Plain"), id("target"), id("::nonevar")}),
                                               ins("return", {id("::NoneVar")})})));
  entry.functions.push_back(withTarget(method("Read", "None",
                                              {ins("propget", {literal("Health"), id("target"), id("result")}),
                                               ins("return", {id("::NoneVar")})})));
  entry.functions.push_back(withTarget(method("Write", "None",
                                              {ins("propset", {literal("Health"), id("target"), number(9)}),
                                               ins("return", {id("::NoneVar")})})));
  mod.states.push_back(std::move(entry));
  model.objects.push_back(std::move(mod));
  model.rebuildStringTable();
  return model;
}

int32_t engineCallNative(void* user, radicle_papyrus_native_call* call) {
  auto* state = static_cast<CallbackState*>(user);
  state->engineCalls += 1;
  if (std::strcmp(radicle_papyrus_native_call_function(call), "Wait") == 0) {
    uint64_t token = 0;
    if (radicle_papyrus_native_call_begin_wait(call, &token) != RADICLE_STATUS_OK) return 0;
    state->waitToken = token;
    return 1;
  }
  const radicle_papyrus_object* self = radicle_papyrus_native_call_self(call);
  state->selfHandle = self == nullptr ? 0 : radicle_papyrus_object_handle(self);
  return 1;
}

int32_t engineGetProperty(void* user, const radicle_papyrus_object* object, const char* name,
                          radicle_papyrus_value** out_value) {
  auto* state = static_cast<CallbackState*>(user);
  state->propertyHandle = radicle_papyrus_object_handle(object);
  if (std::strcmp(name, "Health") != 0) return 0;
  return radicle_papyrus_value_int(77, out_value) == RADICLE_STATUS_OK ? 1 : 0;
}

int32_t engineSetProperty(void* user, const radicle_papyrus_object*, const char* name,
                          const radicle_papyrus_value* value) {
  auto* state = static_cast<CallbackState*>(user);
  if (std::strcmp(name, "Health") != 0) return 0;
  state->stored = radicle_papyrus_value_int_of(value);
  return 1;
}

radicle_papyrus_value* engineValue(uint64_t handle, const char* type) {
  radicle_papyrus_value* value = nullptr;
  EXPECT_EQ(radicle_papyrus_value_object(handle, type, "", &value), RADICLE_STATUS_OK);
  return value;
}

class CApiPapyrusRuntimeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    callbacks_.user = &state_;
    callbacks_.gate = gateCallback;
    callbacks_.call_native = nativeCallback;
    callbacks_.override_function = overrideCallback;
    callbacks_.begin_latent = beginLatentCallback;
    ASSERT_EQ(radicle_papyrus_runtime_create(&callbacks_, &runtime_), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_papyrus_runtime_destroy(runtime_);
    for (radicle_pex* pex : pex_) radicle_pex_destroy(pex);
  }

  void load(const PexFile& model) {
    const std::vector<std::uint8_t> bytes = model.toBytes();
    radicle_pex* pex = nullptr;
    ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
    ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime_, pex), RADICLE_STATUS_OK);
    pex_.push_back(pex);
  }

  CallbackState state_;
  radicle_papyrus_runtime_callbacks callbacks_{};
  radicle_papyrus_runtime* runtime_{nullptr};
  std::vector<radicle_pex*> pex_;
};

PexFile actorModel() {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Actor.psc";
  PexObject actor;
  actor.name = "Actor";
  actor.autoStateName = "";
  PexState entry;
  entry.name = "";
  entry.functions.push_back(method("OnInit", "None", {ins("return", {id("::NoneVar")})}));
  entry.functions.push_back(method("Ping", "None",
                                   {ins("callstatic", {literal("Hook"), literal("Hit"), id("result")}),
                                    ins("return", {id("::NoneVar")})}));
  entry.functions.push_back(method("Run", "None",
                                   {ins("callstatic", {literal("Waiter"), literal("Wait"), id("result")}),
                                    ins("return", {id("::NoneVar")})}));
  actor.states.push_back(std::move(entry));
  model.objects.push_back(std::move(actor));
  model.rebuildStringTable();
  return model;
}

TEST_F(CApiPapyrusRuntimeTest, AttachesInitializesRunsAnEventAndReadsAVariable) {
  load(actorModel());
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "000800:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  EXPECT_NE(instance, 0u);
  EXPECT_EQ(radicle_papyrus_runtime_lifecycle(runtime_, instance),
            RADICLE_PAPYRUS_LIFECYCLE_NOT_INITIALIZED);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_runtime_lifecycle(runtime_, instance), RADICLE_PAPYRUS_LIFECYCLE_READY);

  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime_, "Hook", "Hit", RADICLE_PAPYRUS_NATIVE_STATIC, "", 0), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Ping", nullptr, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);
  EXPECT_EQ(state_.hits, 1);

  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_get_variable(runtime_, instance, "result", &value), RADICLE_STATUS_OK);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(radicle_papyrus_value_int_of(value), 42);
  radicle_papyrus_value_destroy(value);

  radicle_papyrus_value* stored = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(5, &stored), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_runtime_set_variable(runtime_, instance, "Count", stored), RADICLE_STATUS_OK);
  radicle_papyrus_value_destroy(stored);
  ASSERT_EQ(radicle_papyrus_runtime_get_variable(runtime_, instance, "count", &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(value), 5);
  radicle_papyrus_value_destroy(value);
}

TEST_F(CApiPapyrusRuntimeTest, LatentNativeSuspendsAndResumes) {
  load(actorModel());
  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime_, "Waiter", "Wait",
                                                    RADICLE_PAPYRUS_NATIVE_STATIC, "", 1),
            RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "000801:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Run", nullptr, 0, &status),
            RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.waiting, 1);

  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(3, &value), RADICLE_STATUS_OK);
  int32_t accepted = 0;
  ASSERT_EQ(radicle_papyrus_runtime_complete_latent(runtime_, 9, value, &accepted), RADICLE_STATUS_OK);
  EXPECT_EQ(accepted, 1);
  radicle_papyrus_value_destroy(value);
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);
}

TEST_F(CApiPapyrusRuntimeTest, OverridesRegistrationsAndRefusals) {
  load(actorModel());
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "000802:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);

  ASSERT_EQ(radicle_papyrus_runtime_register_override(runtime_, "Actor", "Ping", -1), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime_, "Hook", "Hit", RADICLE_PAPYRUS_NATIVE_STATIC, "", 0), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Ping", nullptr, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(state_.overrides, 1);
  EXPECT_EQ(state_.hits, 0);

  int64_t custom = 0;
  ASSERT_EQ(radicle_papyrus_runtime_register_custom_event(runtime_, instance, "OnPing", &custom),
            RADICLE_STATUS_OK);
  EXPECT_GT(custom, 0);
  int64_t timer = 0;
  ASSERT_EQ(radicle_papyrus_runtime_register_timer(runtime_, instance, 1.0, 1,
                                                   RADICLE_PAPYRUS_CLOCK_REAL_TIME, &timer),
            RADICLE_STATUS_OK);
  int32_t fired = 0;
  ASSERT_EQ(radicle_papyrus_runtime_advance_clock(runtime_, "GameSession", 2.0,
                                                  RADICLE_PAPYRUS_CLOCK_REAL_TIME, &fired),
            RADICLE_STATUS_OK);
  EXPECT_EQ(fired, 2);
  int32_t removed = 0;
  ASSERT_EQ(radicle_papyrus_runtime_unregister(runtime_, custom, &removed), RADICLE_STATUS_OK);
  EXPECT_EQ(removed, 1);

  EXPECT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Missing", nullptr, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_FUNCTION_NOT_FOUND);
}

TEST_F(CApiPapyrusRuntimeTest, DetachReportsWhetherTheInstanceExisted) {
  load(actorModel());
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "000803:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_runtime_detach(runtime_, instance), 1);
  EXPECT_EQ(radicle_papyrus_runtime_detach(runtime_, instance + 100), 0);
  EXPECT_EQ(radicle_papyrus_runtime_detach(nullptr, instance), 0);
}

TEST_F(CApiPapyrusRuntimeTest, InstanceStateIsTheAutoStateAndAnUnknownInstanceHasNone) {
  PexFile model = actorModel();
  model.objects[0].autoStateName = "Idle";
  model.objects[0].states[0].name = "Idle";
  model.rebuildStringTable();
  load(model);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "000804:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  EXPECT_STREQ(radicle_papyrus_runtime_instance_state(runtime_, instance), "Idle");
  EXPECT_STREQ(radicle_papyrus_runtime_instance_state(runtime_, instance + 100), "");
  EXPECT_EQ(radicle_papyrus_runtime_instance_state(nullptr, instance), nullptr);
}

TEST_F(CApiPapyrusRuntimeTest, FailedInitializeReportsItsStatusAsTheFailureText) {
  PexFile model = actorModel();
  model.objects[0].states[0].functions[0].instructions = {
      ins("callstatic", {literal("Missing"), literal("Hit"), id("result")}),
      ins("return", {id("::NoneVar")})};
  model.rebuildStringTable();
  load(model);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM,
                                           "000805:Actor.esp", "", &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_OK;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  EXPECT_NE(status, RADICLE_PAPYRUS_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_runtime_lifecycle(runtime_, instance), RADICLE_PAPYRUS_LIFECYCLE_FAILED);
  EXPECT_STREQ(radicle_papyrus_runtime_failure(runtime_, instance), radicle_papyrus_status_name(status));
  EXPECT_STREQ(radicle_papyrus_runtime_failure(runtime_, instance + 100), "no such instance");
  EXPECT_EQ(radicle_papyrus_runtime_failure(nullptr, instance), nullptr);
}

TEST_F(CApiPapyrusRuntimeTest, EngineNativeGetsSelfAndRequiredScript) {
  load(modModel());
  radicle_papyrus_engine_callbacks engine{};
  engine.struct_size = sizeof(engine);
  engine.user = &state_;
  engine.call_native_ex = engineCallNative;
  ASSERT_EQ(radicle_papyrus_runtime_set_engine_callbacks(runtime_, &engine), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Widget", "Touch",
                                                       RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT, "", 0),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Gadget", "Touch",
                                                       RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT, "Widget", 0),
            RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Mod", RADICLE_PAPYRUS_OWNER_FORM, "000820:Mod.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);

  radicle_papyrus_value* widget = engineValue(RADICLE_PAPYRUS_ENGINE_HANDLE_BIT | 5, "Widget");
  ASSERT_NE(widget, nullptr);
  const radicle_papyrus_value* widgetArgs[] = {widget};
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Poke", widgetArgs, 1, &status),
            RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(state_.engineCalls, 1);
  EXPECT_EQ(state_.selfHandle, RADICLE_PAPYRUS_ENGINE_HANDLE_BIT | 5);

  radicle_papyrus_value* gadget = engineValue(RADICLE_PAPYRUS_ENGINE_HANDLE_BIT | 6, "Gadget");
  ASSERT_NE(gadget, nullptr);
  const radicle_papyrus_value* gadgetArgs[] = {gadget};
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Poke", gadgetArgs, 1, &status),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(state_.engineCalls, 1);
  EXPECT_GT(step.refused, 0);
  radicle_papyrus_value_destroy(widget);
  radicle_papyrus_value_destroy(gadget);
}

TEST_F(CApiPapyrusRuntimeTest, AcceptScriptInstanceFlagNeedsAnEngineObjectNative) {
  EXPECT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Hook", "Hit", RADICLE_PAPYRUS_NATIVE_STATIC, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Actor", "Run",
                                                       RADICLE_PAPYRUS_NATIVE_SCRIPT_INSTANCE, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Widget", "Touch",
                                                       RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE),
            RADICLE_STATUS_OK);
}

TEST_F(CApiPapyrusRuntimeTest, NativeFlagsRejectUnknownBits) {
  EXPECT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Hook", "Hit", RADICLE_PAPYRUS_NATIVE_STATIC, "", 4),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Widget", "Touch",
                                                       RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_ACCEPT_SCRIPT_INSTANCE | 4),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiPapyrusRuntimeTest, NativeStartsWaitFromCallAndCompleteResumes) {
  load(actorModel());
  radicle_papyrus_engine_callbacks engine{};
  engine.struct_size = sizeof(engine);
  engine.user = &state_;
  engine.call_native_ex = engineCallNative;
  ASSERT_EQ(radicle_papyrus_runtime_set_engine_callbacks(runtime_, &engine), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_register_native_ex(runtime_, "Waiter", "Wait",
                                                       RADICLE_PAPYRUS_NATIVE_STATIC, "",
                                                       RADICLE_PAPYRUS_NATIVE_FLAG_MAY_WAIT),
            RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "000821:Actor.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Run", nullptr, 0, &status),
            RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.waiting, 1);
  EXPECT_NE(state_.waitToken, 0u);

  int32_t accepted = 0;
  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(3, &value), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_complete_latent(runtime_, state_.waitToken, value, &accepted),
            RADICLE_STATUS_OK);
  EXPECT_EQ(accepted, 1);
  radicle_papyrus_value_destroy(value);
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);
}

TEST_F(CApiPapyrusRuntimeTest, EnginePropertyGetSet) {
  load(modModel());
  radicle_papyrus_engine_callbacks engine{};
  engine.struct_size = sizeof(engine);
  engine.user = &state_;
  engine.get_property = engineGetProperty;
  engine.set_property = engineSetProperty;
  ASSERT_EQ(radicle_papyrus_runtime_set_engine_callbacks(runtime_, &engine), RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Mod", RADICLE_PAPYRUS_OWNER_FORM, "000822:Mod.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  radicle_papyrus_value* widget = engineValue(RADICLE_PAPYRUS_ENGINE_HANDLE_BIT | 9, "Widget");
  ASSERT_NE(widget, nullptr);
  const radicle_papyrus_value* args[] = {widget};

  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Read", args, 1, &status), RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(state_.propertyHandle, RADICLE_PAPYRUS_ENGINE_HANDLE_BIT | 9);
  radicle_papyrus_value* read = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_get_variable(runtime_, instance, "result", &read), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(read), 77);
  radicle_papyrus_value_destroy(read);

  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Write", args, 1, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(state_.stored, 9);
  radicle_papyrus_value_destroy(widget);
}

TEST_F(CApiPapyrusRuntimeTest, V1RegistrationStillStaticOnly) {
  load(modModel());
  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime_, "Widget", "Plain",
                                                    RADICLE_PAPYRUS_NATIVE_ENGINE_OBJECT, "", 0),
            RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Mod", RADICLE_PAPYRUS_OWNER_FORM, "000823:Mod.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  radicle_papyrus_value* widget = engineValue(RADICLE_PAPYRUS_ENGINE_HANDLE_BIT | 3, "Widget");
  ASSERT_NE(widget, nullptr);
  const radicle_papyrus_value* args[] = {widget};
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "PokePlain", args, 1, &status),
            RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(state_.hits, 0);
  EXPECT_EQ(step.refused, 1);
  radicle_papyrus_value_destroy(widget);
}

TEST_F(CApiPapyrusRuntimeTest, CallFunctionReadsItsReturnValueAfterStep) {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Give.psc";
  PexObject give;
  give.name = "Give";
  give.autoStateName = "";
  PexState entry;
  entry.name = "";
  entry.functions.push_back(method("OnInit", "None", {ins("return", {id("::NoneVar")})}));
  entry.functions.push_back(method("Give", "Int", {ins("return", {number(41)})}));
  give.states.push_back(std::move(entry));
  model.objects.push_back(std::move(give));
  model.rebuildStringTable();
  load(model);

  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Give", RADICLE_PAPYRUS_OWNER_FORM, "000830:Give.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);

  uint64_t ticket = 1;
  ASSERT_EQ(radicle_papyrus_runtime_call_function(runtime_, instance, "Give", nullptr, 0, &ticket, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  ASSERT_NE(ticket, 0u);

  radicle_papyrus_call_info info{};
  info.struct_size = sizeof(info);
  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_call_result(runtime_, ticket, 0, &info, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(info.state, RADICLE_PAPYRUS_CALL_PENDING);
  EXPECT_EQ(value, nullptr);

  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);

  ASSERT_EQ(radicle_papyrus_runtime_call_result(runtime_, ticket, 1, &info, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(info.state, RADICLE_PAPYRUS_CALL_DONE);
  EXPECT_EQ(info.status, RADICLE_PAPYRUS_STATUS_OK);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(radicle_papyrus_value_int_of(value), 41);
  radicle_papyrus_value_destroy(value);

  value = nullptr;
  EXPECT_EQ(radicle_papyrus_runtime_call_result(runtime_, ticket, 1, &info, &value), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(value, nullptr);

  ticket = 1;
  ASSERT_EQ(radicle_papyrus_runtime_call_function(runtime_, 999, "Give", nullptr, 0, &ticket, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(ticket, 0u);
  EXPECT_NE(status, RADICLE_PAPYRUS_STATUS_OK);
}

TEST_F(CApiPapyrusRuntimeTest, OnInitPolicyQueuesAnEventThatArrivesDuringOnInit) {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Queued.psc";
  PexObject queued;
  queued.name = "Queued";
  queued.autoStateName = "";
  PexState entry;
  entry.name = "";
  entry.functions.push_back(method("OnInit", "None",
                                   {ins("callstatic", {literal("Hook"), literal("Arrive"), id("::nonevar")}),
                                    ins("return", {id("::NoneVar")})}));
  entry.functions.push_back(method("Ping", "None", {ins("return", {id("::NoneVar")})}));
  queued.states.push_back(std::move(entry));
  model.objects.push_back(std::move(queued));
  model.rebuildStringTable();
  load(model);

  struct Reentry {
    radicle_papyrus_runtime* runtime;
    uint64_t instance;
  };
  Reentry reentry{nullptr, 0};
  radicle_papyrus_runtime_callbacks callbacks{};
  callbacks.user = &reentry;
  callbacks.call_native = [](void* user, const char*, const char*, const radicle_papyrus_value* const*, size_t,
                             radicle_papyrus_value**) -> int32_t {
    auto* state = static_cast<Reentry*>(user);
    radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_OK;
    return radicle_papyrus_runtime_send_event(state->runtime, state->instance, "Ping", nullptr, 0, &status) ==
                   RADICLE_STATUS_OK
               ? 1
               : 0;
  };
  radicle_papyrus_runtime* runtime = nullptr;
  ASSERT_EQ(radicle_papyrus_runtime_create(&callbacks, &runtime), RADICLE_STATUS_OK);
  std::unique_ptr<radicle_papyrus_runtime, void (*)(radicle_papyrus_runtime*)> guard(runtime,
                                                                                     radicle_papyrus_runtime_destroy);
  ASSERT_EQ(radicle_papyrus_runtime_add_pex(runtime, pex_.back()), RADICLE_STATUS_OK);

  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime, "Queued", RADICLE_PAPYRUS_OWNER_FORM, "000840:Queued.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  reentry = Reentry{runtime, instance};
  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime, "Hook", "Arrive", RADICLE_PAPYRUS_NATIVE_STATIC, "", 0),
            RADICLE_STATUS_OK);

  ASSERT_EQ(radicle_papyrus_runtime_set_oninit_policy(runtime, RADICLE_PAPYRUS_ONINIT_QUEUE), RADICLE_STATUS_OK);

  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime, instance, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime, 512, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.completed, 1);

  radicle_papyrus_runtime_stats stats{};
  stats.struct_size = sizeof(stats);
  ASSERT_EQ(radicle_papyrus_runtime_get_stats(runtime, &stats), RADICLE_STATUS_OK);
  EXPECT_EQ(stats.queued_during_oninit, 1);
  EXPECT_EQ(stats.completed, 1);
}

TEST_F(CApiPapyrusRuntimeTest, MaxParksDropsASliceThatParksTooOften) {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Loop.psc";
  PexObject loop;
  loop.name = "Loop";
  loop.autoStateName = "";
  PexState entry;
  entry.name = "";
  entry.functions.push_back(method("OnInit", "None", {ins("return", {id("::NoneVar")})}));
  entry.functions.push_back(method("Spin", "None", {ins("jmp", {number(0)})}));
  loop.states.push_back(std::move(entry));
  model.objects.push_back(std::move(loop));
  model.rebuildStringTable();
  load(model);

  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Loop", RADICLE_PAPYRUS_OWNER_FORM, "000841:Loop.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_runtime_set_max_parks(runtime_, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_papyrus_runtime_set_max_parks(runtime_, 2), RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Spin", nullptr, 0, &status), RADICLE_STATUS_OK);

  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 4, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.paused, 1);
  EXPECT_EQ(step.parked, 1);
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 4, &step), RADICLE_STATUS_OK);
  EXPECT_EQ(step.paused, 1);
  EXPECT_EQ(step.dropped, 1);

  radicle_papyrus_runtime_stats stats{};
  stats.struct_size = sizeof(stats);
  ASSERT_EQ(radicle_papyrus_runtime_get_stats(runtime_, &stats), RADICLE_STATUS_OK);
  EXPECT_EQ(stats.dropped, 1);
}

TEST_F(CApiPapyrusRuntimeTest, HasScriptReportsLoadedScripts) {
  load(actorModel());
  EXPECT_EQ(radicle_papyrus_runtime_has_script(runtime_, "Actor"), 1);
  EXPECT_EQ(radicle_papyrus_runtime_has_script(runtime_, "Missing"), 0);
  EXPECT_EQ(radicle_papyrus_runtime_has_script(runtime_, nullptr), 0);
  EXPECT_EQ(radicle_papyrus_runtime_has_script(nullptr, "Actor"), 0);
}

TEST_F(CApiPapyrusRuntimeTest, InstanceDeclaresReportsItsOwnScriptsFunctions) {
  load(actorModel());
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "000842:Actor.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_runtime_instance_declares(runtime_, instance, "Ping"), 1);
  EXPECT_EQ(radicle_papyrus_runtime_instance_declares(runtime_, instance, "Missing"), 0);
  EXPECT_EQ(radicle_papyrus_runtime_instance_declares(runtime_, 999, "Ping"), 0);
}

TEST_F(CApiPapyrusRuntimeTest, InstanceScriptNamesTheScriptAndAnUnknownInstanceHasNone) {
  load(actorModel());
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "000843:Actor.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  const char* name = radicle_papyrus_runtime_instance_script(runtime_, instance);
  ASSERT_NE(name, nullptr);
  EXPECT_STREQ(name, "Actor");
  EXPECT_EQ(radicle_papyrus_runtime_instance_script(runtime_, 999), nullptr);
}

TEST_F(CApiPapyrusRuntimeTest, ListInstancesReportsCountAndTruncatesToCapacity) {
  load(actorModel());
  uint64_t first = 0;
  uint64_t second = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "000844:Actor.esp", "",
                                           &first),
            RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "000845:Actor.esp", "",
                                           &second),
            RADICLE_STATUS_OK);
  size_t count = 99;
  ASSERT_EQ(radicle_papyrus_runtime_list_instances(runtime_, nullptr, 0, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  uint64_t ids[2] = {0, 0};
  ASSERT_EQ(radicle_papyrus_runtime_list_instances(runtime_, ids, 1, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(ids[0], first);
  EXPECT_EQ(ids[1], 0u);
  ASSERT_EQ(radicle_papyrus_runtime_list_instances(runtime_, ids, 2, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(ids[0], first);
  EXPECT_EQ(ids[1], second);
  EXPECT_EQ(radicle_papyrus_runtime_list_instances(runtime_, ids, 2, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CApiPapyrusRuntimeTest, GetStatsCountsCompletedEventsAndRejectsASmallStruct) {
  load(actorModel());
  ASSERT_EQ(radicle_papyrus_runtime_register_native(runtime_, "Hook", "Hit", RADICLE_PAPYRUS_NATIVE_STATIC, "", 0),
            RADICLE_STATUS_OK);
  uint64_t instance = 0;
  ASSERT_EQ(radicle_papyrus_runtime_attach(runtime_, "Actor", RADICLE_PAPYRUS_OWNER_FORM, "000846:Actor.esp", "",
                                           &instance),
            RADICLE_STATUS_OK);
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  ASSERT_EQ(radicle_papyrus_runtime_initialize(runtime_, instance, &status), RADICLE_STATUS_OK);

  radicle_papyrus_runtime_stats stats{};
  EXPECT_EQ(radicle_papyrus_runtime_get_stats(runtime_, &stats), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_papyrus_runtime_get_stats(runtime_, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_papyrus_runtime_send_event(runtime_, instance, "Ping", nullptr, 0, &status), RADICLE_STATUS_OK);
  radicle_papyrus_step_result step{};
  ASSERT_EQ(radicle_papyrus_runtime_step(runtime_, 512, &step), RADICLE_STATUS_OK);

  stats = radicle_papyrus_runtime_stats{};
  stats.struct_size = sizeof(stats);
  ASSERT_EQ(radicle_papyrus_runtime_get_stats(runtime_, &stats), RADICLE_STATUS_OK);
  EXPECT_EQ(stats.completed, 1);
  EXPECT_EQ(stats.ready, 0);
  EXPECT_EQ(stats.waiting, 0);
  EXPECT_EQ(stats.refused, 0);
}

}  // namespace
