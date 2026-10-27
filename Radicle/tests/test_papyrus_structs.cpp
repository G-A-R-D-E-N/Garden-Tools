#include "fo4/papyrus/interpreter.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::Host;
using fo4::papyrus::Interpreter;
using fo4::papyrus::ObjectRef;
using fo4::papyrus::Result;
using fo4::papyrus::Status;
using fo4::papyrus::Struct;
using fo4::papyrus::Value;
using fo4::papyrus::ValueType;
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

PexStructMember member(std::string name, std::string type) {
  PexStructMember entry;
  entry.name = std::move(name);
  entry.type = std::move(type);
  return entry;
}

PexStruct structure(std::string name, std::vector<PexStructMember> members) {
  PexStruct entry;
  entry.name = std::move(name);
  entry.members = std::move(members);
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

  bool gate(std::string_view gateKey) override {
    if (gateKey == "vm.states.gotostate" && !allowGotoState) return false;
    return true;
  }

  void addScript(PexObject object) { scripts[fo4::papyrus::lowerAscii(object.name)] = std::move(object); }

  void addNative(const std::string& script, const std::string& function, Native native) {
    natives[key(script, function)] = std::move(native);
  }

  std::map<std::string, PexObject> scripts;
  std::map<std::string, Native> natives;
  std::map<std::string, Value> properties;
  std::map<std::string, Value> variables;
  bool allowGotoState{true};

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

PexObject script(std::string name, std::vector<PexStruct> structs, std::vector<PexVariable> variables,
                 std::vector<PexState> states) {
  PexObject object;
  object.name = std::move(name);
  object.structs = std::move(structs);
  object.variables = std::move(variables);
  object.states = std::move(states);
  return object;
}

Result run(TestHost& host, const std::string& name, const std::string& function,
           std::vector<Value> args = {}) {
  Interpreter interpreter(host);
  return interpreter.runFunction(name, function, args);
}

PexObject pointScript() {
  return script("PointScript",
                {structure("Point", {member("X", "Int"), member("Y", "Int"), member("Label", "String")})},
                {},
                {state("", {method("Make", "Point", {}, {typed("result", "Point")},
                                    {ins("struct_create", {id("result")}),
                                     ins("struct_set", {id("result"), literal("X"), number(3)}),
                                     ins("struct_set", {id("result"), literal("Y"), number(4)}),
                                     ins("struct_set", {id("result"), literal("Label"), literal("here")}),
                                     ins("return", {id("result")})}),
                           method("ReadX", "Int", {typed("p", "Point")}, {},
                                  {ins("struct_get", {id("out"), id("p"), literal("X")}),
                                   ins("return", {id("out")})}),
                           method("WriteAndRead", "Int", {}, {typed("p", "Point")},
                                  {ins("struct_create", {id("p")}),
                                   ins("struct_set", {id("p"), literal("X"), number(9)}),
                                   ins("struct_get", {id("out"), id("p"), literal("x")}),
                                   ins("return", {id("out")})})})});
}

TEST(PapyrusStructs, CreateFillsEveryMemberDefaultAndSetWritesIt) {
  TestHost host;
  host.addScript(pointScript());
  const Result made = run(host, "PointScript", "Make");
  ASSERT_EQ(made.status, Status::Ok);
  ASSERT_EQ(made.value.type, ValueType::Struct);
  const Struct& point = *made.value.structure;
  ASSERT_EQ(point.members.size(), 3u);
  EXPECT_EQ(point.members.at("x").type, ValueType::Integer);
  EXPECT_EQ(point.members.at("x").integer, 3);
  EXPECT_EQ(point.members.at("y").integer, 4);
  EXPECT_EQ(point.members.at("label").str, "here");
}

TEST(PapyrusStructs, StructGetReadsACaseInsensitiveMemberName) {
  TestHost host;
  host.addScript(pointScript());
  Value argument;
  argument.type = ValueType::Struct;
  argument.structure = std::make_shared<Struct>();
  argument.structure->typeName = "Point";
  argument.structure->members["x"] = fo4::papyrus::integerValue(11);
  argument.structure->members["y"] = fo4::papyrus::integerValue(12);
  argument.structure->members["label"] = fo4::papyrus::stringValue("p");
  const Result result = run(host, "PointScript", "ReadX", {argument});
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 11);
}

TEST(PapyrusStructs, StructSetEnforcesTheDeclaredMemberType) {
  TestHost host;
  host.addScript(pointScript());
  const Result result = run(host, "PointScript", "WriteAndRead");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 9);

  PexObject bad = pointScript();
  bad.states[0].functions[2].instructions[1] = ins("struct_set", {id("p"), literal("X"), id("p")});
  host.addScript(std::move(bad));
  EXPECT_EQ(run(host, "PointScript", "WriteAndRead").status, Status::TypeMismatch);
}

TEST(PapyrusStructs, StructGetRejectsAnUnknownMember) {
  TestHost host;
  PexObject object = pointScript();
  object.states[0].functions[2].instructions[1] = ins("struct_get", {id("out"), id("p"), literal("Z")});
  host.addScript(std::move(object));
  EXPECT_EQ(run(host, "PointScript", "WriteAndRead").status, Status::UnknownIdentifier);
}

TEST(PapyrusStructs, StructCreateRejectsATypeThatIsNotAStruct) {
  TestHost host;
  host.addScript(script("Plain", {}, {},
                        {state("", {method("Run", "None", {}, {typed("p", "Plain")},
                                          {ins("struct_create", {id("p")}), ins("return", {id("::NoneVar")})})})}));
  EXPECT_EQ(run(host, "Plain", "Run").status, Status::UnknownIdentifier);
}

TEST(PapyrusStructs, StructsAreReferenceValuesSharedThroughAssignment) {
  TestHost host;
  host.addScript(script("Share", {structure("Point", {member("X", "Int")})}, {},
                         {state("", {method("Run", "Int", {}, {typed("a", "Point"), typed("b", "Point")},
                                           {ins("struct_create", {id("a")}),
                                            ins("assign", {id("b"), id("a")}),
                                            ins("struct_set", {id("a"), literal("X"), number(21)}),
                                            ins("struct_get", {id("out"), id("b"), literal("X")}),
                                            ins("return", {id("out")})})})}));
  const Result result = run(host, "Share", "Run");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 21);
}

TEST(PapyrusStructs, NestedStructMembersAreCreatedAndReadable) {
  TestHost host;
  host.addScript(script("Nested",
                        {structure("Inner", {member("Value", "Int")}),
                         structure("Outer", {member("Child", "Inner"), member("Name", "String")})},
                        {},
                        {state("", {method("Run", "Int", {}, {typed("outer", "Outer"), typed("child", "Inner")},
                                          {ins("struct_create", {id("outer")}),
                                           ins("struct_get", {id("child"), id("outer"), literal("Child")}),
                                           ins("struct_set", {id("child"), literal("Value"), number(5)}),
                                           ins("struct_get", {id("out"), id("outer"), literal("Child")}),
                                           ins("struct_get", {id("out"), id("out"), literal("Value")}),
                                           ins("return", {id("out")})})})}));
  const Result result = run(host, "Nested", "Run");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 5);
}

TEST(PapyrusStructs, StructArrayFindStructReturnsForwardAndReverseIndexes) {
  TestHost host;
  host.addScript(
      script("ArrayStructs", {structure("Entry", {member("Id", "Int")})}, {},
             {state("", {method("Forward", "Int", {}, {typed("items", "Entry[]"), typed("entry", "Entry")},
                               {ins("array_create", {id("items"), number(3)}),
                                ins("struct_create", {id("entry")}),
                                ins("struct_set", {id("entry"), literal("Id"), number(2)}),
                                ins("array_setelement", {id("items"), number(0), id("entry")}),
                                ins("struct_create", {id("entry")}),
                                ins("struct_set", {id("entry"), literal("Id"), number(7)}),
                                ins("array_setelement", {id("items"), number(1), id("entry")}),
                                ins("struct_create", {id("entry")}),
                                ins("struct_set", {id("entry"), literal("Id"), number(9)}),
                                ins("array_setelement", {id("items"), number(2), id("entry")}),
                                ins("array_findstruct",
                                    {id("items"), id("out"), literal("Id"), number(9), number(0)}),
                                ins("return", {id("out")})}),
                          method("Reverse", "Int", {}, {typed("items", "Entry[]"), typed("entry", "Entry")},
                                 {ins("array_create", {id("items"), number(3)}),
                                  ins("struct_create", {id("entry")}),
                                  ins("struct_set", {id("entry"), literal("Id"), number(2)}),
                                  ins("array_setelement", {id("items"), number(0), id("entry")}),
                                  ins("struct_create", {id("entry")}),
                                  ins("struct_set", {id("entry"), literal("Id"), number(7)}),
                                  ins("array_setelement", {id("items"), number(1), id("entry")}),
                                  ins("struct_create", {id("entry")}),
                                  ins("struct_set", {id("entry"), literal("Id"), number(7)}),
                                  ins("array_setelement", {id("items"), number(2), id("entry")}),
                                  ins("array_rfindstruct",
                                      {id("items"), id("out"), literal("Id"), number(7), number(-1)}),
                                  ins("return", {id("out")})}),
                          method("Missing", "Int", {}, {typed("items", "Entry[]")},
                                 {ins("array_create", {id("items"), number(1)}),
                                  ins("array_findstruct",
                                      {id("items"), id("out"), literal("Id"), number(1), number(0)}),
                                  ins("return", {id("out")})})})}));
  EXPECT_EQ(run(host, "ArrayStructs", "Forward").value.integer, 2);
  EXPECT_EQ(run(host, "ArrayStructs", "Reverse").value.integer, 2);
  EXPECT_EQ(run(host, "ArrayStructs", "Missing").value.integer, -1);
}

TEST(PapyrusStructs, StructReturnValueCoercesAgainstTheDeclaredReturnType) {
  TestHost host;
  host.addScript(pointScript());
  const Result made = run(host, "PointScript", "Make");
  ASSERT_EQ(made.status, Status::Ok);
  const Result read = run(host, "PointScript", "ReadX", {made.value});
  ASSERT_EQ(read.status, Status::Ok);
  EXPECT_EQ(read.value.integer, 3);
}

TEST(PapyrusStructs, GotoStateChangesTheReceiverStateForTheNextCall) {
  TestHost host;
  host.addScript(script(
      "StateScript", {}, {},
      {state("", {method("Go", "Int", {}, {},
                         {ins("callmethod", {literal("GotoState"), id("self"), id("::nonevar"),
                                             literal("Alert")}),
                          ins("callmethod", {literal("OnAlert"), id("self"), id("result")}),
                          ins("return", {id("result")})})}),
       state("Alert", {method("OnAlert", "Int", {}, {}, {ins("return", {number(42)})})})}));
  const Result result = run(host, "StateScript", "Go");
  ASSERT_EQ(result.status, Status::Ok);
  EXPECT_EQ(result.value.integer, 42);
}

TEST(PapyrusStructs, GotoStateHonoursTheHostGate) {
  TestHost host;
  host.allowGotoState = false;
  host.addScript(script("Gated", {}, {},
                        {state("", {method("Go", "None", {}, {},
                                          {ins("callmethod", {literal("GotoState"), id("self"),
                                                              id("::nonevar"), literal("Alert")}),
                                           ins("return", {id("::NoneVar")})})}),
                         state("Alert", {})}));
  EXPECT_EQ(run(host, "Gated", "Go").status, Status::GateRefused);
}

}  // namespace
