#include <fo4/c_api.h>
#include <fo4/c_api_papyrus_analysis.h>
#include <fo4/papyrus/analysis_service.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using fo4::papyrus::AnalysisService;

namespace {

class ScratchRoot {
public:
  ScratchRoot()
      : path_(std::filesystem::temp_directory_path() /
              ("fo4re-analysis-c-api-" +
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

std::string take_text(uint8_t* bytes, size_t size) {
  std::string text =
      bytes ? std::string(reinterpret_cast<const char*>(bytes), size) : std::string();
  radicle_free_bytes(bytes);
  return text;
}

const uint8_t* as_bytes(const std::string& text) {
  return reinterpret_cast<const uint8_t*>(text.data());
}

std::vector<const char*> pointers(const std::vector<std::string>& values) {
  std::vector<const char*> out;
  for (const auto& value : values) out.push_back(value.c_str());
  return out;
}

std::string check_export(const std::string& source, bool semantic,
                         const std::vector<std::string>& base_imports = {}) {
  const auto bases = pointers(base_imports);
  uint8_t* text = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_papyrus_check(source.c_str(), semantic ? 1 : 0, nullptr, bases.data(),
                                  bases.size(), &text, &size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return take_text(text, size);
}

std::string analyze_export(const std::string& buffer, const std::string* path) {
  uint8_t* text = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_papyrus_analyze_json(as_bytes(buffer), buffer.size(),
                                         path ? path->c_str() : nullptr, &text, &size),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return take_text(text, size);
}

}  // namespace

TEST(PapyrusAnalysisCApi, LineColumnToOffsetMatchesServiceAndIsMinusOneOutsideText) {
  const std::string text = "abc\r\ndef";
  int32_t offset = 0;

  EXPECT_EQ(radicle_papyrus_line_column_to_offset(as_bytes(text), text.size(), 2, 1, &offset),
            RADICLE_STATUS_OK);
  EXPECT_EQ(offset, 5);
  EXPECT_EQ(offset, AnalysisService::lineColumnToOffset(text, 2, 1));

  EXPECT_EQ(radicle_papyrus_line_column_to_offset(as_bytes(text), text.size(), 1, 9, &offset),
            RADICLE_STATUS_OK);
  EXPECT_EQ(offset, -1);

  EXPECT_EQ(radicle_papyrus_line_column_to_offset(as_bytes(text), text.size(), 5, 1, &offset),
            RADICLE_STATUS_OK);
  EXPECT_EQ(offset, -1);
}

TEST(PapyrusAnalysisCApi, CheckReportsUnresolvedNameAsPap0030AndMatchesService) {
  ScratchRoot root;
  const auto file =
      root.write("A.psc", "ScriptName A\nFunction F()\n    int n = nope\nEndFunction\n");

  const auto text = check_export(file, true);
  EXPECT_EQ(text, AnalysisService::check(file));
  EXPECT_NE(text.find("1 with name or type errors"), std::string::npos);
  EXPECT_NE(text.find("PAP0030"), std::string::npos);
}

TEST(PapyrusAnalysisCApi, CheckWithSemanticOffSaysSyntaxOnly) {
  ScratchRoot root;
  const auto file =
      root.write("A.psc", "ScriptName A\nFunction F()\n    int n = nope\nEndFunction\n");

  const auto text = check_export(file, false);
  EXPECT_EQ(text, AnalysisService::check(file, true, false));
  EXPECT_NE(text.find("does not resolve names or check types"), std::string::npos);
  EXPECT_EQ(text.find("PAP0030"), std::string::npos);
}

TEST(PapyrusAnalysisCApi, CheckForwardsBaseImportsSoInheritedMembersResolve) {
  ScratchRoot root;
  root.write("Base/Parent.psc", "ScriptName Parent\nFunction DoStuff()\nEndFunction\n");
  const auto child = root.write(
      "Src/Child.psc", "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n");

  const auto with_base = check_export(child, true, {root.path() + "/Base"});
  EXPECT_TRUE(with_base.starts_with(
      "RESULT: 1 clean, 0 with syntax errors, 0 with name or type errors"));

  const auto without_base = check_export(child, true);
  EXPECT_NE(without_base.find("not on the roots"), std::string::npos);
}

TEST(PapyrusAnalysisCApi, OutlineAndDefinitionMatchServiceAcrossInheritance) {
  ScratchRoot root;
  root.write("Parent.psc",
             "ScriptName Parent\nFunction DoStuff()\n{Parent version}\nEndFunction\n");
  const auto child = root.write(
      "Child.psc", "ScriptName Child extends Parent\nFunction F()\n  DoStuff()\nEndFunction\n");

  uint8_t* outline = nullptr;
  size_t outline_size = 0;
  ASSERT_EQ(radicle_papyrus_outline(child.c_str(), &outline, &outline_size), RADICLE_STATUS_OK);
  EXPECT_EQ(take_text(outline, outline_size), AnalysisService::outline(child));

  uint8_t* definition = nullptr;
  size_t definition_size = 0;
  ASSERT_EQ(radicle_papyrus_definition(child.c_str(), 3, 3, nullptr, nullptr, 0, &definition,
                                       &definition_size),
            RADICLE_STATUS_OK);
  const auto found = take_text(definition, definition_size);
  EXPECT_EQ(found, AnalysisService::definition(child, 3, 3));
  EXPECT_NE(found.find("in: Parent"), std::string::npos);
  EXPECT_NE(found.find("Parent.psc(2,10)"), std::string::npos);
  EXPECT_NE(found.find("Parent version"), std::string::npos);
}

TEST(PapyrusAnalysisCApi, DefinitionRejectsPositionOutsideFileAsResultText) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\n");

  uint8_t* text = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_papyrus_definition(file.c_str(), 99, 1, nullptr, nullptr, 0, &text, &size),
            RADICLE_STATUS_OK);
  EXPECT_NE(take_text(text, size).find("outside"), std::string::npos);
}

TEST(PapyrusAnalysisCApi, AnalyzeJsonReadsBufferNotDiskAndReportsSyntaxErrors) {
  ScratchRoot root;
  const auto file = root.write("A.psc", "ScriptName A\n");

  const std::string buffer = "ScriptName A\nFunction Buffered()\nEndFunction\n";
  const auto json = nlohmann::json::parse(analyze_export(buffer, &file));
  bool found = false;
  for (const auto& symbol : json["symbols"]) found = found || symbol["name"] == "Buffered";
  EXPECT_TRUE(found);

  const std::string broken = "ScriptName A\nFunction F(\nEndFunction\n";
  EXPECT_EQ(analyze_export(broken, &file), AnalysisService::analyzeJson(broken, &file));
  const auto broken_json = nlohmann::json::parse(analyze_export(broken, &file));
  EXPECT_GT(broken_json["errorCount"].get<int>(), 0);
  EXPECT_TRUE(broken_json["diagnostics"][0]["code"].get<std::string>().starts_with("PAP"));
}

TEST(PapyrusAnalysisCApi, SymbolAtResolvesBufferOffsetAndAnswersUnresolvedWithoutError) {
  const std::string text =
      "ScriptName A\nint Property Health auto\nFunction F()\n  Health = 1\nEndFunction\n";
  const auto offset = static_cast<int32_t>(text.find("Health = 1"));

  uint8_t* resolved = nullptr;
  size_t resolved_size = 0;
  ASSERT_EQ(radicle_papyrus_symbol_at_json(as_bytes(text), text.size(), nullptr, offset, nullptr,
                                           nullptr, 0, &resolved, &resolved_size),
            RADICLE_STATUS_OK);
  const auto resolved_text = take_text(resolved, resolved_size);
  EXPECT_EQ(resolved_text, AnalysisService::symbolAtJson(text, nullptr, offset));
  const auto json = nlohmann::json::parse(resolved_text);
  EXPECT_TRUE(json["resolved"].get<bool>());
  EXPECT_EQ(json["kind"].get<std::string>(), "Property");
  EXPECT_EQ(json["line"].get<int>(), 2);

  const std::string unknown = "ScriptName A\nFunction F()\n  GetThing().Unknown()\nEndFunction\n";
  const auto unknown_offset = static_cast<int32_t>(unknown.find("Unknown"));
  uint8_t* answer = nullptr;
  size_t answer_size = 0;
  ASSERT_EQ(radicle_papyrus_symbol_at_json(as_bytes(unknown), unknown.size(), nullptr,
                                           unknown_offset, nullptr, nullptr, 0, &answer,
                                           &answer_size),
            RADICLE_STATUS_OK);
  const auto answer_text = take_text(answer, answer_size);
  EXPECT_EQ(answer_text, AnalysisService::symbolAtJson(unknown, nullptr, unknown_offset));
  EXPECT_FALSE(nlohmann::json::parse(answer_text)["resolved"].get<bool>());
}

TEST(PapyrusAnalysisCApi, CompilePathWritesPexToOutputFolderNotBesideSource) {
  ScratchRoot root;
  const auto file = root.write("Src/A.psc", "ScriptName A\nFunction F()\nEndFunction\n");
  const auto out_dir = root.path() + "/Out";

  uint8_t* text = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_papyrus_compile_path(file.c_str(), out_dir.c_str(), nullptr, 0, 0, nullptr,
                                         nullptr, 0, &text, &size),
            RADICLE_STATUS_OK);
  EXPECT_TRUE(take_text(text, size).starts_with("RESULT: 1 succeeded, 0 failed"));

  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(out_dir) / "A.pex"));
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(root.path()) / "Src" / "A.pex"));
}

TEST(PapyrusAnalysisCApi, NaturalRootsMatchServiceAndAddSourceUserFolder) {
  ScratchRoot root;
  root.write("Data/Scripts/Source/User/Keep.txt", "");
  const auto folder = root.path() + "/Data/Scripts/Source/Mod";

  char** rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_papyrus_natural_roots(folder.c_str(), &rows, &count), RADICLE_STATUS_OK);
  std::vector<std::string> exported;
  for (size_t index = 0; index < count; ++index) exported.emplace_back(rows[index]);
  radicle_free_string_array(rows, count);

  std::vector<std::string> expected;
  for (const auto& root_path : AnalysisService::naturalRootsFor(folder)) {
    expected.push_back(root_path.string());
  }
  EXPECT_EQ(exported, expected);
  EXPECT_EQ(exported.size(), 3u);
}

TEST(PapyrusAnalysisCApi, NullRequiredArgumentsAreRefusedWithOutParamsCleared) {
  uint8_t sentinel = 0;
  uint8_t* text = nullptr;
  size_t size = 0;
  auto arm = [&] {
    text = &sentinel;
    size = 7;
  };
  auto expect_refused = [&](radicle_status status) {
    EXPECT_EQ(status, RADICLE_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(text, nullptr);
    EXPECT_EQ(size, 0u);
  };

  arm();
  expect_refused(radicle_papyrus_check(nullptr, 1, nullptr, nullptr, 0, &text, &size));
  EXPECT_STREQ(radicle_last_error(), "source is required");

  arm();
  expect_refused(radicle_papyrus_outline(nullptr, &text, &size));
  arm();
  expect_refused(radicle_papyrus_definition(nullptr, 1, 1, nullptr, nullptr, 0, &text, &size));
  arm();
  expect_refused(radicle_papyrus_analyze_json(nullptr, 3, nullptr, &text, &size));
  arm();
  expect_refused(
      radicle_papyrus_symbol_at_json(nullptr, 3, nullptr, 0, nullptr, nullptr, 0, &text, &size));
  arm();
  expect_refused(radicle_papyrus_compile_path(nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr, 0,
                                              &text, &size));

  int32_t offset = 7;
  EXPECT_EQ(radicle_papyrus_line_column_to_offset(nullptr, 0, 1, 1, &offset),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(offset, 0);

  char** rows = nullptr;
  size_t count = 5;
  EXPECT_EQ(radicle_papyrus_natural_roots(nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_papyrus_outline("A.psc", nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(PapyrusAnalysisCApi, BaseImportsRefuseNullEntryAndMissingArray) {
  uint8_t* text = nullptr;
  size_t size = 0;
  const char* with_null[] = {nullptr};

  EXPECT_EQ(radicle_papyrus_check("A.psc", 1, nullptr, with_null, 1, &text, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
  EXPECT_EQ(radicle_papyrus_check("A.psc", 1, nullptr, nullptr, 2, &text, &size),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(text, nullptr);
}
