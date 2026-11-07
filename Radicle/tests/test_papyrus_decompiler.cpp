#include "fo4/papyrus/decompiler.hpp"
#include "fo4/papyrus/decompile_service.hpp"
#include "fo4/pex/pex.hpp"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include "papyrus_pex_comparer.hpp"

#include <fo4/papyrus/code_generator.hpp>
#include <fo4/papyrus/declaration_check.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type_checker.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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
using fo4::pex::PexDebugFunction;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexStruct;
using fo4::pex::PexStructMember;
using fo4::pex::PexStructOrder;
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

void writePex(const std::filesystem::path& path, const std::string& name) {
  auto pex = scriptWith(makeFunction("Run", {}, {}, {}), name);
  pex.majorVersion = 3; pex.minorVersion = 9; pex.gameId = 2;
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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

// Detects: a folder decompile must retain nested names, report bad PEX files, and skip symlinks.
TEST(PapyrusDecompiler, FolderDecompileWritesNamespacedSourceAndReportsFailures) {
  DecompileFixture f;
  const auto input = std::filesystem::path(f.root) / "input";
  const auto output = std::filesystem::path(f.root) / "output";
  std::filesystem::create_directories(input / "nested");
  auto pex = scriptWith(makeFunction("Run", {}, {}, {}), "Space:Example");
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  std::ofstream good(input / "nested" / "example.PEX", std::ios::binary);
  good.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  good.close();
  std::ofstream(input / "bad.pex") << "invalid";
  const auto result = fo4::papyrus::DecompileService::decompile(input.string(), output.string(), false, false);
  EXPECT_NE(result.find("RESULT: 1/2 decompiled -> .psc"), std::string::npos);
  EXPECT_NE(result.find("bad.pex:"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(output / "Space" / "Example.psc"));
}

// Detects: a linked input must not be decompiled a second time.
TEST(PapyrusDecompiler, FolderDecompileSkipsInputSymlinks) {
  DecompileFixture f;
  const auto input = std::filesystem::path(f.root) / "input";
  std::filesystem::create_directories(input);
  writePex(input / "original.pex", "Example");
  std::error_code ec;
  std::filesystem::create_symlink(input / "original.pex", input / "link.pex", ec);
  if (ec) GTEST_SKIP() << "Cannot create file symlink: " << ec.message();
  const auto result = fo4::papyrus::DecompileService::decompile(input.string(), "", false, false);
  EXPECT_NE(result.find("RESULT: 1/1 decompiled"), std::string::npos);
}

// Detects: an output namespace link must not redirect a decompile outside the output folder.
TEST(PapyrusDecompiler, FolderDecompileRejectsLinkedNamespace) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  const auto input = root / "input";
  const auto output = root / "output";
  const auto outside = root / "outside";
  std::filesystem::create_directories(input);
  std::filesystem::create_directories(output);
  std::filesystem::create_directories(outside);
  auto pex = scriptWith(makeFunction("Run", {}, {}, {}), "Space:Example");
  pex.majorVersion = 3; pex.minorVersion = 9; pex.gameId = 2;
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  std::ofstream file(input / "original.pex", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  file.close();
  std::error_code ec;
  std::filesystem::create_directory_symlink(outside, output / "Space", ec);
  if (ec) GTEST_SKIP() << "Cannot create directory symlink: " << ec.message();
  const auto result = fo4::papyrus::DecompileService::decompile(input.string(), output.string(), false, false);
  EXPECT_NE(result.find("RESULT: 0/1 decompiled"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(outside / "Example.psc"));
}

// Detects: a destination link must not overwrite its external target.
TEST(PapyrusDecompiler, DecompileDoesNotFollowDestinationLink) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  std::filesystem::create_directories(root / "output");
  auto pex = scriptWith(makeFunction("Run", {}, {}, {}), "Example");
  pex.majorVersion = 3; pex.minorVersion = 9; pex.gameId = 2;
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  std::ofstream file(root / "original.pex", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  file.close();
  std::ofstream(root / "outside.psc") << "keep";
  std::error_code ec;
  std::filesystem::create_symlink(root / "outside.psc", root / "output" / "Example.psc", ec);
  if (ec) GTEST_SKIP() << "Cannot create file symlink: " << ec.message();
  fo4::papyrus::DecompileService::decompile((root / "original.pex").string(), (root / "output").string(), false, true);
  std::ifstream outside(root / "outside.psc");
  std::string contents; outside >> contents;
  EXPECT_EQ(contents, "keep");
  EXPECT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(root / "output" / "Example.psc")));
}

// Detects: scripts that derive the same destination must not silently overwrite each other.
TEST(PapyrusDecompiler, FolderDecompileReportsDuplicateDestination) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  std::filesystem::create_directories(root / "input");
  auto pex = scriptWith(makeFunction("Run", {}, {}, {}), "Example");
  pex.majorVersion = 3; pex.minorVersion = 9; pex.gameId = 2;
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  for (const auto* name : {"a.pex", "b.pex"}) {
    std::ofstream file(root / "input" / name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto result = fo4::papyrus::DecompileService::decompile((root / "input").string(), (root / "output").string(), false, false);
  EXPECT_NE(result.find("RESULT: 1/2 decompiled"), std::string::npos);
  EXPECT_NE(result.find("duplicate"), std::string::npos);
}

#ifndef _WIN32
// Detects: case-distinct script names on a case-sensitive filesystem must both be written.
TEST(PapyrusDecompiler, FolderDecompileKeepsCaseDistinctOutputs) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  std::filesystem::create_directories(root / "input");
  std::filesystem::create_directories(root / "output");
  std::ofstream(root / "output" / "case-probe") << "probe";
  if (std::filesystem::exists(root / "output" / "CASE-PROBE")) {
    GTEST_SKIP() << "Output filesystem is case-insensitive";
  }
  for (const auto& [filename, object] : {std::pair{"a.pex", "Foo"}, std::pair{"b.pex", "foo"}}) {
    writePex(root / "input" / filename, object);
  }
  const auto result = fo4::papyrus::DecompileService::decompile(
      (root / "input").string(), (root / "output").string(), false, false);
  EXPECT_NE(result.find("RESULT: 2/2 decompiled"), std::string::npos);
  EXPECT_TRUE(std::filesystem::is_regular_file(root / "output" / "Foo.psc"));
  EXPECT_TRUE(std::filesystem::is_regular_file(root / "output" / "foo.psc"));
}

// Detects: case aliases on a case-insensitive POSIX volume must not overwrite an earlier output.
TEST(PapyrusDecompiler, FolderDecompileReportsCaseAliasesOnInsensitiveVolume) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  std::filesystem::create_directories(root / "input");
  std::filesystem::create_directories(root / "output");
  std::ofstream(root / "output" / "case-probe") << "probe";
  if (!std::filesystem::exists(root / "output" / "CASE-PROBE")) {
    GTEST_SKIP() << "Output filesystem is case-sensitive";
  }
  for (const auto& [filename, object] : {std::pair{"a.pex", "Foo"}, std::pair{"b.pex", "foo"}}) {
    writePex(root / "input" / filename, object);
  }
  const auto result = fo4::papyrus::DecompileService::decompile(
      (root / "input").string(), (root / "output").string(), false, false);
  EXPECT_NE(result.find("RESULT: 1/2 decompiled"), std::string::npos);
  EXPECT_NE(result.find("duplicate output"), std::string::npos);
}
#endif

#ifdef _WIN32
// Detects: Windows trailing-dot aliases must not silently overwrite a different script.
TEST(PapyrusDecompiler, FolderDecompileRejectsWindowsNamespaceAliases) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  std::filesystem::create_directories(root / "input");
  for (const auto& [filename, object] : {std::pair{"a.pex", "Space:Example"},
                                         std::pair{"b.pex", "Space.:Example"}}) {
    writePex(root / "input" / filename, object);
  }
  const auto result = fo4::papyrus::DecompileService::decompile(
      (root / "input").string(), (root / "output").string(), false, false);
  EXPECT_NE(result.find("RESULT: 1/2 decompiled"), std::string::npos);
  EXPECT_NE(result.find("invalid Windows output"), std::string::npos);
  EXPECT_TRUE(std::filesystem::is_regular_file(root / "output" / "Space" / "Example.psc"));
}

// Detects: a Windows short directory alias must not produce a second output for the same file.
TEST(PapyrusDecompiler, FolderDecompileReportsWindowsShortNameAlias) {
  DecompileFixture f;
  const auto root = std::filesystem::path(f.root);
  const auto folder = root / "output" / "LongNamespaceName";
  std::filesystem::create_directories(root / "input");
  std::filesystem::create_directories(folder);
  const auto length = GetShortPathNameW(folder.c_str(), nullptr, 0);
  if (!length) GTEST_SKIP() << "Short names unavailable";
  std::wstring shortPath(length, L'\0');
  const auto actual = GetShortPathNameW(folder.c_str(), shortPath.data(), length);
  if (!actual || actual >= length) GTEST_SKIP() << "Short names unavailable";
  shortPath.resize(actual);
  const auto alias = std::filesystem::path(shortPath).filename().string();
  if (alias == "LongNamespaceName") GTEST_SKIP() << "Filesystem did not assign a short name";
  for (const auto& [filename, object] : {std::pair{std::string("a.pex"), std::string("LongNamespaceName:Example")},
                                         std::pair{std::string("b.pex"), alias + ":Example"}}) {
    writePex(root / "input" / filename, object);
  }
  const auto result = fo4::papyrus::DecompileService::decompile(
      (root / "input").string(), (root / "output").string(), false, false);
  EXPECT_NE(result.find("RESULT: 1/2 decompiled"), std::string::npos);
  EXPECT_NE(result.find("duplicate output"), std::string::npos);
}
#endif

// Detects: assembly folder output must use the input basename instead of the script namespace.
TEST(PapyrusDecompiler, FolderDisassemblyUsesInputBasename) {
  DecompileFixture f;
  const auto input = std::filesystem::path(f.root) / "input";
  const auto output = std::filesystem::path(f.root) / "output";
  std::filesystem::create_directories(input);
  auto pex = scriptWith(makeFunction("Run", {}, {}, {}), "Space:Example");
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.rebuildStringTable();
  const auto bytes = pex.toBytes();
  std::ofstream file(input / "original.pex", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  file.close();
  const auto result = fo4::papyrus::DecompileService::decompile(input.string(), output.string(), true, false);
  EXPECT_NE(result.find("RESULT: 1/1 decompiled -> .pas"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(output / "original.pas"));
}

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

TEST(PapyrusDecompiler, RemoteEventWhosePrefixDiffersInCaseRecompilesToTheSameName) {
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Sender", "Scriptname Sender\n");
  f.write("Listener",
          "Scriptname Listener\n"
          "Event sender.Kick(Sender akSender)\n"
          "EndEvent\n");
  auto pex = f.compile("Listener");
  ASSERT_TRUE(pex != nullptr);
  const std::string text = Decompiler::toSource(*pex);
  EXPECT_NE(text.find("Event Sender.Kick(Sender akSender)"), std::string::npos);

  DecompileFixture f2;
  f2.writeWithScriptObject();
  f2.write("Sender", "Scriptname Sender\n");
  f2.write("Listener", text);
  auto again = f2.compile("Listener");
  ASSERT_TRUE(again != nullptr);
  const auto lower = [](std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  };
  const auto& original = pex->objects.at(0).states.at(0).functions.at(0).name;
  const auto& recompiled = again->objects.at(0).states.at(0).functions.at(0).name;
  EXPECT_EQ(lower(recompiled), lower(original));
}

TEST(PapyrusDecompiler, ObjectNameThatIsNotASafeFileNameIsRefused) {
  const auto fn = makeFunction("Go", {}, {}, {step("return", {PexValue{}})});
  for (const auto& name : {"Bad/Name", "Namespace:..:escape", "Namespace:folder/escape",
                           "Namespace:folder\\escape", "Namespace::escape"}) {
    EXPECT_THROW(Decompiler::toSource(scriptWith(fn, name)), std::runtime_error) << name;
  }
}

TEST(PapyrusDecompiler, NamespacedObjectNameIsAcceptedAndEmittedAsScriptName) {
  const auto fn = makeFunction("Go", {}, {}, {step("return", {PexValue{}})});
  const std::string text = Decompiler::toSource(scriptWith(fn, "Namespace:SafeScript"));
  EXPECT_NE(text.find("ScriptName Namespace:SafeScript"), std::string::npos);
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

TEST(PapyrusDecompiler, InheritedAutoPropertyReadMapsBackToThePropertyName) {
  const std::string base = "Scriptname InheritBase\nint Property Count = 3 Auto\n";
  const std::string child =
      "Scriptname InheritChild extends InheritBase\n"
      "Function Go()\n"
      "    int i = Count\n"
      "    Count = i + 1\n"
      "EndFunction\n";

  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("InheritBase", base);
  f.write("InheritChild", child);
  auto pex = f.compile("InheritChild");
  ASSERT_TRUE(pex != nullptr);
  const std::string text = Decompiler::toSource(*pex);
  SCOPED_TRACE("decompiled source:\n" + text);
  EXPECT_EQ(text.find("_Count_var"), std::string::npos);
  EXPECT_NE(text.find("i = Count"), std::string::npos);
  EXPECT_NE(text.find("Count = (i + 1)"), std::string::npos);

  DecompileFixture f2;
  f2.writeWithScriptObject();
  f2.write("InheritBase", base);
  f2.write("InheritChild", text);
  auto again = f2.compile("InheritChild");
  ASSERT_TRUE(again != nullptr);
  ASSERT_TRUE(again->objects.size() == 1);
  EXPECT_TRUE(firstDifference(pex->objects[0], again->objects[0]).empty());
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

TEST(PapyrusDecompiler, PropertyGroupsAreEmittedAsGroupBlocksInOriginalOrder) {
  // Invariant: ungrouped properties print first, then each named group with its doc, in PEX order.
  DecompileFixture f;
  f.writeWithScriptObject();
  f.write("Grouped",
          "Scriptname Grouped\n"
          "int Property Loose Auto\n"
          "Group Stats\n"
          "{ Stat block }\n"
          "int Property Health = 100 Auto\n"
          "int Property Mana Auto\n"
          "EndGroup\n");
  auto original = f.compile("Grouped");
  ASSERT_TRUE(original != nullptr);
  const std::string text = Decompiler::toSource(*original);
  SCOPED_TRACE("decompiled source:\n" + text);

  const auto loose = text.find("Int Property Loose");
  const auto group = text.find("Group Stats\n");
  const auto doc = text.find("{  Stat block  }");
  const auto health = text.find("Int Property Health");
  const auto mana = text.find("Int Property Mana");
  const auto end = text.find("EndGroup\n");
  ASSERT_TRUE(loose != std::string::npos && group != std::string::npos && doc != std::string::npos);
  ASSERT_TRUE(health != std::string::npos && mana != std::string::npos && end != std::string::npos);
  EXPECT_LT(loose, group);
  EXPECT_LT(group, doc);
  EXPECT_LT(doc, health);
  EXPECT_LT(health, mana);
  EXPECT_LT(mana, end);

  const auto shapeOf = [](const PexFile& pex) {
    std::vector<std::pair<std::string, std::vector<std::string>>> shape;
    for (const auto& entry : pex.propertyGroups) shape.emplace_back(entry.groupName, entry.propertyNames);
    return shape;
  };
  DecompileFixture g;
  g.writeWithScriptObject();
  g.write("Grouped", text);
  auto recompiled = g.compile("Grouped");
  ASSERT_TRUE(recompiled != nullptr);
  EXPECT_EQ(shapeOf(*recompiled), shapeOf(*original));
}

TEST(PapyrusDecompiler, StructMembersFollowTheDebugStructOrder) {
  // Invariant: members print in the order the PEX struct order lists them, not their stored order.
  auto file = scriptWith(makeFunction("Go", {}, {}, {step("return", {PexValue{}})}));
  PexStructMember key;
  key.name = "iKey";
  key.type = "int";
  PexStructMember value;
  value.name = "sValue";
  value.type = "string";
  PexStruct pair;
  pair.name = "Pair";
  pair.members = {key, value};
  file.objects[0].structs.push_back(pair);
  file.structOrders.push_back(PexStructOrder{"Probe", "Pair", {"sValue", "iKey"}});
  EXPECT_EQ(Decompiler::toSource(file),
            "ScriptName Probe extends ScriptObject\n\n"
            "Struct Pair\n"
            "\tstring sValue\n"
            "\tint iKey\n"
            "EndStruct\n"
            "\n"
            "Function Go()\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, OnPrefixAloneDoesNotMakeAFunctionAnEvent) {
  // Invariant: only names in the FO4 event table or remote events become Event blocks; an On prefix alone does not.
  auto file = scriptWith(makeFunction("OnlyOnce", {}, {}, {step("return", {PexValue{}})}));
  file.objects[0].states[0].functions.push_back(
      makeFunction("OnFoo", {}, {}, {step("return", {PexValue{}})}));
  EXPECT_EQ(Decompiler::toSource(file),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function OnlyOnce()\n"
            "\tReturn\n"
            "EndFunction\n"
            "\n"
            "Function OnFoo()\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, KnownEventNamesAreEventsWhateverTheDebugFunctionTypes) {
  // Invariant: event detection reads only name, return type and global flag, so absent, consistent or conflicting debug records cannot change it.
  const std::vector<std::string> names{"OnInit", "OnActivate", "OnHit", "OnCellAttach", "OnlyOnce"};
  const auto build = [&](const std::vector<std::uint8_t>* debugTypes) {
    auto file = scriptWith(makeFunction(names[0], {}, {}, {step("return", {PexValue{}})}));
    for (std::size_t i = 1; i < names.size(); ++i) {
      file.objects[0].states[0].functions.push_back(
          makeFunction(names[i], {}, {}, {step("return", {PexValue{}})}));
    }
    if (debugTypes != nullptr) {
      file.hasDebugInfo = true;
      for (std::size_t i = 0; i < names.size(); ++i) {
        file.debugFunctions.push_back(PexDebugFunction{"Probe", "", names[i], (*debugTypes)[i], {}});
      }
    }
    return file;
  };
  const std::vector<std::uint8_t> consistent{0, 0, 0, 0, 0};
  const std::vector<std::uint8_t> conflicting{2, 250, 1, 0, 2};
  const std::string expected =
      "ScriptName Probe extends ScriptObject\n\n"
      "Event OnInit()\n"
      "\tReturn\n"
      "EndEvent\n"
      "\n"
      "Event OnActivate()\n"
      "\tReturn\n"
      "EndEvent\n"
      "\n"
      "Event OnHit()\n"
      "\tReturn\n"
      "EndEvent\n"
      "\n"
      "Event OnCellAttach()\n"
      "\tReturn\n"
      "EndEvent\n"
      "\n"
      "Function OnlyOnce()\n"
      "\tReturn\n"
      "EndFunction\n";
  EXPECT_EQ(Decompiler::toSource(build(nullptr)), expected);
  EXPECT_EQ(Decompiler::toSource(build(&consistent)), expected);
  EXPECT_EQ(Decompiler::toSource(build(&conflicting)), expected);
}

TEST(PapyrusDecompiler, ScriptLocalNameThatIsNotAnEngineEventStaysAFunction) {
  // Invariant: a None-returning function named like a script-local event (flee) is a Function, because only the engine event table makes an Event.
  const auto fn = makeFunction("flee", {}, {}, {step("return", {PexValue{}})});
  EXPECT_EQ(Decompiler::toSource(scriptWith(fn)),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function flee()\n"
            "\tReturn\n"
            "EndFunction\n");
}

TEST(PapyrusDecompiler, GlobalNativeFunctionsStayFunctionsEvenWithAnEventName) {
  // Invariant: a global function is never an Event, even when its name is in the event table (OnLoad) or it returns None (Hide).
  PexFunction hide;
  hide.name = "Hide";
  hide.isGlobal = true;
  hide.isNative = true;
  hide.params = {{"asName", "string"}};
  PexFunction onLoad;
  onLoad.name = "OnLoad";
  onLoad.isGlobal = true;
  onLoad.isNative = true;
  auto file = scriptWith(hide);
  file.objects[0].states[0].functions.push_back(onLoad);
  EXPECT_EQ(Decompiler::toSource(file),
            "ScriptName Probe extends ScriptObject\n\n"
            "Function Hide(string asName) Global Native\n"
            "\n"
            "Function OnLoad() Global Native\n");
}

TEST(PapyrusDecompiler, TempReadBeforeItsAssignmentIsFlaggedInTheDecompile) {
  // Invariant: a compiler temporary read before any assignment is named in a warning line, not silently printed as _temp0.
  const auto fn = makeFunction("Go", {}, {{"i", "int"}},
                               {step("assign", {identifier("i"), identifier("::temp0")}),
                                step("return", {PexValue{}})});
  const std::string text = Decompiler::toSource(scriptWith(fn));
  EXPECT_NE(text.find("\t; [decompile warning: compiler temporary ::temp0 is read before it is assigned; "
                      "decompiled as _temp0]\n"),
            std::string::npos);
}
