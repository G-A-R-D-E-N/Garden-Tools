#include "fo4/papyrus/decompiler.hpp"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include "papyrus_pex_comparer.hpp"

#include <fo4/papyrus/code_generator.hpp>
#include <fo4/papyrus/declaration_check.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type_checker.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using fo4::papyrus::CodeGenerator;
using fo4::papyrus::CodeGenOptions;
using fo4::papyrus::Decompiler;
using fo4::papyrus::Diagnostic;
using fo4::papyrus::Parser;
using fo4::papyrus::Resolver;
using fo4::papyrus::Script;
using fo4::papyrus::ScriptIndex;
using fo4::papyrus::Severity;
using fo4::papyrus::TypeChecker;
using fo4::papyrus::declarationCheck;
using fo4::papyrus::test::firstDifference;
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

std::string uniqueRoot() {
  return (std::filesystem::temp_directory_path() / "fo4re-decompile-" /
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
      .string();
}

struct DecompileFixture {
  std::string root = uniqueRoot();
  ScriptIndex index;

  ~DecompileFixture() {
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }

  void write(std::string_view scriptName, std::string_view source) {
    std::string relative(scriptName);
    std::replace(relative.begin(), relative.end(), ':', '/');
    const auto path = std::filesystem::path(root) / (relative + ".psc");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << source;
  }

  void writeWithScriptObject() { write("ScriptObject", "Scriptname ScriptObject Native Hidden\n"); }

  std::unique_ptr<PexFile> compile(std::string_view scriptName) {
    index.addRoot(root);
    const Script* script = index.resolve(scriptName);
    if (script == nullptr) throw std::runtime_error("script not resolved: " + std::string(scriptName));
    if (script->hasErrors()) return nullptr;

    for (const auto& d : declarationCheck(*script)) {
      if (d.severity == Severity::Error) return nullptr;
    }

    const auto resolution = Resolver(index).resolve(script);
    TypeChecker checker(index);
    const auto checked = checker.check(resolution);
    for (const auto& d : checked) {
      if (d.severity == Severity::Error) return nullptr;
    }

    CodeGenOptions options;
    options.sourceFileName = std::string(scriptName) + ".psc";
    options.userName = "test";
    options.computerName = "native";
    options.emitDebugInfo = true;
    options.emitDebugOnlyCode = true;
    options.emitBetaOnlyCode = true;

    CodeGenerator generator(index);
    std::vector<Diagnostic> diagnostics;
    auto* pex = generator.generate(*script, resolution, options, diagnostics);
    if (!pex) return nullptr;
    return std::unique_ptr<PexFile>(pex);
  }

  void roundTrip(std::string_view scriptName) {
    auto original = compile(scriptName);
    ASSERT_TRUE(original != nullptr);
    ASSERT_TRUE(original->objects.size() == 1);

    const std::string text = Decompiler::toSource(*original);
    SCOPED_TRACE("decompiled source:\n" + text);

    std::string relative(scriptName);
    std::replace(relative.begin(), relative.end(), ':', '/');
    const std::string name = relative.substr(relative.find_last_of('/') + 1);

    const auto reparsed = Parser::parse(text, name + ".psc");
    ASSERT_FALSE(reparsed.hasErrors());

    DecompileFixture f2;
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
      if (error || !entry.is_regular_file()) continue;
      const auto target = std::filesystem::path(f2.root) / std::filesystem::relative(entry.path(), root);
      std::filesystem::create_directories(target.parent_path(), error);
      std::filesystem::copy_file(entry.path(), target,
                                 std::filesystem::copy_options::overwrite_existing, error);
    }
    f2.write(scriptName, text);
    auto second = f2.compile(scriptName);
    ASSERT_TRUE(second != nullptr);
    ASSERT_TRUE(second->objects.size() == 1);

    const std::string difference = firstDifference(original->objects[0], second->objects[0]);
    EXPECT_TRUE(difference.empty()) << difference;
  }
};

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

TEST(PapyrusDecompiler, SimpleFunctionRoundTrips) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Simple",
          "Scriptname Simple\n"
          "int Function Go()\n"
          "    int i = 5\n"
          "    i += 1\n"
          "    Return i\n"
          "EndFunction\n");
  f.roundTrip("Simple");
}

TEST(PapyrusDecompiler, ArithmeticAndStringConcatRoundTrip) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Arith",
          "Scriptname Arith\n"
          "Function Go(int a, float b)\n"
          "    int c = (a * 2) + 1\n"
          "    float d = b / 2.0\n"
          "    string s = \"x\" + \"y\"\n"
          "    bool eq = a == 3\n"
          "EndFunction\n");
  f.roundTrip("Arith");
}

TEST(PapyrusDecompiler, IfElseChainRoundTrips) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Cond",
          "Scriptname Cond\n"
          "Function Go(int a)\n"
          "    if a > 2\n"
          "        a = 1\n"
          "    else\n"
          "        a = 2\n"
          "    endIf\n"
          "EndFunction\n");
  f.roundTrip("Cond");
}

TEST(PapyrusDecompiler, WhileLoopRoundTrips) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Loop",
          "Scriptname Loop\n"
          "Function Go()\n"
          "    int i = 0\n"
          "    while i < 5\n"
          "        i += 1\n"
          "    endWhile\n"
          "EndFunction\n");
  f.roundTrip("Loop");
}

TEST(PapyrusDecompiler, AutoPropertyRoundTrips) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Props",
          "Scriptname Props\n"
          "int Property Count = 3 Auto\n"
          "Function Go()\n"
          "    Count = 7\n"
          "EndFunction\n");
  f.roundTrip("Props");
}

TEST(PapyrusDecompiler, CustomPropertyWithHandlersRoundTrips) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("CustomProp",
          "Scriptname CustomProp\n"
          "int mValue = 0\n"
          "int Property Value\n"
          "    int Function Get()\n"
          "        Return mValue\n"
          "    EndFunction\n"
          "    Function Set(int newValue)\n"
          "        mValue = newValue\n"
          "    EndFunction\n"
          "EndProperty\n");
  f.roundTrip("CustomProp");
}

TEST(PapyrusDecompiler, StructsAndStructAccessRoundTrip) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Structs",
          "Scriptname Structs\n"
          "Struct Pair\n"
          "    int iKey = 0\n"
          "    string sValue = \"\"\n"
          "EndStruct\n"
          "Function Go()\n"
          "    Pair p = new Pair\n"
          "    p.iKey = 5\n"
          "EndFunction\n");
  f.roundTrip("Structs");
}

TEST(PapyrusDecompiler, ArraysRoundTrip) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Arrays",
          "Scriptname Arrays\n"
          "Function Go()\n"
          "    int[] xs = new int[3]\n"
          "    xs[0] = 1\n"
          "    xs.Add(2)\n"
          "    int n = xs.Length\n"
          "EndFunction\n");
  f.roundTrip("Arrays");
}

TEST(PapyrusDecompiler, CallsOnOtherObjectsRoundTrip) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Holder", "Scriptname Holder\nint Function Give(int n)\n    Return n\nEndFunction\n");
  f.write("User",
          "Scriptname User\n"
          "Function Go(Holder h)\n"
          "    h.Give(5)\n"
          "    int x = h.Give(1)\n"
          "EndFunction\n");
  f.roundTrip("User");
}

TEST(PapyrusDecompiler, BooleanAndComparisonFormsReparseCleanly) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Logic",
          "Scriptname Logic\n"
          "Function Go(int a, int b)\n"
          "    bool c = (a > 2) && (b < 4)\n"
          "    if !c\n"
          "        b = 0\n"
          "    endIf\n"
          "EndFunction\n");
  auto original = f.compile("Logic");
  ASSERT_TRUE(original != nullptr);
  const std::string text = Decompiler::toSource(*original);
  const auto reparsed = Parser::parse(text, "Logic.psc");
  ASSERT_FALSE(reparsed.hasErrors());
  DecompileFixture f2;
  f2.write("Logic", text);
  ASSERT_TRUE(f2.compile("Logic") != nullptr);
}

TEST(PapyrusDecompiler, CompositeRoundTripScriptRoundTrips) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("RoundTrip",
          "Scriptname RoundTrip\n"
          "{A script with most of the shapes in it}\n"
          "Struct Pair\n"
          "    int iKey = 0\n"
          "    string sValue = \"\"\n"
          "EndStruct\n"
          "int Property Count = 1 Auto Hidden\n"
          "int Property Mask = 8 AutoReadOnly\n"
          "string mName\n"
          "Event OnInit()\n"
          "    Count += 1\n"
          "    Pair p = new Pair\n"
          "    p.iKey = Count\n"
          "    if Count > 2 && mName != \"\"\n"
          "        mName = \"big\"\n"
          "    else\n"
          "        mName = \"small\"\n"
          "    endIf\n"
          "    int i = 0\n"
          "    while i < Count\n"
          "        i += 1\n"
          "    endWhile\n"
          "EndEvent\n");
  auto original = f.compile("RoundTrip");
  ASSERT_TRUE(original != nullptr);
  const std::string text = Decompiler::toSource(*original);
  const auto reparsed = Parser::parse(text, "RoundTrip.psc");
  ASSERT_FALSE(reparsed.hasErrors());
  DecompileFixture f2;
  f2.write("RoundTrip", text);
  ASSERT_TRUE(f2.compile("RoundTrip") != nullptr);
}
