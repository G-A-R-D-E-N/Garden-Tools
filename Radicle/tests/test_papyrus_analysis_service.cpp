#include <fo4/papyrus/analysis_service.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

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
            "Function Pick(int first, int second, int third)");
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
            "Function Pick(string first, int second)");
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

TEST(PapyrusAnalysisService, CheckTextHintsFlagUnusedImportAndLocalWithoutErrors) {
  ScratchRoot root;
  root.write("Helper.psc", "ScriptName Helper\nFunction Help()\nEndFunction\n");
  const std::string text =
      "ScriptName A\nImport Helper\nFunction F()\n  Int unused = 1\nEndFunction\n";
  const auto file = root.write("A.psc", text);

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(text, &file, 1));
  EXPECT_TRUE(json["complete"].get<bool>());
  EXPECT_EQ(json["errorCount"].get<int>(), 0);
  ASSERT_EQ(json["hints"].size(), 2u);
  EXPECT_EQ(json["hints"][0]["code"].get<std::string>(), "PAPH001");
  EXPECT_EQ(json["hints"][1]["code"].get<std::string>(), "PAPH002");
  EXPECT_EQ(json["hints"][1]["severity"].get<std::string>(), "hint");
}

TEST(PapyrusAnalysisService, CheckTextHintsDoNotFlagImportUsedThroughQualifiedCall) {
  ScratchRoot root;
  root.write("Helper.psc", "ScriptName Helper\nFunction Help()\nEndFunction\n");
  const std::string text =
      "ScriptName A\nImport Helper\nFunction F()\n  Helper.Help()\nEndFunction\n";
  const auto file = root.write("A.psc", text);

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(text, &file, 1));
  EXPECT_TRUE(json["complete"].get<bool>());
  EXPECT_TRUE(json["hints"].empty());
}

TEST(PapyrusAnalysisService, CheckTextHintsDoNotFlagImportUsedThroughUnqualifiedCall) {
  ScratchRoot root;
  root.write("Helper.psc", "ScriptName Helper\nFunction Help() global\nEndFunction\n");
  const std::string text = "ScriptName A\nImport Helper\nFunction F()\n  Help()\nEndFunction\n";
  const auto file = root.write("A.psc", text);

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(text, &file, 1));
  EXPECT_TRUE(json["complete"].get<bool>());
  EXPECT_EQ(json["errorCount"].get<int>(), 0);
  EXPECT_TRUE(json["hints"].empty());
}

TEST(PapyrusAnalysisService, CheckTextHintsCountAssignmentAsUseAndNeverFlagParameters) {
  const std::string text = "ScriptName A\nFunction F(Int p)\n  Int n = 0\n  n = 2\nEndFunction\n";

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(text, nullptr, 1));
  EXPECT_TRUE(json["complete"].get<bool>());
  EXPECT_TRUE(json["hints"].empty());
}

TEST(PapyrusAnalysisService, CheckTextWithIncompleteBaseWithholdsHints) {
  const std::string text =
      "ScriptName A extends NotOnAnyRoot\nFunction F()\n  Int unused = 1\nEndFunction\n";

  const auto json = nlohmann::json::parse(AnalysisService::checkTextJson(text, nullptr, 1));
  EXPECT_FALSE(json["complete"].get<bool>());
  EXPECT_TRUE(json["hints"].empty());
}

TEST(PapyrusAnalysisService, ReferencesOfLocalStayInsideItsFunction) {
  const std::string text =
      "ScriptName A\nFunction F()\n  Int n = 1\n  Int m = n\nEndFunction\n"
      "Function G()\n  Int n = 2\n  Int k = n\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Int n = 1") + 4);

  const auto json = nlohmann::json::parse(AnalysisService::referencesJson(text, nullptr, offset));
  EXPECT_EQ(json["declaration"]["kind"].get<std::string>(), "Local");
  ASSERT_EQ(json["references"].size(), 2u);
  EXPECT_EQ(json["references"][0]["line"].get<int>(), 3);
  EXPECT_EQ(json["references"][1]["line"].get<int>(), 4);
  EXPECT_TRUE(json["references"][0]["isDeclaration"].get<bool>());
}

TEST(PapyrusAnalysisService, ReferencesOfScriptNameReachOtherScriptFiles) {
  ScratchRoot root;
  const std::string text =
      "ScriptName Helper\nStruct MyStruct\n  Int Left\nEndStruct\nFunction Help()\nEndFunction\n";
  const auto file = root.write("Helper.psc", text);
  const std::string user =
      "ScriptName User\nFunction F()\n  helper h = None\n  Helper:MyStruct s = None\n"
      "  ; Helper in a comment\n  Debug.Trace(\"Helper\")\nEndFunction\n";
  root.write("User.psc", user);
  const auto offset = static_cast<int>(text.find("Helper") + 1);

  const auto json = nlohmann::json::parse(AnalysisService::referencesJson(text, &file, offset));
  EXPECT_EQ(json["declaration"]["kind"].get<std::string>(), "Script");
  std::vector<int> userStarts;
  for (const auto& reference : json["references"]) {
    if (reference["file"].get<std::string>().ends_with("User.psc")) {
      userStarts.push_back(reference["start"].get<int>());
    }
  }
  EXPECT_EQ(json["references"].size(), 3u);
  ASSERT_EQ(userStarts.size(), 2u);
  EXPECT_TRUE(std::find(userStarts.begin(), userStarts.end(),
                        static_cast<int>(user.find("helper h"))) != userStarts.end());
  EXPECT_TRUE(std::find(userStarts.begin(), userStarts.end(),
                        static_cast<int>(user.find("Helper:MyStruct"))) != userStarts.end());
}

TEST(PapyrusAnalysisService, ReferencesWithForwardSlashBufferPathDoNotDuplicateTheDiskCopy) {
  ScratchRoot root;
  const std::string text = "ScriptName Helper\nFunction Help()\nEndFunction\n";
  const auto file = root.write("Helper.psc", text);
  root.write("User.psc", "ScriptName User\nFunction F()\n  Helper h = None\nEndFunction\n");
  std::string forward = file;
  std::replace(forward.begin(), forward.end(), '\\', '/');
  const std::string imports = root.path();
  const auto offset = static_cast<int>(text.find("Helper") + 1);

  const auto references = nlohmann::json::parse(
      AnalysisService::referencesJson(text, &forward, offset, &imports));
  EXPECT_EQ(references["references"].size(), 2u);

  const auto edits = nlohmann::json::parse(
      AnalysisService::renameJson(text, &forward, offset, "Helper2", &imports));
  EXPECT_TRUE(edits["ok"].get<bool>());
  EXPECT_EQ(edits["edits"].size(), 2u);
}

TEST(PapyrusAnalysisService, RenameRefusesKeywordIllegalNameAndLocalCollision) {
  const std::string text = "ScriptName A\nFunction F()\n  Int n = 1\n  Int m = 2\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Int n = 1") + 4);
  const auto refused = [&](const std::string& name) {
    return nlohmann::json::parse(AnalysisService::renameJson(text, nullptr, offset, name));
  };

  EXPECT_FALSE(refused("Int")["ok"].get<bool>());
  EXPECT_NE(refused("Int")["reason"].get<std::string>().find("keyword"), std::string::npos);
  EXPECT_NE(refused("9bad")["reason"].get<std::string>().find("identifier"), std::string::npos);
  EXPECT_NE(refused("m")["reason"].get<std::string>().find("already"), std::string::npos);
}

TEST(PapyrusAnalysisService, RenameLocalEditsOnlyItsOwnFunction) {
  const std::string text =
      "ScriptName A\nFunction F()\n  Int n = 1\n  Int m = n\nEndFunction\n"
      "Function G()\n  Int n = 2\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Int n = 1") + 4);
  const auto gStart = static_cast<int>(text.find("Function G()"));

  const auto json =
      nlohmann::json::parse(AnalysisService::renameJson(text, nullptr, offset, "total"));
  EXPECT_TRUE(json["ok"].get<bool>());
  ASSERT_EQ(json["edits"].size(), 2u);
  for (const auto& edit : json["edits"]) {
    EXPECT_LT(edit["start"].get<int>(), gStart);
    EXPECT_EQ(edit["newText"].get<std::string>(), "total");
  }
  EXPECT_TRUE(json["fileRename"].is_null());
}

TEST(PapyrusAnalysisService, RenameScriptRenamesItsFileAndEditsReferencingScripts) {
  ScratchRoot root;
  const std::string text =
      "ScriptName Helper\nStruct MyStruct\n  Int Left\nEndStruct\nFunction Help()\nEndFunction\n";
  const auto file = root.write("Helper.psc", text);
  const std::string user =
      "ScriptName User\nFunction F()\n  helper h = None\n  Helper:MyStruct s = None\n"
      "  ; Helper in a comment\n  Debug.Trace(\"Helper\")\nEndFunction\n";
  root.write("User.psc", user);
  const auto offset = static_cast<int>(text.find("Helper") + 1);

  const auto json =
      nlohmann::json::parse(AnalysisService::renameJson(text, &file, offset, "Helper2"));
  EXPECT_TRUE(json["ok"].get<bool>());
  EXPECT_TRUE(json["fileRename"]["to"].get<std::string>().ends_with("Helper2.psc"));

  int declarations = 0;
  std::map<int, int> userEdits;
  for (const auto& edit : json["edits"]) {
    EXPECT_EQ(edit["newText"].get<std::string>(), "Helper2");
    const auto editFile = edit["file"].get<std::string>();
    const auto start = edit["start"].get<int>();
    if (editFile == file && start == static_cast<int>(text.find("Helper"))) ++declarations;
    if (editFile.ends_with("User.psc")) userEdits[start] = edit["length"].get<int>();
  }
  EXPECT_EQ(json["edits"].size(), 3u);
  EXPECT_EQ(declarations, 1);
  ASSERT_EQ(userEdits.size(), 2u);
  EXPECT_EQ(userEdits.at(static_cast<int>(user.find("helper h"))), 6);
  EXPECT_EQ(userEdits.at(static_cast<int>(user.find("Helper:MyStruct"))), 6);
}

TEST(PapyrusAnalysisService, RenameRefusesLocalThatWouldShadowAPropertyUsedInItsFunction) {
  const std::string text =
      "ScriptName A\nInt Property Speed Auto\nFunction F()\n  Int n = 1\n  Int m = Speed\nEndFunction\n";
  const auto offset = static_cast<int>(text.find("Int n = 1") + 4);
  const auto refused =
      nlohmann::json::parse(AnalysisService::renameJson(text, nullptr, offset, "Speed"));
  EXPECT_FALSE(refused["ok"].get<bool>());
  EXPECT_NE(refused["reason"].get<std::string>().find("shadow"), std::string::npos);

  const std::string member =
      "ScriptName A\nInt Property Speed Auto\nFunction F()\n  Int n = 1\n  Int m = n.Speed\n"
      "EndFunction\n";
  const auto memberOffset = static_cast<int>(member.find("Int n = 1") + 4);
  const auto allowed =
      nlohmann::json::parse(AnalysisService::renameJson(member, nullptr, memberOffset, "Speed"));
  EXPECT_TRUE(allowed["ok"].get<bool>());
}

TEST(PapyrusAnalysisService, RenameRefusesBaseScriptOutsideTheProject) {
  ScratchRoot base;
  base.write("Base.psc", "ScriptName Base\n");
  ScratchRoot project;
  const auto file = project.write("A.psc", "ScriptName A extends Base\n");
  const std::string text = "ScriptName A extends Base\n";
  const auto offset = static_cast<int>(text.find("Base") + 1);
  const std::vector<std::filesystem::path> bases{base.path()};

  const auto json = nlohmann::json::parse(
      AnalysisService::renameJson(text, &file, offset, "Other", nullptr, bases));
  EXPECT_FALSE(json["ok"].get<bool>());
  EXPECT_NE(json["reason"].get<std::string>().find("base script"), std::string::npos);
}

TEST(PapyrusAnalysisService, WorkspaceSymbolsRanksExactBeforePrefix) {
  ScratchRoot root;
  root.write("Alpha.psc",
             "ScriptName Alpha\nFunction GetActorValue()\nEndFunction\nFunction GetActor()\n"
             "EndFunction\nFunction ActorGet()\nEndFunction\n");
  const std::string imports = root.path();

  const auto json =
      nlohmann::json::parse(AnalysisService::workspaceSymbolsJson("getactor", 100, &imports));
  ASSERT_EQ(json["results"].size(), 2u);
  EXPECT_EQ(json["results"][0]["name"].get<std::string>(), "GetActor");
  EXPECT_EQ(json["results"][1]["name"].get<std::string>(), "GetActorValue");
  EXPECT_FALSE(json["truncated"].get<bool>());
}

TEST(PapyrusAnalysisService, WorkspaceSymbolsMatchInitialsAtWordStarts) {
  ScratchRoot root;
  root.write("Alpha.psc",
             "ScriptName Alpha\nFunction GetActorValue()\nEndFunction\nFunction ActorGet()\n"
             "EndFunction\n");
  const std::string imports = root.path();

  const auto json =
      nlohmann::json::parse(AnalysisService::workspaceSymbolsJson("gav", 100, &imports));
  ASSERT_EQ(json["results"].size(), 1u);
  EXPECT_EQ(json["results"][0]["name"].get<std::string>(), "GetActorValue");
}

TEST(PapyrusAnalysisService, WorkspaceSymbolsReportsTruncationAtLimit) {
  ScratchRoot root;
  root.write("Alpha.psc",
             "ScriptName Alpha\nFunction GetActorValue()\nEndFunction\nFunction GetActor()\n"
             "EndFunction\n");
  const std::string imports = root.path();

  const auto json =
      nlohmann::json::parse(AnalysisService::workspaceSymbolsJson("getactor", 1, &imports));
  EXPECT_TRUE(json["truncated"].get<bool>());
  ASSERT_EQ(json["results"].size(), 1u);
  EXPECT_EQ(json["results"][0]["name"].get<std::string>(), "GetActor");
}
