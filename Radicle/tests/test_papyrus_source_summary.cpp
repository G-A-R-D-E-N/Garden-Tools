#include <fo4/papyrus/source_summary.hpp>
#include <fo4/papyrus_source/parser.hpp>
#include <fo4/papyrus_source/summary.hpp>

#include "papyrus_corpus_roots.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using fo4::papyrus::SourceSummary;
using fo4::papyrus::SummaryEvent;
using fo4::papyrus::SummaryParameter;

std::vector<SummaryParameter> pscParameters(const std::vector<fo4::psc::Parameter>& parameters) {
  std::vector<SummaryParameter> out;
  for (const auto& parameter : parameters) out.push_back({parameter.name, parameter.type, parameter.isArray});
  return out;
}

void pscScope(SourceSummary& summary, const std::string& state, const std::vector<fo4::psc::FunctionDeclaration>& functions,
              const std::vector<fo4::psc::CustomEventDeclaration>& custom) {
  for (const auto& function : functions) {
    if (function.kind != fo4::psc::FunctionKind::Event) continue;
    summary.events.push_back({function.name, state, false, pscParameters(function.parameters)});
  }
  for (const auto& event : custom) summary.events.push_back({event.name, state, true, pscParameters(event.parameters)});
}

void pscStates(SourceSummary& summary, const std::vector<fo4::psc::StateDeclaration>& states) {
  for (const auto& state : states) {
    pscScope(summary, state.name, state.functions, state.customEvents);
    pscStates(summary, state.states);
  }
}

SourceSummary pscView(const std::string& text) {
  SourceSummary view;
  const fo4::psc::ParseResult parsed = fo4::psc::parse(text);
  if (!parsed.ok()) {
    view.ok = false;
    return view;
  }
  const fo4::psc::ScriptSummary summary = fo4::psc::summarize(parsed.script);
  view.name = summary.name;
  view.extends = summary.extends;
  view.autoState = summary.autoState;
  view.imports = summary.imports;
  for (const std::string& property : summary.declaredProperties) {
    const bool isAuto = std::find(summary.autoProperties.begin(), summary.autoProperties.end(), property) !=
                        summary.autoProperties.end();
    view.properties.push_back({property, isAuto});
  }
  view.states = summary.states;
  for (const auto& function : summary.functions) {
    view.functions.push_back({function.name, function.state, function.isNative, function.isEvent});
  }
  pscScope(view, std::string(), parsed.script.functions, parsed.script.customEvents);
  pscStates(view, parsed.script.states);
  return view;
}

std::string describeFunctions(const SourceSummary& summary) {
  std::string out;
  for (const auto& function : summary.functions) {
    out += function.name + "|" + function.state + "|" + (function.isNative ? "n" : "-") + (function.isEvent ? "e" : "-") + ";";
  }
  return out;
}

std::string describeEvents(const SourceSummary& summary) {
  std::string out;
  for (const SummaryEvent& event : summary.events) {
    out += event.name + "|" + event.state + "|" + (event.isCustom ? "c" : "-") + "(";
    for (const auto& parameter : event.parameters) {
      out += parameter.type + (parameter.isArray ? "[]" : "") + " " + parameter.name + ",";
    }
    out += ");";
  }
  return out;
}

std::string describeProperties(const SourceSummary& summary) {
  std::string out;
  for (const auto& property : summary.properties) out += property.name + (property.isAuto ? "=auto;" : ";");
  return out;
}

std::string join(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& item : items) out += item + ";";
  return out;
}

std::string difference(const std::string& text) {
  const SourceSummary theirs = pscView(text);
  const SourceSummary ours = fo4::papyrus::summarizeSource(text);
  if (theirs.ok != ours.ok) return std::string("accepted by ") + (theirs.ok ? "fo4::psc only" : "fo4::papyrus only");
  if (!theirs.ok) return std::string();
  if (theirs.name != ours.name) return "name " + theirs.name + " vs " + ours.name;
  if (theirs.extends != ours.extends) return "extends " + theirs.extends + " vs " + ours.extends;
  if (theirs.autoState != ours.autoState) return "auto state " + theirs.autoState + " vs " + ours.autoState;
  if (theirs.imports != ours.imports) return "imports " + join(theirs.imports) + " vs " + join(ours.imports);
  if (describeProperties(theirs) != describeProperties(ours)) {
    return "properties " + describeProperties(theirs) + " vs " + describeProperties(ours);
  }
  if (theirs.states != ours.states) return "states " + join(theirs.states) + " vs " + join(ours.states);
  if (describeFunctions(theirs) != describeFunctions(ours)) {
    return "functions " + describeFunctions(theirs) + " vs " + describeFunctions(ours);
  }
  if (describeEvents(theirs) != describeEvents(ours)) {
    return "events " + describeEvents(theirs) + " vs " + describeEvents(ours);
  }
  return std::string();
}

const std::vector<std::string>& fixtures() {
  static const std::vector<std::string> all = {
      "Scriptname Summary:Probe extends ObjectReference\n"
      "Import Utility\n"
      "Int Property Count Auto\n"
      "Int Property Total AutoReadOnly\n"
      "Event Actor.OnPlayerLoadGame(Actor akSender)\n"
      "EndEvent\n"
      "Function Helper()\n"
      "EndFunction\n"
      "State Busy\n"
      "  Function Tick()\n"
      "  EndFunction\n"
      "EndState\n",

      "ScriptName Actor extends ObjectReference\n"
      "Event OnDeath(Actor akKiller)\n"
      "EndEvent\n"
      "Event OnLoad()\n"
      "EndEvent\n"
      "CustomEvent Ping\n"
      "Function Helper()\n"
      "EndFunction\n"
      "State Dead\n"
      "  Event OnBeginState()\n"
      "  EndEvent\n"
      "EndState\n",

      "ScriptName ObjectReference extends Form\n"
      "Event OnPackageEnd(Form[] akItems, int aiCount)\n"
      "EndEvent\n",

      "ScriptName Mixed extends Quest\n"
      "Import Debug\n"
      "Import Math\n"
      "Float Property Speed Hidden\n"
      "  Float Function Get()\n"
      "    Return 1.0\n"
      "  EndFunction\n"
      "  Function Set(Float value)\n"
      "  EndFunction\n"
      "EndProperty\n"
      "Bool Property Flag = true Auto Conditional\n"
      "Group Tuning\n"
      "  Int Property Level = 3 Auto\n"
      "EndGroup\n"
      "Function Alpha(String Text, Var[] Items) Global Native\n"
      "Event OnInit()\n"
      "EndEvent\n"
      "Function Beta()\n"
      "EndFunction\n"
      "Event Actor.OnDying(Actor akKiller)\n"
      "EndEvent\n"
      "Auto State Idle\n"
      "  Event OnBeginState(String oldState)\n"
      "  EndEvent\n"
      "  Function Wake()\n"
      "  EndFunction\n"
      "EndState\n"
      "State Idle\n"
      "EndState\n"
      "State Active\n"
      "  Function Wake()\n"
      "  EndFunction\n"
      "EndState\n",

      "ScriptName Structs\n"
      "Struct Point\n"
      "  Int X = 1\n"
      "  String Label = \"a\"\n"
      "EndStruct\n"
      "Int Function Bump(Int start)\n"
      "  Point p = new Point\n"
      "  p.X = start + 4\n"
      "  Return p.X\n"
      "EndFunction\n",

      "ScriptName Broken\nEvent OnX(\n",
      "ScriptName Odd\nInt x = 1 #\n",
      "Function NoHeader()\nEndFunction\n",
  };
  return all;
}

}  // namespace

TEST(PapyrusSourceSummaryParity, FixturesAgreeWithFo4Psc) {
  for (const std::string& text : fixtures()) {
    EXPECT_EQ(difference(text), "") << text;
  }
}

TEST(PapyrusSourceSummary, KeepsSourceCaseOfPrimitiveParameterTypes) {
  const SourceSummary summary = fo4::papyrus::summarizeSource(
      "ScriptName ObjectReference extends Form\n"
      "Event OnPackageEnd(Form[] akItems, Int aiCount, STRING asText)\n"
      "EndEvent\n");
  ASSERT_TRUE(summary.ok);
  ASSERT_EQ(summary.events.size(), 1u);
  ASSERT_EQ(summary.events[0].parameters.size(), 3u);
  EXPECT_EQ(summary.events[0].parameters[0].type, "Form");
  EXPECT_TRUE(summary.events[0].parameters[0].isArray);
  EXPECT_EQ(summary.events[0].parameters[1].type, "Int");
  EXPECT_EQ(summary.events[0].parameters[2].type, "STRING");
}

TEST(PapyrusSourceSummary, ReportsTheFirstErrorWithItsPosition) {
  const SourceSummary summary = fo4::papyrus::summarizeSource("ScriptName Odd\nInt x = 1 #\n");
  EXPECT_FALSE(summary.ok);
  EXPECT_EQ(summary.errorLine, 2);
  EXPECT_FALSE(summary.errorMessage.empty());
}

TEST(PapyrusSourceSummary, ReportsTheEarliestErrorWhenALexerErrorComesLater) {
  const SourceSummary summary =
      fo4::papyrus::summarizeSource("ScriptName Odd\nEvent OnX(\nEndEvent\nFunction A()\nEndFunction\nString s = \"unterminated\n");
  EXPECT_FALSE(summary.ok);
  EXPECT_LE(summary.errorLine, 3);
}

TEST(PapyrusSourceSummaryParity, VanillaCorpusAgreesWithFo4Psc) {
  const auto roots = fo4::papyrus::test::rootsFromEnv("FO4RE_PSC_ROOTS");
  if (roots.empty()) GTEST_SKIP() << "FO4RE_PSC_ROOTS is not set to a directory holding .psc sources";
  std::size_t checked = 0;
  std::size_t mismatched = 0;
  for (const auto& root : roots) {
    for (const auto& path : fo4::papyrus::test::enumerateFiles(root, ".psc")) {
      std::ifstream stream(path, std::ios::binary);
      const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
      if (text.empty()) continue;
      ++checked;
      const std::string found = difference(text);
      if (found.empty()) continue;
      if (++mismatched <= 20) std::cout << "MISMATCH " << path.generic_string() << ": " << found << std::endl;
    }
  }
  std::cout << "psc summary parity: checked=" << checked << " mismatched=" << mismatched << std::endl;
  ASSERT_GT(checked, 0u);
  EXPECT_EQ(mismatched, 0u);
}
