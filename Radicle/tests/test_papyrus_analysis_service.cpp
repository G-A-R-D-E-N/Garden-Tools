#include <fo4/papyrus/analysis_service.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using fo4::papyrus::AnalysisService;

namespace {

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
