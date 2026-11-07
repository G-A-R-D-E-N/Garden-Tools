#include "fo4/papyrus/interpreter.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::Host;
using fo4::papyrus::Interpreter;
using fo4::papyrus::Limits;
using fo4::papyrus::ObjectRef;
using fo4::papyrus::Result;
using fo4::papyrus::Status;
using fo4::papyrus::Value;
using fo4::papyrus::ValueType;
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

using Native = std::function<bool(const std::vector<Value>&, Value&, Status&)>;

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

PexTypedName typed(std::string name, std::string type) {
  return PexTypedName{std::move(name), std::move(type)};
}

PexFunction method(std::string name, std::string returnType, std::vector<PexTypedName> params,
                   std::vector<PexTypedName> locals, std::vector<PexInstruction> body) {
  PexFunction function;
  function.name = std::move(name);
  function.returnType = std::move(returnType);
  function.params = std::move(params);
  function.locals = std::move(locals);
  function.instructions = std::move(body);
  return function;
}

PexState state(std::string name, std::vector<PexFunction> functions) {
  PexState entry;
  entry.name = std::move(name);
  entry.functions = std::move(functions);
  return entry;
}

std::string key(const std::string& type, const std::string& name) {
  return fo4::papyrus::lowerAscii(type) + "." + fo4::papyrus::lowerAscii(name);
}

class TestHost : public Host {
 public:
  const fo4::pex::PexObject* findObject(const std::string& name) const override {
    const auto found = scripts.find(fo4::papyrus::lowerAscii(name));
    return found == scripts.end() ? nullptr : &found->second;
  }

  bool getProperty(const ObjectRef& object, const std::string& name, Value& out,
                   Status& status) override {
    return lookup(properties, object, name, out, status);
  }

  bool setProperty(const ObjectRef& object, const std::string& name, const Value& value,
                   Status& status) override {
    return store(properties, object, name, value, status);
  }

  bool getVariable(const ObjectRef& object, const std::string& name, Value& out,
                   Status& status) override {
    return lookup(variables, object, name, out, status);
  }

  bool setVariable(const ObjectRef& object, const std::string& name, const Value& value,
                   Status& status) override {
    return store(variables, object, name, value, status);
  }

  bool callNative(const std::string& script, const std::string& function,
                  const std::vector<Value>& args, Value& out, Status& status) override {
    const auto found = natives.find(key(script, function));
    if (found == natives.end()) {
      status = Status::FunctionNotFound;
      return false;
    }
    return found->second(args, out, status);
  }

  void addScript(PexObject object) { scripts[fo4::papyrus::lowerAscii(object.name)] = std::move(object); }

  void addNative(const std::string& script, const std::string& function, Native native) {
    natives[key(script, function)] = std::move(native);
  }

  std::map<std::string, PexObject> scripts;
  std::map<std::string, Native> natives;
  std::map<std::string, Value> properties;
  std::map<std::string, Value> variables;

 private:
  static bool lookup(const std::map<std::string, Value>& table, const ObjectRef& object,
                     const std::string& name, Value& out, Status& status) {
    const auto found = table.find(key(object.type, name));
    if (found == table.end()) {
      status = Status::UnknownIdentifier;
      return false;
    }
    out = found->second;
    return true;
  }

  static bool store(std::map<std::string, Value>& table, const ObjectRef& object,
                    const std::string& name, const Value& value, Status& status) {
    (void)status;
    table[key(object.type, name)] = value;
    return true;
  }
};

PexObject script(std::string name, std::string parent, std::vector<PexVariable> variables,
                 std::vector<PexState> states) {
  PexObject object;
  object.name = std::move(name);
  object.parentClassName = std::move(parent);
  object.autoStateName = "";
  object.variables = std::move(variables);
  object.states = std::move(states);
  return object;
}

PexVariable variable(std::string name, std::string type) {
  PexVariable entry;
  entry.name = std::move(name);
  entry.type = std::move(type);
  return entry;
}

Result runScript(TestHost& host, const std::string& name, const std::string& function,
                 std::vector<Value> args = {}, Limits limits = Limits{}) {
  Interpreter interpreter(host, limits);
  return interpreter.runFunction(name, function, args);
}

Result runSelf(TestHost& host, const Value& self, const std::string& function,
               std::vector<Value> args = {}, Limits limits = Limits{}) {
  Interpreter interpreter(host, limits);
  return interpreter.runFunction(self, function, args);
}

PexState arithmeticState() {
  PexFunction wrap =
      method("Wrap", "Int", {}, {typed("::temp0", "Int"), typed("::temp1", "Int")},
             {ins("iadd", {id("::temp0"), number(2147483647), number(1)}),
              ins("isub", {id("::temp1"), number(7), number(9)}),
              ins("iadd", {id("::temp0"), id("::temp0"), id("::temp1")}),
              ins("return", {id("::temp0")})});
  PexFunction division =
      method("Division", "Int", {}, {typed("::temp0", "Int"), typed("::temp1", "Int")},
             {ins("idiv", {id("::temp0"), number(-7), number(2)}),
              ins("imod", {id("::temp1"), number(-7), number(2)}),
              ins("imul", {id("::temp1"), id("::temp1"), number(1000)}),
              ins("iadd", {id("::temp0"), id("::temp0"), id("::temp1")}),
              ins("return", {id("::temp0")})});
  return state("", {wrap, division});
}

PexState textState() {
  PexFunction concat =
      method("Concat", "String", {}, {typed("::temp0", "String")},
             {ins("strcat", {id("::temp0"), literal("Total: "), number(42)}),
              ins("return", {id("::temp0")})});
  PexFunction compare =
      method("Compare", "Bool", {}, {typed("::temp0", "Bool")},
             {ins("cmp_eq", {id("::temp0"), literal("Actor"), literal("aCtOr")}),
              ins("return", {id("::temp0")})});
  return state("", {concat, compare});
}

PexState castState() {
  PexFunction convert =
      method("Convert", "Int", {}, {typed("::temp0", "Float"), typed("::temp1", "Int")},
             {ins("cast", {id("::temp0"), number(7)}),
              ins("cast", {id("::temp1"), id("::temp0")}),
              ins("return", {id("::temp1")})});
  PexFunction truth =
      method("Truth", "Bool", {}, {typed("::temp0", "Bool"), typed("::temp1", "Bool")},
             {ins("cast", {id("::temp0"), number(3)}), ins("not", {id("::temp1"), id("::temp0")}),
              ins("not", {id("::temp0"), id("::temp1")}), ins("return", {id("::temp0")})});
  PexFunction castRef = method(
      "CastRef", "Int", {typed("ref", "Var")}, {typed("::temp0", "Bool"), typed("::temp1", "Int")},
      {ins("cast", {id("::temp0"), id("ref")}), ins("jmpf", {id("::temp0"), number(2)}),
       ins("assign", {id("::temp1"), number(1)}), ins("return", {id("::temp1")})});
  PexFunction notNone =
      method("NotNone", "Bool", {}, {typed("::temp0", "Bool")},
             {ins("jmpt", {id("::NoneVar"), number(2)}),
              ins("not", {id("::temp0"), id("::NoneVar")}), ins("return", {id("::temp0")})});
  return state("", {convert, truth, castRef, notNone});
}

PexState loopState() {
  PexFunction total = method(
      "SumTo", "Int", {typed("n", "Int")},
      {typed("::temp0", "Int"), typed("::temp1", "Int"), typed("::temp2", "Bool")},
      {ins("assign", {id("::temp0"), number(0)}), ins("assign", {id("::temp1"), number(1)}),
       ins("cmp_gt", {id("::temp2"), id("::temp1"), id("n")}),
       ins("jmpt", {id("::temp2"), number(4)}),
       ins("iadd", {id("::temp0"), id("::temp0"), id("::temp1")}),
       ins("iadd", {id("::temp1"), id("::temp1"), number(1)}),
       ins("jmp", {number(-4)}), ins("return", {id("::temp0")})});
  PexFunction spin = method("Spin", "None", {}, {}, {ins("jmp", {number(0)})});
  return state("", {total, spin});
}

PexState recursionState() {
  PexFunction factorial = method(
      "Factorial", "Int", {typed("n", "Int")},
      {typed("::temp0", "Int"), typed("::temp1", "Bool"), typed("::temp2", "Int")},
      {ins("cmp_lte", {id("::temp1"), id("n"), number(1)}),
       ins("jmpf", {id("::temp1"), number(3)}), ins("assign", {id("::temp0"), number(1)}),
       ins("return", {id("::temp0")}), ins("isub", {id("::temp2"), id("n"), number(1)}),
       ins("callstatic", {id("Script"), id("Factorial"), id("::temp0"), id("::temp2")}),
       ins("imul", {id("::temp0"), id("n"), id("::temp0")}), ins("return", {id("::temp0")})});
  return state("", {factorial});
}

PexState arrayState() {
  PexFunction build = method(
      "Build", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int")},
      {ins("array_create", {id("::temp0"), number(3)}),
       ins("array_setelement", {id("::temp0"), number(0), number(11)}),
       ins("array_setelement", {id("::temp0"), number(1), number(22)}),
       ins("array_add", {id("::temp0"), number(33), number(2)}),
       ins("array_insert", {id("::temp0"), number(44), number(1)}),
       ins("array_remove", {id("::temp0"), number(0), number(1)}),
       ins("array_removelast", {id("::temp0")}),
       ins("array_getelement", {id("::temp1"), id("::temp0"), number(0)}),
       ins("return", {id("::temp1")})});
  PexFunction find = method(
      "Find", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int"),
                          typed("::temp2", "Int")},
      {ins("array_create", {id("::temp0"), number(3)}),
       ins("array_setelement", {id("::temp0"), number(0), number(5)}),
       ins("array_setelement", {id("::temp0"), number(1), number(6)}),
       ins("array_setelement", {id("::temp0"), number(2), number(5)}),
       ins("array_findelement", {id("::temp0"), id("::temp1"), number(5), number(1)}),
       ins("array_rfindelement", {id("::temp0"), id("::temp2"), number(5), number(9)}),
       ins("imul", {id("::temp1"), id("::temp1"), number(100)}),
       ins("iadd", {id("::temp1"), id("::temp1"), id("::temp2")}),
       ins("return", {id("::temp1")})});
  PexFunction length = method("Length", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int")},
                              {ins("array_create", {id("::temp0"), number(4)}),
                               ins("array_length", {id("::temp1"), id("::temp0")}),
                               ins("return", {id("::temp1")})});
  PexFunction overrun = method(
      "Overrun", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int")},
      {ins("array_create", {id("::temp0"), number(2)}),
       ins("array_getelement", {id("::temp1"), id("::temp0"), number(5)}),
       ins("return", {id("::temp1")})});
  PexFunction tooLong = method(
      "TooLong", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int")},
      {ins("array_create", {id("::temp0"), number(2)}),
       ins("array_add", {id("::temp0"), number(1), number(400)}),
       ins("array_length", {id("::temp1"), id("::temp0")}),
       ins("return", {id("::temp1")})});
  PexFunction lastIndex = method(
      "LastIndex", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int")},
      {ins("array_create", {id("::temp0"), number(3)}),
       ins("array_setelement", {id("::temp0"), number(2), number(5)}),
       ins("array_rfindelement", {id("::temp0"), id("::temp1"), number(5), number(-1)}),
       ins("return", {id("::temp1")})});
  PexFunction nest = method(
      "Nest", "Int", {}, {typed("::temp0", "Var[]"), typed("::temp1", "Int")},
      {ins("array_create", {id("::temp0"), number(1)}),
       ins("array_add", {id("::temp0"), id("::temp0"), number(1)}),
       ins("array_length", {id("::temp1"), id("::temp0")}), ins("return", {id("::temp1")})});
  return state("", {build, find, length, overrun, tooLong, lastIndex, nest});
}

}

TEST(PapyrusVm, IntegerArithmeticWrapsAndDivisionTruncatesTowardZero) {
  TestHost host;
  host.addScript(script("Script", "", {}, {arithmeticState()}));
  const Result wrapped = runScript(host, "Script", "Wrap");
  ASSERT_EQ(wrapped.status, Status::Ok);
  EXPECT_EQ(wrapped.value.integer, 2147483646);
  const Result divided = runScript(host, "Script", "Division");
  ASSERT_EQ(divided.status, Status::Ok);
  EXPECT_EQ(divided.value.integer, -1003);
}

TEST(PapyrusVm, DivisionByZeroIsReportedAndTheFunctionYieldsNone) {
  TestHost host;
  PexFunction broken = method("Broken", "Int", {}, {typed("::temp0", "Int")},
                              {ins("idiv", {id("::temp0"), number(4), number(0)}),
                               ins("return", {id("::temp0")})});
  host.addScript(script("Script", "", {}, {state("", {broken})}));
  const Result result = runScript(host, "Script", "Broken");
  EXPECT_EQ(result.status, Status::DivisionByZero);
  EXPECT_EQ(result.value.type, ValueType::None);
  EXPECT_FALSE(fo4::papyrus::isFatal(result.status));
}

TEST(PapyrusVm, StringConcatFormatsNumbersAndEqualityIgnoresCase) {
  TestHost host;
  host.addScript(script("Script", "", {}, {textState()}));
  const Result concat = runScript(host, "Script", "Concat");
  ASSERT_EQ(concat.status, Status::Ok);
  EXPECT_EQ(concat.value.str, "Total: 42");
  const Result compare = runScript(host, "Script", "Compare");
  ASSERT_EQ(compare.status, Status::Ok);
  EXPECT_TRUE(compare.value.boolean);
  Limits limits;
  limits.maxStringLength = 8;
  const Result capped = runScript(host, "Script", "Concat", {}, limits);
  EXPECT_EQ(capped.status, Status::StringTooLong);
  EXPECT_FALSE(fo4::papyrus::isFatal(capped.status));
  EXPECT_EQ(capped.value.type, ValueType::None);
  limits.maxStringLength = 9;
  EXPECT_EQ(runScript(host, "Script", "Concat", {}, limits).status, Status::Ok);
}

TEST(PapyrusVm, ScalarCastsFollowThePapyrusTable) {
  TestHost host;
  host.addScript(script("Script", "", {}, {castState()}));
  const Result convert = runScript(host, "Script", "Convert");
  ASSERT_EQ(convert.status, Status::Ok);
  EXPECT_EQ(convert.value.integer, 7);
  const Result truth = runScript(host, "Script", "Truth");
  ASSERT_EQ(truth.status, Status::Ok);
  EXPECT_TRUE(truth.value.boolean);
  const Result nullCast = runScript(host, "Script", "CastRef", {fo4::papyrus::noneValue()});
  ASSERT_EQ(nullCast.status, Status::Ok);
  EXPECT_EQ(nullCast.value.integer, 0);
  const Result nullNot = runScript(host, "Script", "NotNone");
  ASSERT_EQ(nullNot.status, Status::Ok);
  EXPECT_TRUE(nullNot.value.boolean);
  const Result selfCast =
      runScript(host, "Script", "CastRef", {fo4::papyrus::objectValue(2, "Script", "")});
  ASSERT_EQ(selfCast.status, Status::Ok);
  EXPECT_EQ(selfCast.value.integer, 1);
  const Value items = fo4::papyrus::arrayValue(std::make_shared<fo4::papyrus::Array>());
  const Result arrayCast = runScript(host, "Script", "CastRef", {items});
  ASSERT_EQ(arrayCast.status, Status::Ok);
  EXPECT_EQ(arrayCast.value.integer, 1);
}

TEST(PapyrusVm, CastingAnArrayToAScalarIsRejected) {
  TestHost host;
  PexFunction broken = method(
      "Broken", "Int", {}, {typed("::temp0", "Int[]"), typed("::temp1", "Int")},
      {ins("array_create", {id("::temp0"), number(1)}),
       ins("cast", {id("::temp1"), id("::temp0")}), ins("return", {id("::temp1")})});
  host.addScript(script("Script", "", {}, {state("", {broken})}));
  const Result result = runScript(host, "Script", "Broken");
  EXPECT_EQ(result.status, Status::TypeMismatch);
}

TEST(PapyrusVm, LoopJumpsAreRelativeToTheCurrentInstruction) {
  TestHost host;
  host.addScript(script("Script", "", {}, {loopState()}));
  const Result result = runScript(host, "Script", "SumTo", {fo4::papyrus::integerValue(10)});
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 55);
  const Result zero = runScript(host, "Script", "SumTo", {fo4::papyrus::integerValue(0)});
  ASSERT_EQ(zero.status, Status::Ok);
  EXPECT_EQ(zero.value.integer, 0);
}

TEST(PapyrusVm, BadJumpTargetIsRejectedInsteadOfReadingPastTheFunction) {
  TestHost host;
  PexFunction broken = method("Broken", "None", {}, {}, {ins("jmp", {number(9)})});
  PexFunction toEnd = method("ToEnd", "Int", {}, {typed("::temp0", "Int")},
                             {ins("jmp", {number(2)}), ins("assign", {id("::temp0"), number(9)})});
  host.addScript(script("Script", "", {}, {state("", {broken, toEnd})}));
  const Result result = runScript(host, "Script", "Broken");
  EXPECT_EQ(result.status, Status::BadJumpTarget);
  EXPECT_TRUE(fo4::papyrus::isFatal(result.status));
  const Result atEnd = runScript(host, "Script", "ToEnd");
  EXPECT_EQ(atEnd.status, Status::Ok);
  EXPECT_EQ(atEnd.value.type, ValueType::None);
}

TEST(PapyrusVm, RecursionReturnsThroughTheCallerDestinationSlot) {
  TestHost host;
  host.addScript(script("Script", "", {}, {recursionState()}));
  const Result result = runScript(host, "Script", "Factorial", {fo4::papyrus::integerValue(5)});
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 120);
  EXPECT_EQ(result.callDepth, 5U);
}

TEST(PapyrusVm, RecursionStopsAtTheCallDepthLimit) {
  TestHost host;
  host.addScript(script("Script", "", {}, {recursionState()}));
  Limits limits;
  limits.maxCallDepth = 8;
  const Result result =
      runScript(host, "Script", "Factorial", {fo4::papyrus::integerValue(30)}, limits);
  EXPECT_EQ(result.status, Status::CallDepthExceeded);
  EXPECT_EQ(result.callDepth, 8U);
}

TEST(PapyrusVm, InstructionBudgetStopsAnEndlessLoop) {
  TestHost host;
  host.addScript(script("Script", "", {}, {loopState()}));
  Limits limits;
  limits.maxInstructions = 100;
  const Result result = runScript(host, "Script", "Spin", {}, limits);
  EXPECT_EQ(result.status, Status::InstructionBudgetExceeded);
  EXPECT_EQ(result.instructions, 100U);
}

TEST(PapyrusVm, ArrayInstructionsMutateTheSharedArrayInPlace) {
  TestHost host;
  host.addScript(script("Script", "", {}, {arrayState()}));
  const Result build = runScript(host, "Script", "Build");
  ASSERT_EQ(build.status, Status::Ok);
  EXPECT_EQ(build.value.integer, 44);
  const Result find = runScript(host, "Script", "Find");
  ASSERT_EQ(find.status, Status::Ok);
  EXPECT_EQ(find.value.integer, 202);
  const Result length = runScript(host, "Script", "Length");
  ASSERT_EQ(length.status, Status::Ok);
  EXPECT_EQ(length.value.integer, 4);
  const Result last = runScript(host, "Script", "LastIndex");
  ASSERT_EQ(last.status, Status::Ok);
  EXPECT_EQ(last.value.integer, 2);
}

TEST(PapyrusVm, ArrayOverrunAndOverlongArraysAreReported) {
  TestHost host;
  host.addScript(script("Script", "", {}, {arrayState()}));
  const Result overrun = runScript(host, "Script", "Overrun");
  EXPECT_EQ(overrun.status, Status::IndexOutOfRange);
  EXPECT_EQ(overrun.value.type, ValueType::None);
  Limits limits;
  limits.maxArrayLength = 128;
  const Result tooLong = runScript(host, "Script", "TooLong", {}, limits);
  EXPECT_EQ(tooLong.status, Status::ArrayTooLong);
  const Result nested = runScript(host, "Script", "Nest");
  EXPECT_EQ(nested.status, Status::TypeMismatch);
}

TEST(PapyrusVm, MethodsResolveThroughTheParentChainAndCallParentRestartsThere) {
  TestHost host;
  const PexFunction parentDescribe =
      method("Describe", "String", {}, {typed("::temp0", "String")},
             {ins("assign", {id("::temp0"), literal("Parent")}), ins("return", {id("::temp0")})});
  const PexFunction parentWho =
      method("Who", "String", {}, {typed("::temp0", "String")},
             {ins("assign", {id("::temp0"), literal("ParentWho")}), ins("return", {id("::temp0")})});
  const PexFunction childDescribe =
      method("Describe", "String", {}, {typed("::temp0", "String")},
             {ins("assign", {id("::temp0"), literal("Child")}), ins("return", {id("::temp0")})});
  const PexFunction callParent =
      method("CallParent", "String", {}, {typed("::temp0", "String")},
             {ins("callparent", {id("Describe"), id("::temp0")}), ins("return", {id("::temp0")})});
  const PexFunction parentLevel =
      method("Level", "Int", {}, {typed("::temp0", "Int")},
             {ins("assign", {id("::temp0"), id("Count")}), ins("return", {id("::temp0")})});
  PexVariable count = variable("Count", "Int");
  count.defaultValue = number(7);
  host.addScript(
      script("Parent", "", {count}, {state("", {parentDescribe, parentWho, parentLevel})}));
  host.addScript(script("Child", "Parent", {}, {state("", {childDescribe, callParent})}));

  const Value self = fo4::papyrus::objectValue(7, "Child", "");
  const Result own = runSelf(host, self, "Describe");
  ASSERT_EQ(own.status, Status::Ok);
  EXPECT_EQ(own.value.str, "Child");
  const Result inherited = runSelf(host, self, "Who");
  ASSERT_EQ(inherited.status, Status::Ok);
  EXPECT_EQ(inherited.value.str, "ParentWho");
  const Result viaParent = runSelf(host, self, "CallParent");
  ASSERT_EQ(viaParent.status, Status::Ok);
  EXPECT_EQ(viaParent.value.str, "Parent");
  const Result level = runSelf(host, self, "Level");
  ASSERT_EQ(level.status, Status::Ok);
  EXPECT_EQ(level.value.integer, 7);
}

TEST(PapyrusVm, NamedStatesShadowTheEmptyStateForTheSameFunctionName) {
  TestHost host;
  const PexFunction idle = method("Ping", "Int", {}, {typed("::temp0", "Int")},
                                  {ins("assign", {id("::temp0"), number(1)}),
                                   ins("return", {id("::temp0")})});
  const PexFunction running = method("Ping", "Int", {}, {typed("::temp0", "Int")},
                                     {ins("assign", {id("::temp0"), number(2)}),
                                      ins("return", {id("::temp0")})});
  host.addScript(script("Stated", "", {}, {state("", {idle}), state("Running", {running})}));

  const Result emptyState = runSelf(host, fo4::papyrus::objectValue(3, "Stated", ""), "Ping");
  ASSERT_EQ(emptyState.status, Status::Ok);
  EXPECT_EQ(emptyState.value.integer, 1);
  const Result namedState =
      runSelf(host, fo4::papyrus::objectValue(3, "Stated", "Running"), "Ping");
  ASSERT_EQ(namedState.status, Status::Ok);
  EXPECT_EQ(namedState.value.integer, 2);
}

TEST(PapyrusVm, NativeFunctionsResolveThroughTheHostRegistry) {
  TestHost host;
  const PexFunction boost = [] {
    PexFunction function = method("Boost", "Int", {typed("amount", "Int")},
                                  {typed("::temp0", "Int")}, {});
    function.isNative = true;
    return function;
  }();
  const PexFunction caller = method(
      "Caller", "Int", {}, {typed("::temp0", "Int")},
      {ins("callstatic", {id("Natives"), id("Boost"), id("::temp0"), number(5)}),
       ins("iadd", {id("::temp0"), id("::temp0"), number(1)}), ins("return", {id("::temp0")})});
  const PexFunction missing = method(
      "Missing", "Int", {}, {typed("::temp0", "Int")},
      {ins("callstatic", {id("Nowhere"), id("Gone"), id("::temp0")}), ins("return", {id("::temp0")})});
  const PexFunction blast = method(
      "Blast", "Int", {}, {typed("::temp0", "Int")},
      {ins("callstatic", {id("Natives"), id("Explode"), id("::temp0")}),
       ins("return", {id("::temp0")})});
  host.addScript(script("Natives", "", {}, {state("", {boost, caller, missing, blast})}));
  host.addNative("Natives", "Explode", [](const std::vector<Value>&, Value&, Status&) -> bool {
    throw std::runtime_error("host fault");
  });
  host.addNative("Natives", "Boost", [](const std::vector<Value>& args, Value& out, Status& status) {
    if (args.size() != 1 || args[0].type != ValueType::Integer) {
      status = Status::TypeMismatch;
      return false;
    }
    out = fo4::papyrus::floatValue(static_cast<float>(args[0].integer) * 10.0F);
    return true;
  });

  const Result result = runScript(host, "Natives", "Caller");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 51);
  const Result absent = runScript(host, "Natives", "Missing");
  EXPECT_EQ(absent.status, Status::FunctionNotFound);
  const Result direct = runScript(host, "Natives", "Boost", {fo4::papyrus::integerValue(5)});
  ASSERT_EQ(direct.status, Status::Ok);
  EXPECT_EQ(direct.value.integer, 50);
  const Result exploded = runScript(host, "Natives", "Blast");
  EXPECT_EQ(exploded.status, Status::HostError);
}

TEST(PapyrusVm, PropertiesUseTheHostAndAutoVariablesUseTheBackingVariable) {
  TestHost host;
  PexProperty count;
  count.name = "Count";
  count.type = "Int";
  count.flags = 0x07;
  count.autoVarName = "countVar";
  PexFunction read = method("Read", "Int", {}, {typed("::temp0", "Int")},
                            {ins("propget", {id("Count"), id("self"), id("::temp0")}),
                             ins("return", {id("::temp0")})});
  PexFunction write = method("Write", "Int", {typed("amount", "Int")}, {typed("::temp0", "Int")},
                             {ins("propset", {id("Count"), id("self"), id("amount")}),
                              ins("propget", {id("Count"), id("self"), id("::temp0")}),
                              ins("return", {id("::temp0")})});
  PexObject object = script("Holder", "", {variable("countVar", "Int")}, {state("", {read, write})});
  object.properties.push_back(count);
  host.addScript(std::move(object));
  host.variables["holder.countvar"] = fo4::papyrus::integerValue(4);

  const Result before = runScript(host, "Holder", "Read");
  ASSERT_EQ(before.status, Status::Ok);
  EXPECT_EQ(before.value.integer, 4);
  const Result after = runScript(host, "Holder", "Write", {fo4::papyrus::integerValue(9)});
  ASSERT_EQ(after.status, Status::Ok);
  EXPECT_EQ(after.value.integer, 9);
  EXPECT_EQ(host.variables["holder.countvar"].integer, 9);
}

TEST(PapyrusVm, PropertyHandlersRunEvenWhenTheirFunctionNamesAreEmpty) {
  TestHost host;
  PexProperty count;
  count.name = "Count";
  count.flags = 0x03;
  count.readHandler = method("", "Int", {}, {}, {ins("return", {number(42)})});
  count.writeHandler =
      method("", "None", {typed("value", "Int")}, {}, {ins("assign", {id("Last"), id("value")})});
  const PexFunction probe = method(
      "Probe", "Int", {}, {typed("::temp0", "Int")},
      {ins("propset", {id("Count"), id("self"), number(9)}),
       ins("propget", {id("Count"), id("self"), id("::temp0")}), ins("return", {id("::temp0")})});
  PexObject object = script("Holder", "", {variable("Last", "Int")}, {state("", {probe})});
  object.properties.push_back(count);
  host.addScript(std::move(object));
  const Result result = runSelf(host, fo4::papyrus::objectValue(5, "Holder", ""), "Probe");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 42);
  EXPECT_EQ(host.variables["holder.last"].integer, 9);
}

TEST(PapyrusVm, IntegerOrderingIsExactBeyondFloatPrecision) {
  TestHost host;
  const PexFunction order = method(
      "Order", "Bool", {}, {typed("::temp0", "Bool")},
      {ins("cmp_lt", {id("::temp0"), number(16777216), number(16777217)}),
       ins("return", {id("::temp0")})});
  host.addScript(script("Script", "", {}, {state("", {order})}));
  const Result result = runScript(host, "Script", "Order");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_TRUE(result.value.boolean);
}

TEST(PapyrusVm, AFailedReturnWriteStopsTheRunInsteadOfContinuingTheCaller) {
  TestHost host;
  const PexFunction make = method("Make", "Int", {}, {}, {ins("return", {number(5)})});
  const PexFunction caller = method(
      "Caller", "Int", {}, {typed("::temp0", "Thing"), typed("::temp1", "Int")},
      {ins("callstatic", {id("Script"), id("Make"), id("::temp0")}),
       ins("assign", {id("::temp1"), number(1)}), ins("return", {id("::temp1")})});
  host.addScript(script("Script", "", {}, {state("", {make, caller})}));
  const Result result = runScript(host, "Script", "Caller");
  EXPECT_EQ(result.status, Status::TypeMismatch);
  EXPECT_EQ(result.instructions, 2U);
}

TEST(PapyrusVm, WritesToNoneVarAreDiscardedAndReadBackAsNone) {
  TestHost host;
  const PexFunction sink = method(
      "Sink", "Int", {}, {typed("::temp0", "Int")},
      {ins("assign", {id("::NoneVar"), number(5)}),
       ins("assign", {id("::temp0"), id("::NoneVar")}), ins("return", {id("::temp0")})});
  host.addScript(script("Script", "", {}, {state("", {sink})}));
  const Result result = runScript(host, "Script", "Sink");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.type, ValueType::None);
}

TEST(PapyrusVm, UnknownIdentifierAndArgumentCountAreRejected) {
  TestHost host;
  const PexFunction missingLocal = method("Missing", "Int", {}, {typed("::temp0", "Int")},
                                          {ins("return", {id("notDeclared")})});
  const PexFunction twoParams = method("Two", "Int", {typed("a", "Int"), typed("b", "Int")},
                                       {typed("::temp0", "Int")},
                                       {ins("iadd", {id("::temp0"), id("a"), id("b")}),
                                        ins("return", {id("::temp0")})});
  host.addScript(script("Script", "", {}, {state("", {missingLocal, twoParams})}));
  const Result unknown = runScript(host, "Script", "Missing");
  EXPECT_EQ(unknown.status, Status::UnknownIdentifier);
  const Result arity = runScript(host, "Script", "Two", {fo4::papyrus::integerValue(1)});
  EXPECT_EQ(arity.status, Status::ArgumentCountMismatch);
}

TEST(PapyrusVm, StructCreateRefusesAnUntypedDestinationAndUnknownOpcodesAreRejected) {
  TestHost host;
  const PexFunction structs = method("Structs", "None", {}, {}, {ins("struct_create", {id("::temp0")})});
  const PexFunction unknown = method("Unknown", "None", {}, {}, {[] {
                                     PexInstruction instruction;
                                     instruction.opCode = 250;
                                     instruction.mnemonic = "not_a_real_opcode";
                                     return instruction;
                                   }()});
  const PexFunction shortArgs = method("Short", "None", {}, {}, {[] {
                                     PexInstruction instruction;
                                     instruction.opCode = opcodeOf("iadd");
                                     instruction.mnemonic = "iadd";
                                     instruction.args = {id("::temp0")};
                                     return instruction;
                                   }()});
  PexInstruction swapped = ins("iadd", {id("::temp0"), number(4), number(1)});
  swapped.mnemonic = "isub";
  const PexFunction mismatch = method("Mismatch", "None", {}, {typed("::temp0", "Int")}, {swapped});
  host.addScript(script("Script", "", {}, {state("", {structs, unknown, shortArgs, mismatch})}));
  EXPECT_EQ(runScript(host, "Script", "Structs").status, Status::UnknownIdentifier);
  EXPECT_EQ(runScript(host, "Script", "Unknown").status, Status::UnknownOpcode);
  EXPECT_EQ(runScript(host, "Script", "Short").status, Status::StackUnderflow);
  EXPECT_EQ(runScript(host, "Script", "Mismatch").status, Status::UnknownOpcode);
}

TEST(PapyrusVm, AFunctionMissingFromTheScriptIsReported) {
  TestHost host;
  host.addScript(script("Script", "", {}, {loopState()}));
  EXPECT_EQ(runScript(host, "Script", "Absent").status, Status::FunctionNotFound);
  EXPECT_EQ(runScript(host, "NoSuchScript", "SumTo").status, Status::FunctionNotFound);
  EXPECT_EQ(runSelf(host, fo4::papyrus::noneValue(), "SumTo").status, Status::NullObject);
}

TEST(PapyrusVm, CodeSurvivesThePexWriterAndReaderRoundTrip) {
  TestHost host;
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Synthetic.psc";
  pex.userName = "garden";
  pex.computerName = "garden-box";
  pex.objects.push_back(script("Script", "", {}, {loopState()}));
  pex.rebuildStringTable();
  const PexFile decoded = PexFile::fromBytes(pex.toBytes());
  ASSERT_EQ(decoded.objects.size(), 1U);
  host.addScript(decoded.objects[0]);
  const Result result = runScript(host, "Script", "SumTo", {fo4::papyrus::integerValue(4)});
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 10);
}
