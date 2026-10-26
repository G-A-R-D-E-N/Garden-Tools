#include "fo4/c_api_papyrus_runtime.h"

#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
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

}  // namespace
