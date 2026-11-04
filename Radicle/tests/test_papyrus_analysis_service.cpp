#include <fo4/papyrus/analysis_service.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using fo4::papyrus::AnalysisService;

namespace {

std::set<std::string> labelsOf(const nlohmann::json& reply) {
  std::set<std::string> labels;
  for (const auto& item : reply["items"]) labels.insert(item["label"].get<std::string>());
  return labels;
}

class ScratchRoot {
public:
  ScratchRoot()
      : path_(std::filesystem::temp_directory_path() /
              ("fo4re-analysis-service-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }

  ~ScratchRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  std::string path() const { return path_.string(); }

  std::string write(const std::string& relative, const std::string& content) const {
    const auto file = path_ / relative;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
    return file.string();
  }

private:
  std::filesystem::path path_;
};

}  // namespace

TEST(PapyrusAnalysisService, LineColumnToOffsetMapsAcrossLfAndCrlf) {
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc\ndef", 1, 1), 0);
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc\ndef", 2, 1), 4);
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc\ndef", 2, 3), 6);
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc\r\ndef", 2, 1), 5);
}

TEST(PapyrusAnalysisService, LineColumnToOffsetRejectsOutOfRangePositionsRatherThanClamping) {
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc", 0, 1), -1);
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc", 1, 0), -1);
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc", 5, 1), -1);
  EXPECT_EQ(AnalysisService::lineColumnToOffset("abc\ndef", 1, 9), -1);
}

TEST(PapyrusAnalysisService, CheckCountsCleanFileAsClean) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\nFunction F()\nEndFunction\n");

  const auto result = AnalysisService::check(file);
  EXPECT_TRUE(result.starts_with(
      "RESULT: 1 clean, 0 with syntax errors, 0 with name or type errors"));
}

TEST(PapyrusAnalysisService, CheckWithoutSemanticPassSaysItOnlyParsed) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\nFunction F()\nEndFunction\n");

  const auto result = AnalysisService::check(file, true, false);
  EXPECT_TRUE(result.starts_with("RESULT: 1 clean, 0 with syntax errors,"));
  EXPECT_NE(result.find("does not resolve names or check types"), std::string::npos);
}

TEST(PapyrusAnalysisService, CheckReportsUndefinedNameAsPap0030) {
  ScratchRoot root;
  const auto file =
      root.write("A.psc", "ScriptName A\nFunction F()\n    int n = nope\nEndFunction\n");

  const auto result = AnalysisService::check(file);
  EXPECT_NE(result.find("1 with name or type errors"), std::string::npos);
  EXPECT_NE(result.find("PAP0030"), std::string::npos);
}

TEST(PapyrusAnalysisService, CheckCountsIncompleteBaseChainSeparately) {
  ScratchRoot root;
  const auto file = root.write(
      "A.psc", "ScriptName A extends NotOnAnyRoot\nFunction F()\n    Inherited()\nEndFunction\n");

  const auto result = AnalysisService::check(file);
  EXPECT_NE(result.find("0 with name or type errors"), std::string::npos);
  EXPECT_NE(result.find("not on the roots"), std::string::npos);
}

TEST(PapyrusAnalysisService, CheckReportsSyntaxErrorWithFilePosition) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\nFunction F(\nEndFunction\n");

  const auto result = AnalysisService::check(file);
  EXPECT_TRUE(result.starts_with("RESULT: 0 clean, 1 with syntax errors"));
  EXPECT_NE(result.find("A.psc("), std::string::npos);
}

TEST(PapyrusAnalysisService, CheckWalksFolderAndCountsEveryPscFile) {
  ScratchRoot root;
  root.write("A.psc", "ScriptName A\n");
  root.write("nested/B.psc", "ScriptName nested:B\n");

  EXPECT_TRUE(AnalysisService::check(root.path()).starts_with(
      "RESULT: 2 clean, 0 with syntax errors"));
}

TEST(PapyrusAnalysisService, CheckRefusesPexAndPointsToDecompile) {
  ScratchRoot root;
  const auto file = root.write("A.pex", "not really a pex");

  EXPECT_NE(AnalysisService::check(file).find("decompile_papyrus"), std::string::npos);
}

TEST(PapyrusAnalysisService, CheckReportsMissingPathAsNotFound) {
  const auto missing =
      (std::filesystem::temp_directory_path() / "fo4re-no-such-file.psc").string();
  EXPECT_NE(AnalysisService::check(missing).find("Not found"), std::string::npos);
}

TEST(PapyrusAnalysisService, OutlineListsDeclarationsWithSignatures) {
  ScratchRoot root;
  const auto file =
      root.write("A.psc", "ScriptName A\nint Property Health auto\nFunction F()\nEndFunction\n");

  const auto result = AnalysisService::outline(file);
  EXPECT_NE(result.find("Property"), std::string::npos);
  EXPECT_NE(result.find("int Property Health Auto"), std::string::npos);
  EXPECT_NE(result.find("Function F()"), std::string::npos);
}

TEST(PapyrusAnalysisService, OutlineStillListsSymbolsAfterSyntaxErrors) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\nint Property Health auto\n%%%\n");

  const auto result = AnalysisService::outline(file);
  EXPECT_NE(result.find("syntax error"), std::string::npos);
  EXPECT_NE(result.find("Health"), std::string::npos);
}

TEST(PapyrusAnalysisService, DefinitionResolvesInheritedMemberInParentFile) {
  ScratchRoot root;
  root.write("Parent.psc",
             "ScriptName Parent\nFunction DoStuff()\n{Parent version}\nEndFunction\n");
  const auto child =
      root.write("Child.psc",
                 "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n");

  const auto result = AnalysisService::definition(child, 3, 3);
  EXPECT_NE(result.find("RESULT: Function DoStuff"), std::string::npos);
  EXPECT_NE(result.find("in: Parent"), std::string::npos);
  EXPECT_NE(result.find("Parent.psc(2,10)"), std::string::npos);
  EXPECT_NE(result.find("Parent version"), std::string::npos);
}

TEST(PapyrusAnalysisService, DefinitionSaysNotResolvedRatherThanGuessing) {
  ScratchRoot root;
  const auto file = root.write(
      "A.psc", "ScriptName A\nFunction F()\n  GetThing().Unknown()\nEndFunction\n");

  const auto result = AnalysisService::definition(file, 3, 15);
  EXPECT_TRUE(result.starts_with("RESULT: not resolved."));
  EXPECT_NE(result.find("no type checker"), std::string::npos);
}

TEST(PapyrusAnalysisService, DefinitionRejectsPositionOutsideFile) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\n");

  EXPECT_NE(AnalysisService::definition(file, 99, 1).find("outside"), std::string::npos);
}

TEST(PapyrusAnalysisService, AnalyzeJsonUsesBufferTextNotDisk) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\nint Property Health auto\n");
  const auto buffer = "ScriptName A\nint Property Health auto\nFunction F(\nEndFunction\n";

  const auto json = nlohmann::json::parse(AnalysisService::analyzeJson(buffer, &file));
  EXPECT_GT(json["errorCount"].get<int>(), 0);
  const auto& first = json["diagnostics"][0];
  EXPECT_EQ(first["line"].get<int>(), 3);
  EXPECT_EQ(first["severity"].get<std::string>(), "error");
  bool hasHealth = false;
  for (const auto& s : json["symbols"]) {
    if (s["name"].get<std::string>() == "Health") hasHealth = true;
  }
  EXPECT_TRUE(hasHealth);
}

TEST(PapyrusAnalysisService, AnalyzeJsonReportsNameAndExtendsForCleanBuffer) {
  const auto json = nlohmann::json::parse(AnalysisService::analyzeJson("ScriptName A extends B\n"));
  EXPECT_EQ(json["errorCount"].get<int>(), 0);
  EXPECT_EQ(json["script"].get<std::string>(), "A");
  EXPECT_EQ(json["extends"].get<std::string>(), "B");
}

TEST(PapyrusAnalysisService, SymbolAtResolvesWithinBufferAsSameFile) {
  ScratchRoot root;
  const std::string text =
      "ScriptName A\nint Property Health auto\nFunction F()\n  Health = 1\nEndFunction\n";
  const auto file = root.write("A.psc", text);
  const auto offset = static_cast<int>(text.find("Health = 1"));

  const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, &file, offset));
  EXPECT_TRUE(json["resolved"].get<bool>());
  EXPECT_EQ(json["kind"].get<std::string>(), "Property");
  EXPECT_TRUE(json["sameFile"].get<bool>());
  EXPECT_EQ(json["line"].get<int>(), 2);
}

TEST(PapyrusAnalysisService, SymbolAtReportsOtherFileAsNotSameFile) {
  ScratchRoot root;
  root.write("Parent.psc", "ScriptName Parent\nFunction DoStuff()\nEndFunction\n");
  const std::string text =
      "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("DoStuff()"));

  const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, &file, offset));
  EXPECT_TRUE(json["resolved"].get<bool>());
  EXPECT_FALSE(json["sameFile"].get<bool>());
  EXPECT_TRUE(json["file"].get<std::string>().ends_with("Parent.psc"));
}

TEST(PapyrusAnalysisService, SymbolAtReportsUnresolvedAsAnswerNotError) {
  const std::string text = "ScriptName A\nFunction F()\n  GetThing().Unknown()\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Unknown"));

  const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, nullptr, offset));
  EXPECT_FALSE(json["resolved"].get<bool>());
  EXPECT_FALSE(json.contains("error"));
}

TEST(PapyrusAnalysisService, SymbolAtWorksWithoutPath) {
  const std::string text =
      "ScriptName A\nint Property Health auto\nFunction F()\n  Health = 1\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Health = 1"));

  const auto json = nlohmann::json::parse(AnalysisService::symbolAtJson(text, nullptr, offset));
  EXPECT_TRUE(json["resolved"].get<bool>());
  EXPECT_TRUE(json["sameFile"].get<bool>());
}

TEST(PapyrusAnalysisService, CompletionAfterDotListsMembersUpExtendsChain) {
  ScratchRoot root;
  root.write("Parent.psc",
             "ScriptName Parent\nInt Property Health Auto\nFunction DoStuff()\nEndFunction\n");
  const std::string text =
      "ScriptName Child extends Parent\nParent Property Link Auto\n"
      "Function F()\n  Link.\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("Link.") + 5);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  EXPECT_EQ(json["context"].get<std::string>(), "member");
  EXPECT_EQ(json["replaceLength"].get<int>(), 0);
  const auto labels = labelsOf(json);
  EXPECT_TRUE(labels.contains("DoStuff"));
  EXPECT_TRUE(labels.contains("Health"));
}

TEST(PapyrusAnalysisService, CompletionAfterDotOnStructListsItsFields) {
  const std::string text =
      "ScriptName A\nStruct Pair\n  Int Left\n  Int Right\nEndStruct\nFunction F()\n  Pair myPair\n"
      "  myPair.\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("myPair.") + 7);

  const auto labels = labelsOf(
      nlohmann::json::parse(AnalysisService::completionJson(text, nullptr, offset)));
  EXPECT_TRUE(labels.contains("Left"));
  EXPECT_TRUE(labels.contains("Right"));
  EXPECT_FALSE(labels.contains("Find"));
}

TEST(PapyrusAnalysisService, CompletionAfterDotOnArrayListsBuiltIns) {
  const std::string text = "ScriptName A\nFunction F()\n  Int[] values\n  values.\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("values.") + 7);

  const auto labels = labelsOf(
      nlohmann::json::parse(AnalysisService::completionJson(text, nullptr, offset)));
  EXPECT_TRUE(labels.contains("Find"));
  EXPECT_TRUE(labels.contains("Add"));
  EXPECT_TRUE(labels.contains("Length"));
}

TEST(PapyrusAnalysisService, CompletionAfterUnfinishedScriptNameDotOffersOnlyGlobals) {
  ScratchRoot root;
  root.write("Parent.psc",
             "ScriptName Parent\nFunction Make() global\nEndFunction\nFunction DoStuff()\n"
             "EndFunction\n");
  const std::string text = "ScriptName Child\nFunction F()\n  Parent.\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("Parent.") + 7);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  const auto labels = labelsOf(json);
  EXPECT_TRUE(labels.contains("Make"));
  EXPECT_FALSE(labels.contains("DoStuff"));
}

TEST(PapyrusAnalysisService, CompletionAfterExtendsOffersScriptNames) {
  ScratchRoot root;
  root.write("Parent.psc", "ScriptName Parent\n");
  const std::string text = "ScriptName Child extends \nFunction F()\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("extends ") + 8);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  EXPECT_EQ(json["context"].get<std::string>(), "extends");
  EXPECT_TRUE(labelsOf(json).contains("Parent"));
}

TEST(PapyrusAnalysisService, CompletionInGeneralScopeListsParametersAndLocalsBeforeCaret) {
  const std::string text =
      "ScriptName A\nFunction F(Int count)\n  Int total = 0\n  \nEndFunction\n";
  const auto offset = static_cast<int>(text.find("  \nEndFunction") + 2);

  const auto labels = labelsOf(
      nlohmann::json::parse(AnalysisService::completionJson(text, nullptr, offset)));
  EXPECT_TRUE(labels.contains("count"));
  EXPECT_TRUE(labels.contains("total"));
}

TEST(PapyrusAnalysisService, CompletionMatchesTypedPrefixCaseInsensitively) {
  const std::string text =
      "ScriptName A\nInt Property Health Auto\nFunction DoStuff()\nEndFunction\n"
      "Function F()\n  hEa\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("hEa") + 3);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, nullptr, offset));
  EXPECT_EQ(json["replaceStart"].get<int>(), offset - 3);
  EXPECT_EQ(json["replaceLength"].get<int>(), 3);
  const auto labels = labelsOf(json);
  EXPECT_TRUE(labels.contains("Health"));
  EXPECT_FALSE(labels.contains("DoStuff"));
}

TEST(PapyrusAnalysisService, SignatureHelpCountsCommasToActiveParameter) {
  const std::string text =
      "ScriptName A\nFunction Pick(Int first, Int second, Int third)\nEndFunction\n"
      "Function F()\n  Pick(1, 2, \nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Pick(1, 2, ") + 11);

  const auto json =
      nlohmann::json::parse(AnalysisService::signatureHelpJson(text, nullptr, offset));
  EXPECT_TRUE(json["active"].get<bool>());
  EXPECT_EQ(json["activeParameter"].get<int>(), 2);
  EXPECT_EQ(json["signatures"][0]["label"].get<std::string>(),
            "Function Pick(Int first, Int second, Int third)");
  EXPECT_EQ(json["signatures"][0]["parameters"].size(), 3u);
}

TEST(PapyrusAnalysisService, SignatureHelpIsInactiveOutsideAnyCall) {
  const std::string text = "ScriptName A\nFunction F()\n  Int n = 1\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("n = 1"));

  const auto json =
      nlohmann::json::parse(AnalysisService::signatureHelpJson(text, nullptr, offset));
  EXPECT_FALSE(json["active"].get<bool>());
}

TEST(PapyrusAnalysisService, CheckTextReportsNameErrorFromUnsavedBuffer) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\nFunction F()\nEndFunction\n");
  const std::string buffer = "ScriptName A\nFunction F()\n    int n = nope\nEndFunction\n";

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(buffer, &file, 7));
  EXPECT_EQ(json["revision"].get<int>(), 7);
  EXPECT_TRUE(json["complete"].get<bool>());
  EXPECT_EQ(json["errorCount"].get<int>(), 1);
  EXPECT_EQ(json["diagnostics"][0]["code"].get<std::string>(), "PAP0030");
}

TEST(PapyrusAnalysisService, CheckTextEchoesRevisionOnCleanBuffer) {
  const auto json = nlohmann::json::parse(
      AnalysisService::checkTextJson("ScriptName A\nFunction F()\nEndFunction\n", nullptr, 42));
  EXPECT_EQ(json["revision"].get<int>(), 42);
  EXPECT_TRUE(json["complete"].get<bool>());
  EXPECT_EQ(json["errorCount"].get<int>(), 0);
}

TEST(PapyrusAnalysisService, CheckTextWithMissingBaseIsIncompleteNotClean) {
  ScratchRoot root;
  const std::string text =
      "ScriptName A extends NotOnAnyRoot\nFunction F()\n    Inherited()\nEndFunction\n";
  const auto file = root.write("A.psc", text);

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(text, &file, 1));
  EXPECT_FALSE(json["complete"].get<bool>());
  EXPECT_EQ(json["errorCount"].get<int>(), 0);
}

TEST(PapyrusAnalysisService, CheckTextWithSyntaxErrorIsIncompleteAndSkipsNames) {
  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(
      "ScriptName A\nFunction F(\nEndFunction\n", nullptr, 3));
  EXPECT_FALSE(json["complete"].get<bool>());
  EXPECT_GT(json["errorCount"].get<int>(), 0);
  EXPECT_EQ(json["diagnostics"][0]["severity"].get<std::string>(), "error");
}

TEST(PapyrusAnalysisService, CompletionInsideCommentOrStringOffersNothing) {
  const std::string head = "ScriptName A\nFunction F()\n";
  for (const std::string line :
       {"  ; Foo.", "  ;/ Foo. /;", "  { Foo. }", "  Debug.Trace(\"Foo.\")"}) {
    const std::string text = head + line + "\nEndFunction\n";
    const auto offset = static_cast<int>(text.find("Foo.") + 4);

    const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, nullptr, offset));
    EXPECT_EQ(json["context"].get<std::string>(), "none") << line;
    EXPECT_TRUE(json["items"].empty()) << line;
  }
}

TEST(PapyrusAnalysisService, CompletionAfterExtendsCompletesNamespacedScriptName) {
  ScratchRoot root;
  root.write("Fragments/Quests/QF_X.psc", "ScriptName QF_X\n");
  const std::string text = "ScriptName Child extends Fragments:Qu\nFunction F()\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("Fragments:Qu") + 12);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  EXPECT_EQ(json["context"].get<std::string>(), "extends");
  EXPECT_EQ(json["replaceStart"].get<int>(), offset - 12);
  EXPECT_EQ(json["replaceLength"].get<int>(), 12);
  EXPECT_TRUE(labelsOf(json).contains("Fragments:Quests:QF_X"));
}

TEST(PapyrusAnalysisService, CompletionAfterAsOffersPrimitiveTypesAndScriptNames) {
  ScratchRoot root;
  root.write("Parent.psc", "ScriptName Parent\n");
  const std::string text = "ScriptName Child\nFunction F()\n  Form f = None as \nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("None as ") + 8);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  const auto labels = labelsOf(json);
  EXPECT_TRUE(labels.contains("Int"));
  EXPECT_TRUE(labels.contains("Float"));
  EXPECT_TRUE(labels.contains("Bool"));
  EXPECT_TRUE(labels.contains("String"));
  EXPECT_TRUE(labels.contains("Parent"));
}

TEST(PapyrusAnalysisService, CompletionAfterNewOffersInheritedStructsByOwnerName) {
  ScratchRoot root;
  root.write("Parent.psc", "ScriptName Parent\nStruct Pair\n  Int Left\nEndStruct\n");
  const std::string text =
      "ScriptName Child extends Parent\nFunction F()\n  Int[] items = new \nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("new ") + 4);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  EXPECT_EQ(json["context"].get<std::string>(), "new");
  EXPECT_TRUE(labelsOf(json).contains("Parent:Pair"));
}

TEST(PapyrusAnalysisService, CompletionInGeneralScopeOffersScriptNames) {
  ScratchRoot root;
  root.write("Parent.psc", "ScriptName Parent\n");
  const std::string text = "ScriptName Child extends Parent\nFunction F()\n  Par\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("Par\n") + 3);

  const auto json = nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset));
  EXPECT_EQ(json["replaceLength"].get<int>(), 3);
  EXPECT_TRUE(labelsOf(json).contains("Parent"));
}

TEST(PapyrusAnalysisService, CompletionOnForeignObjectOffersOnlyFunctionsAndProperties) {
  ScratchRoot root;
  root.write("Parent.psc",
             "ScriptName Parent\nInt CachedCount\nInt Property Health Auto\nFunction DoStuff()\n"
             "EndFunction\nEvent OnInit()\nEndEvent\n");
  const std::string text =
      "ScriptName Child\nParent Property Link Auto\nFunction F()\n  Link.\nEndFunction\n";
  const auto file = root.write("Child.psc", text);
  const auto offset = static_cast<int>(text.find("Link.") + 5);

  const auto labels = labelsOf(
      nlohmann::json::parse(AnalysisService::completionJson(text, &file, offset)));
  EXPECT_TRUE(labels.contains("DoStuff"));
  EXPECT_TRUE(labels.contains("Health"));
  EXPECT_FALSE(labels.contains("CachedCount"));
  EXPECT_FALSE(labels.contains("OnInit"));
}

TEST(PapyrusAnalysisService, CompletionOnSelfOffersScriptVariablesAndEvents) {
  const std::string text =
      "ScriptName A\nInt CachedCount\nEvent OnInit()\nEndEvent\nFunction F()\n  self.\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("self.") + 5);

  const auto labels = labelsOf(
      nlohmann::json::parse(AnalysisService::completionJson(text, nullptr, offset)));
  EXPECT_TRUE(labels.contains("CachedCount"));
  EXPECT_TRUE(labels.contains("OnInit"));
}

TEST(PapyrusAnalysisService, SignatureHelpSkipsEscapedQuoteInsideString) {
  const std::string text =
      "ScriptName A\nFunction Pick(String first, Int second)\nEndFunction\n"
      "Function F()\n  Pick(\"a\\\"(\", \nEndFunction\n";
  const auto offset = static_cast<int>(text.find("\", \n") + 3);

  const auto json =
      nlohmann::json::parse(AnalysisService::signatureHelpJson(text, nullptr, offset));
  EXPECT_TRUE(json["active"].get<bool>());
  EXPECT_EQ(json["activeParameter"].get<int>(), 1);
  EXPECT_EQ(json["signatures"][0]["label"].get<std::string>(),
            "Function Pick(String first, Int second)");
}

TEST(PapyrusAnalysisService, SignatureHelpIsInactiveInsideString) {
  const std::string text =
      "ScriptName A\nFunction Pick(String first, Int second)\nEndFunction\n"
      "Function F()\n  Pick(\"a, \nEndFunction\n";
  const auto offset = static_cast<int>(text.find("\"a, ") + 4);

  const auto json =
      nlohmann::json::parse(AnalysisService::signatureHelpJson(text, nullptr, offset));
  EXPECT_FALSE(json["active"].get<bool>());
}
