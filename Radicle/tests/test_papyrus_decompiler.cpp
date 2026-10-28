#include "fo4/papyrus/decompiler.hpp"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using fo4::papyrus::Decompiler;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

namespace {

PexValue identifier(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexValue integer(std::int32_t number) {
  PexValue value;
  value.type = PexValueType::Integer;
  value.int_ = number;
  return value;
}

PexInstruction step(std::string mnemonic, std::vector<PexValue> args, int fixedArgCount = 0) {
  PexInstruction instruction;
  instruction.mnemonic = std::move(mnemonic);
  instruction.args = std::move(args);
  instruction.fixedArgCount = fixedArgCount;
  return instruction;
}

PexFunction makeFunction(std::string name, std::vector<PexTypedName> params, std::vector<PexTypedName> locals,
                     std::vector<PexInstruction> instructions) {
  PexFunction result;
  result.name = std::move(name);
  result.params = std::move(params);
  result.locals = std::move(locals);
  result.instructions = std::move(instructions);
  return result;
}

PexFile scriptWith(PexFunction fn, std::string objectName = "Probe") {
  PexObject object;
  object.name = objectName;
  object.parentClassName = "ScriptObject";
  PexState state;
  state.functions.push_back(std::move(fn));
  object.states.push_back(std::move(state));
  PexFile file;
  file.sourceFileName = objectName + ".psc";
  file.userName = "test";
  file.computerName = "native";
  file.objects.push_back(std::move(object));
  return file;
}

}  // namespace

TEST(PapyrusDecompiler, ZeroAssignmentToLocalIsHoistedToItsDeclaration) {
  const auto fn = makeFunction("Go", {}, {{"i", "int"}},
                           {step("assign", {identifier("i"), integer(0)}), step("return", {PexValue{}})});
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function Go()\n"
            "\tint i\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, CallWhoseResultIsNeverReadStillEmitsTheCall) {
  const auto fn = makeFunction("Go", {}, {},
                           {step("callmethod", {identifier("Ping"), identifier("self"), identifier("::temp0")}, 3),
                            step("return", {PexValue{}})});
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function Go()\n"
            "\tPing()\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, TwoJumpsOnOneConditionFoldIntoOneCompoundIf) {
  const auto fn = makeFunction("Go", {{"a", "bool"}}, {},
                           {step("jmpf", {identifier("a"), integer(1)}),
                            step("jmpf", {identifier("a"), integer(2)}),
                            step("return", {PexValue{}}),
                            step("return", {PexValue{}})});
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function Go(bool a)\n"
            "\tIf ((a && a))\n"
            "\t\tReturn\n"
            "\tEndIf\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, SameTypeCastIsEmittedAsPlainAssignmentWithoutAsCast) {
  const auto fn = makeFunction("Go", {{"a", "bool"}}, {{"b", "bool"}},
                           {step("cast", {identifier("b"), identifier("a")}), step("return", {PexValue{}})});
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function Go(bool a)\n"
            "\tbool b = a\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, DisassemblyListsObjectFunctionLocalsAndInstructions) {
  const auto fn = makeFunction("Go", {}, {{"i", "int"}},
                           {step("assign", {identifier("i"), integer(1)}), step("return", {PexValue{}})});
  const std::string text = Decompiler::disassemble(scriptWith(fn, "Asm"));
  EXPECT_NE(text.find("; Disassembly of Asm.psc\n"), std::string::npos);
  EXPECT_NE(text.find(".object Asm extends ScriptObject\n"), std::string::npos);
  EXPECT_NE(text.find(".function Go -> None\n"), std::string::npos);
  EXPECT_NE(text.find(".locals int i\n"), std::string::npos);
  EXPECT_NE(text.find("assign"), std::string::npos);
}

TEST(PapyrusDecompiler, NativeFunctionIsEmittedAsHeaderWithNoBodyOrTerminator) {
  PexFunction fn;
  fn.name = "Ping";
  fn.isNative = true;
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function Ping() Native\n");
}

TEST(PapyrusDecompiler, RemoteEventNameIsRebuiltFromTheSenderPrefix) {
  const auto fn = makeFunction("::remote_Sender_OnSomething", {{"akSender", "Sender"}}, {},
                           {step("return", {PexValue{}})});
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Event Sender.OnSomething(Sender akSender)\n"
            "\tReturn\n"
            "EndEvent\n");
}

TEST(PapyrusDecompiler, ObjectNameThatIsNotASafeFileNameIsRefused) {
  const auto fn = makeFunction("Go", {}, {}, {step("return", {PexValue{}})});
  EXPECT_THROW(Decompiler::toSource(scriptWith(fn, "Bad/Name")), std::runtime_error);
}
