#include "fo4/c_api_papyrus.h"

#include "fo4/c_api_pex.h"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexStruct;
using fo4::pex::PexStructMember;
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

PexObject object(std::string name, std::vector<PexStruct> structs, std::vector<PexFunction> functions) {
  PexState entry;
  entry.name = "";
  entry.functions = std::move(functions);
  PexObject result;
  result.name = std::move(name);
  result.autoStateName = "";
  result.structs = std::move(structs);
  result.states.push_back(std::move(entry));
  return result;
}

PexStruct pointStruct() {
  PexStruct point;
  point.name = "Point";
  point.members.push_back(PexStructMember{"X", "Int", 0, std::nullopt, false, {}});
  return point;
}

std::vector<std::uint8_t> pexBytes(const PexFile& model) {
  return model.toBytes();
}

struct CallbackState {
  bool allowGotoState{true};
  int nativeCalls{0};
  std::string lastScript;
  std::string lastFunction;
};

int32_t gateCallback(void* user, const char* key) {
  auto* state = static_cast<CallbackState*>(user);
  if (std::strcmp(key, "vm.states.gotostate") == 0 && !state->allowGotoState) return 0;
  return 1;
}

int32_t getVariableCallback(void* user, const radicle_papyrus_object*, const char* name,
                            radicle_papyrus_value** out_value) {
  (void)user;
  if (std::strcmp(name, "Answer") != 0) return 0;
  return radicle_papyrus_value_int(5, out_value) == RADICLE_STATUS_OK ? 1 : 0;
}

int32_t setVariableCallback(void*, const radicle_papyrus_object*, const char*, const radicle_papyrus_value*) {
  return 1;
}

int32_t getPropertyCallback(void*, const radicle_papyrus_object*, const char*, radicle_papyrus_value**) {
  return 0;
}

int32_t setPropertyCallback(void*, const radicle_papyrus_object*, const char*, const radicle_papyrus_value*) {
  return 0;
}

int32_t nativeCallback(void* user, const char* script, const char* function,
                       const radicle_papyrus_value* const*, size_t, radicle_papyrus_value** out_value) {
  auto* state = static_cast<CallbackState*>(user);
  state->nativeCalls += 1;
  state->lastScript = script;
  state->lastFunction = function;
  if (std::strcmp(script, "Tool") != 0 || std::strcmp(function, "Bonus") != 0) return 0;
  return radicle_papyrus_value_int(42, out_value) == RADICLE_STATUS_OK ? 1 : 0;
}

radicle_papyrus_callbacks callbacksFor(CallbackState& state) {
  radicle_papyrus_callbacks callbacks{};
  callbacks.user = &state;
  callbacks.gate = gateCallback;
  callbacks.get_variable = getVariableCallback;
  callbacks.set_variable = setVariableCallback;
  callbacks.get_property = getPropertyCallback;
  callbacks.set_property = setPropertyCallback;
  callbacks.call_native = nativeCallback;
  return callbacks;
}

class CApiPapyrusTest : public ::testing::Test {
 protected:
  void SetUp() override { callbacks_ = callbacksFor(state_); }

  void loadPex(const PexFile& model) {
    const std::vector<std::uint8_t> bytes = pexBytes(model);
    radicle_pex* pex = nullptr;
    ASSERT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &pex), RADICLE_STATUS_OK);
    ASSERT_NE(pex, nullptr);
    pex_.push_back(pex);
  }

  void createRuntime() {
    ASSERT_EQ(radicle_papyrus_create(&callbacks_, &runtime_), RADICLE_STATUS_OK);
    ASSERT_NE(runtime_, nullptr);
    for (radicle_pex* pex : pex_) ASSERT_EQ(radicle_papyrus_add_pex(runtime_, pex), RADICLE_STATUS_OK);
  }

  void TearDown() override {
    radicle_papyrus_destroy(runtime_);
    for (radicle_pex* pex : pex_) radicle_pex_destroy(pex);
  }

  CallbackState state_;
  radicle_papyrus_callbacks callbacks_{};
  radicle_papyrus* runtime_{nullptr};
  std::vector<radicle_pex*> pex_;
};

TEST_F(CApiPapyrusTest, RunsAStructFunctionAndReturnsItsMember) {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Point.psc";
  PexFunction make = method("Make", "Int", {},
                            {ins("struct_create", {id("point")}),
                             ins("struct_set", {id("point"), literal("X"), number(7)}),
                             ins("struct_get", {id("out"), id("point"), literal("X")}),
                             ins("return", {id("out")})});
  make.locals.push_back(PexTypedName{"point", "Point"});
  make.locals.push_back(PexTypedName{"out", "Int"});
  model.objects.push_back(object("PointScript", {pointStruct()}, {make}));
  model.rebuildStringTable();
  loadPex(model);
  createRuntime();
  ASSERT_EQ(radicle_papyrus_pex_count(runtime_), 1u);

  radicle_papyrus_value* result = nullptr;
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_OK;
  size_t instructions = 0;
  ASSERT_EQ(radicle_papyrus_run(runtime_, "PointScript", "Make", nullptr, 0, &result, &status, &instructions),
            RADICLE_STATUS_OK);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  EXPECT_GT(instructions, 0u);
  EXPECT_EQ(radicle_papyrus_value_kind_of(result), RADICLE_PAPYRUS_VALUE_INT);
  EXPECT_EQ(radicle_papyrus_value_int_of(result), 7);
  radicle_papyrus_value_destroy(result);
}

TEST_F(CApiPapyrusTest, NativeCallbackAndVariableCallbackServeTheRun) {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "Main.psc";
  PexFunction runFunction = method("Run", "Int", {},
                                   {ins("callstatic", {literal("Tool"), literal("Bonus"), id("bonus")}),
                                    ins("return", {id("bonus")})});
  runFunction.locals.push_back(PexTypedName{"bonus", "Int"});
  model.objects.push_back(object(
      "Main", {},
      {runFunction, method("ReadVariable", "Int", {}, {ins("return", {id("Answer")})})}));
  PexObject tool;
  tool.name = "Tool";
  tool.autoStateName = "";
  PexState toolState;
  toolState.name = "";
  PexFunction bonus;
  bonus.name = "Bonus";
  bonus.returnType = "Int";
  bonus.isNative = true;
  toolState.functions.push_back(bonus);
  tool.states.push_back(toolState);
  model.objects.push_back(std::move(tool));
  model.rebuildStringTable();
  loadPex(model);
  createRuntime();

  radicle_papyrus_value* result = nullptr;
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_OK;
  ASSERT_EQ(radicle_papyrus_run(runtime_, "Main", "Run", nullptr, 0, &result, &status, nullptr),
            RADICLE_STATUS_OK);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(result), 42);
  EXPECT_EQ(state_.nativeCalls, 1);
  EXPECT_EQ(state_.lastScript, "Tool");
  EXPECT_EQ(state_.lastFunction, "Bonus");
  radicle_papyrus_value_destroy(result);

  ASSERT_EQ(radicle_papyrus_run(runtime_, "Main", "ReadVariable", nullptr, 0, &result, &status, nullptr),
            RADICLE_STATUS_OK);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(radicle_papyrus_value_int_of(result), 5);
  radicle_papyrus_value_destroy(result);
}

TEST_F(CApiPapyrusTest, GateRefusalAndUnknownFunctionAreReported) {
  PexFile model;
  model.majorVersion = 3;
  model.minorVersion = 9;
  model.gameId = 2;
  model.sourceFileName = "State.psc";
  model.objects.push_back(object(
      "StateScript", {},
      {method("Go", "None", {},
              {ins("callmethod", {literal("GotoState"), id("self"), id("::nonevar"), literal("Alert")}),
               ins("return", {id("::NoneVar")})})}));
  model.rebuildStringTable();
  loadPex(model);
  createRuntime();
  state_.allowGotoState = false;

  radicle_papyrus_value* result = nullptr;
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_OK;
  ASSERT_EQ(radicle_papyrus_run(runtime_, "StateScript", "Go", nullptr, 0, &result, &status, nullptr),
            RADICLE_STATUS_OK);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_GATE_REFUSED);
  EXPECT_STREQ(radicle_papyrus_status_name(status), "GateRefused");

  ASSERT_EQ(radicle_papyrus_run(runtime_, "StateScript", "Missing", nullptr, 0, &result, &status, nullptr),
            RADICLE_STATUS_OK);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(status, RADICLE_PAPYRUS_STATUS_FUNCTION_NOT_FOUND);
}

TEST_F(CApiPapyrusTest, ValueConstructorsRoundTrip) {
  radicle_papyrus_value* value = nullptr;
  ASSERT_EQ(radicle_papyrus_value_string("text", 4, &value), RADICLE_STATUS_OK);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(radicle_papyrus_value_kind_of(value), RADICLE_PAPYRUS_VALUE_STRING);
  EXPECT_STREQ(radicle_papyrus_value_text(value), "text");
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_int(-3, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(value), -3);
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_float(1.5F, &value), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(radicle_papyrus_value_float_of(value), 1.5F);
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_bool(1, &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_bool_of(value), 1);
  radicle_papyrus_value_destroy(value);

  ASSERT_EQ(radicle_papyrus_value_object(9, "Actor", "Alert", &value), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_object_handle(value), 9u);
  EXPECT_STREQ(radicle_papyrus_value_object_type(value), "Actor");
  EXPECT_STREQ(radicle_papyrus_value_object_state(value), "Alert");
  radicle_papyrus_value_destroy(value);

  radicle_papyrus_value* array = nullptr;
  ASSERT_EQ(radicle_papyrus_value_array(2, &array), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_array_count(array), 2u);
  radicle_papyrus_value* element = nullptr;
  ASSERT_EQ(radicle_papyrus_value_int(4, &element), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_array_set(array, 0, element), RADICLE_STATUS_OK);
  radicle_papyrus_value_destroy(element);
  ASSERT_EQ(radicle_papyrus_value_array_get(array, 0, &element), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(element), 4);
  radicle_papyrus_value_destroy(element);
  radicle_papyrus_value_destroy(array);

  radicle_papyrus_value* point = nullptr;
  ASSERT_EQ(radicle_papyrus_value_struct("Point", &point), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_int(8, &element), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_papyrus_value_struct_set(point, "X", element), RADICLE_STATUS_OK);
  radicle_papyrus_value_destroy(element);
  ASSERT_EQ(radicle_papyrus_value_struct_get(point, "x", &element), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_papyrus_value_int_of(element), 8);
  radicle_papyrus_value_destroy(element);
  EXPECT_EQ(radicle_papyrus_value_struct_get(point, "Missing", &element), RADICLE_STATUS_NOT_FOUND);
  radicle_papyrus_value_destroy(point);
}

TEST_F(CApiPapyrusTest, RejectsBadArgumentsAndClearsOutParameters) {
  radicle_papyrus* runtime = nullptr;
  EXPECT_EQ(radicle_papyrus_create(nullptr, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_papyrus_create(nullptr, &runtime), RADICLE_STATUS_OK);
  ASSERT_NE(runtime, nullptr);
  EXPECT_EQ(radicle_papyrus_add_pex(runtime, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_papyrus_value* result = reinterpret_cast<radicle_papyrus_value*>(static_cast<std::uintptr_t>(1));
  radicle_papyrus_status status = RADICLE_PAPYRUS_STATUS_INTERNAL_ERROR;
  size_t instructions = 99;
  EXPECT_EQ(radicle_papyrus_run(runtime, nullptr, "Run", nullptr, 0, &result, &status, &instructions),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(instructions, 0u);
  radicle_papyrus_destroy(runtime);
}

}  // namespace
