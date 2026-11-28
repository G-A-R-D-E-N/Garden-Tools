#include "papyrus_codegen_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>

using fo4::papyrus::ScriptIndex;
using fo4::papyrus::codegen_test::CodegenFixture;
using fo4::papyrus::codegen_test::function;
using fo4::papyrus::codegen_test::listing;

namespace {

std::string joinDiagnostics(const fo4::papyrus::codegen_test::CompileResult& result) {
  std::string joined;
  for (const auto& d : result.diagnostics) joined += d.toString() + " | ";
  return joined;
}

}

TEST(PapyrusCallBinding, FindCallableOnSkipsAPropertyThatSharesTheName) {
  const auto script = fo4::papyrus::Parser::parse(
      "Scriptname Holder\nint Property Dive Auto\nFunction Dive()\nEndFunction\n");
  const auto* member = ScriptIndex::findMemberOn(&script, "dive");
  const auto* callable = ScriptIndex::findCallableOn(&script, "dive");
  ASSERT_NE(member, nullptr);
  ASSERT_NE(callable, nullptr);
  EXPECT_EQ(member->nodeKind(), fo4::papyrus::NodeKind::PropertyDecl);
  EXPECT_EQ(callable->nodeKind(), fo4::papyrus::NodeKind::FunctionDecl);
}

TEST(PapyrusCallBinding, FindCallableOnReachesStateEventsAndIgnoresVariables) {
  const auto script = fo4::papyrus::Parser::parse(
      "Scriptname Holder\nint Ticks\nAuto State Busy\nEvent OnInit()\nEndEvent\nEndState\n");
  EXPECT_NE(ScriptIndex::findCallableOn(&script, "OnInit"), nullptr);
  EXPECT_EQ(ScriptIndex::findCallableOn(&script, "Ticks"), nullptr);
  EXPECT_EQ(ScriptIndex::findCallableOn(&script, ""), nullptr);
  EXPECT_EQ(ScriptIndex::findCallableOn(nullptr, "OnInit"), nullptr);
}

TEST(PapyrusCallBinding, CallThroughAnObjectBindsToTheFunctionNotASameNamedProperty) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Holder",
          "Scriptname Holder\nint Property Dive Auto\n"
          "Function Dive(float paulCut = 0.5)\nEndFunction\n");
  f.write("Caller",
          "Scriptname Caller\nHolder Property Target Auto\n"
          "Function Go()\n    Target.Dive()\nEndFunction\n");
  const auto result = f.compile("Caller");
  const auto joined = joinDiagnostics(result);
  ASSERT_TRUE(result.success) << joined;
  EXPECT_TRUE(joined.empty()) << joined;

  const auto lines = listing(function(result.pex->objects[0], "Go"));
  EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
    return line.rfind("callmethod Dive ", 0) == 0;
  }));
  EXPECT_TRUE(std::none_of(lines.begin(), lines.end(), [](const std::string& line) {
    return line.rfind("propget", 0) == 0;
  }));
}

TEST(PapyrusCallBinding, BareCallInsideTheOwningScriptBindsToTheFunctionNotTheProperty) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Owner",
          "Scriptname Owner\nint Property Dive Auto\n"
          "Function Dive(float paulCut = 0.5)\nEndFunction\n"
          "Function Go()\n    Dive()\nEndFunction\n");
  const auto result = f.compile("Owner");
  const auto joined = joinDiagnostics(result);
  ASSERT_TRUE(result.success) << joined;
  EXPECT_TRUE(std::all_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto& d) {
    return d.code == fo4::papyrus::DiagnosticCode::DuplicateDeclaration &&
           d.severity == fo4::papyrus::Severity::Warning;
  })) << joined;

  const auto lines = listing(function(result.pex->objects[0], "Go"));
  EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& line) {
    return line.rfind("callmethod Dive ", 0) == 0;
  }));
}

TEST(PapyrusCallBinding, ScriptHashStructArrayParameterResolvesToTheImportedStruct) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Holder", "Scriptname Holder\nStruct Memory\n    int fId = 0\nEndStruct\n");
  f.write("Consumer",
          "Scriptname Consumer\nFunction Take(Holder#Memory[] akItems)\nEndFunction\n"
          "Function Go()\n    Holder#Memory[] items = new Holder#Memory[2]\n    Take(items)\n"
          "EndFunction\n");
  const auto result = f.compile("Consumer");
  const auto joined = joinDiagnostics(result);
  EXPECT_TRUE(joined.empty()) << joined;
  EXPECT_TRUE(result.success);
}

TEST(PapyrusCallBinding, ScriptHashStructArrayParameterRejectsAnIntArgument) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("Holder", "Scriptname Holder\nStruct Memory\n    int fId = 0\nEndStruct\n");
  f.write("Consumer",
          "Scriptname Consumer\nFunction Take(Holder#Memory[] akItems)\nEndFunction\n"
          "Function Go()\n    Take(5)\nEndFunction\n");
  const auto result = f.compile("Consumer");
  const auto joined = joinDiagnostics(result);
  EXPECT_FALSE(result.success) << joined;
  const auto mismatch = std::count_if(
      result.diagnostics.begin(), result.diagnostics.end(), [](const auto& d) {
        return d.code == fo4::papyrus::DiagnosticCode::TypeMismatch &&
               d.message.find("pass to 'akItems'") != std::string::npos &&
               d.message.find("Holder:Memory[]") != std::string::npos;
      });
  EXPECT_EQ(mismatch, 1) << joined;
}

TEST(PapyrusCallBinding, StructImportedByTheResolvedScriptIsALocalDeclarationType) {
  CodegenFixture f;
  f.writeWithScriptObject();
  f.write("S", "Scriptname S\nStruct Point\n    int X\n    float Y\nEndStruct\n");
  f.write("A",
          "Scriptname A\nImport S\nFunction Go()\n    Point p = new Point\n    float y = p.Y\n"
          "EndFunction\n");
  const auto result = f.compile("A");
  const auto joined = joinDiagnostics(result);
  EXPECT_TRUE(joined.empty()) << joined;
  EXPECT_TRUE(result.success);
}
