#include <fo4/papyrus/declaration_check.hpp>
#include <fo4/papyrus/parser.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

std::vector<fo4::papyrus::Diagnostic> duplicates_in(const std::string& source) {
  const auto script = fo4::papyrus::Parser::parse(source);
  EXPECT_FALSE(script.hasErrors());
  return fo4::papyrus::declarationCheck(script);
}

}

TEST(PapyrusDeclarationCheck, TwoFunctionsWithOneNameInTheEmptyStateAreReportedAsPap0020) {
  const auto problems = duplicates_in(
      "ScriptName Dup\n"
      "Function Foo()\n"
      "EndFunction\n"
      "Function Foo()\n"
      "EndFunction\n");
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_EQ(problems[0].code, fo4::papyrus::DiagnosticCode::DuplicateDeclaration);
  EXPECT_NE(problems[0].message.find(
                "'Foo' is already declared as a function or event in the empty state"),
            std::string::npos);
}

TEST(PapyrusDeclarationCheck, OneNameInTwoStatesIsNotADuplicate) {
  const auto problems = duplicates_in(
      "ScriptName Dup\n"
      "State A\n"
      "Function Foo()\n"
      "EndFunction\n"
      "EndState\n"
      "State B\n"
      "Function Foo()\n"
      "EndFunction\n"
      "EndState\n");
  EXPECT_TRUE(problems.empty());
}

TEST(PapyrusDeclarationCheck, RemoteHandlersKeyedByEmittedNameDoNotCollideAcrossRemoteTypes) {
  const auto problems = duplicates_in(
      "ScriptName Dup\n"
      "Event ObjectReference.OnActivate(ObjectReference akActionRef)\n"
      "EndEvent\n"
      "Event Actor.OnActivate(ObjectReference akActionRef)\n"
      "EndEvent\n");
  EXPECT_TRUE(problems.empty());
}

TEST(PapyrusDeclarationCheck, TwoHandlersForOneRemoteTypeAndEventAreReported) {
  const auto problems = duplicates_in(
      "ScriptName Dup\n"
      "Event Actor.OnActivate(ObjectReference akActionRef)\n"
      "EndEvent\n"
      "Event Actor.OnActivate(ObjectReference akActionRef)\n"
      "EndEvent\n");
  ASSERT_EQ(problems.size(), 1u);
  EXPECT_NE(problems[0].message.find("'Actor.OnActivate'"), std::string::npos);
}
