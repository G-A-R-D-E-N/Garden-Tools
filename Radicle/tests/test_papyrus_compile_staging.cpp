#include <fo4/papyrus/compile_staging.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {

class ScratchRoot {
public:
  ScratchRoot()
      : path_(std::filesystem::temp_directory_path() /
              ("fo4re-compile-staging-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path_);
  }

  ~ScratchRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

  std::filesystem::path write(const std::string& relative, const std::string& content = {}) const {
    const auto file = path_ / relative;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
    return file;
  }

private:
  std::filesystem::path path_;
};

std::optional<std::string> refusal(const std::vector<std::filesystem::path>& files,
                                   const std::filesystem::path& staging) {
  try {
    fo4::papyrus::stageNamespacedScripts(files, staging);
  } catch (const fo4::papyrus::CompileStagingError& error) {
    return error.what();
  }
  return std::nullopt;
}

}  // namespace

TEST(PapyrusCompileStaging, ReportsAnUnstageableNamespacePartAsAnErrorNotAsNoStaging) {
  ScratchRoot root;
  const auto source = root.write("src/Good.psc", "ScriptName Good:Foo\nFunction Ping()\nEndFunction\n");
  const auto staging = root.path() / "staging";
  root.write("staging/Good");

  const auto message = refusal({source}, staging);
  ASSERT_TRUE(message.has_value());
  EXPECT_NE(message->find("Refused to compile"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(staging));
}

TEST(PapyrusCompileStaging, RefusesAnUnsafeScriptNameAndWritesNothingOutsideStaging) {
  const std::vector<std::tuple<std::string, std::string, std::string>> rows = {
      {"ScriptName A:B?:C", "", "unsafe namespace part"},
      {"ScriptName ..:..:Evil", "../Evil.psc", "Refused to compile"},
      {"ScriptName A:..:B", "", "Refused to compile"},
      {"ScriptName MyMod:/fo4ide_gt086_escape:Evil", "/fo4ide_gt086_escape/Evil.psc", "Refused to compile"},
  };
  for (const auto& [scriptLine, escapeText, fragment] : rows) {
    SCOPED_TRACE(scriptLine);
    ScratchRoot root;
    const auto source = root.write("src/Evil.psc", scriptLine + "\nFunction Ping()\nEndFunction\n");
    const auto staging = root.path() / "staging";
    const bool hasEscape = !escapeText.empty();
    const auto escaped = hasEscape ? root.path() / escapeText : std::filesystem::path{};

    const auto message = refusal({source}, staging);
    const bool escapedWritten = hasEscape && std::filesystem::exists(escaped);
    std::error_code ec;
    if (hasEscape) std::filesystem::remove(escaped, ec);
    ASSERT_TRUE(message.has_value());
    EXPECT_NE(message->find(fragment), std::string::npos);
    if (hasEscape) EXPECT_FALSE(escapedWritten);
    EXPECT_FALSE(std::filesystem::exists(staging));
  }
}

TEST(PapyrusCompileStaging, StagesANamespacedScriptUnderItsNamespaceFolders) {
  ScratchRoot root;
  const auto source = root.write("src/Foo.psc", "ScriptName MyMod:Quests:Foo\nFunction Ping()\nEndFunction\n");
  const auto staging = root.path() / "staging";

  const auto result = fo4::papyrus::stageNamespacedScripts({source}, staging);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->string(), staging.string());
  EXPECT_TRUE(std::filesystem::is_regular_file(staging / "MyMod" / "Quests" / "Foo.psc"));
}

TEST(PapyrusCompileStaging, ResolvesTheNamespaceRootOfASingleFileOrFallsBackToItsFolder) {
  ScratchRoot root;
  const auto out = (root.path() / "out").string();

  const auto matched = root.write("A/B/Foo.psc", "ScriptName A:B:Foo\nFunction Ping()\nEndFunction\n");
  const auto matchedPlan = fo4::papyrus::planCompile(matched.string(), out, "", {});
  EXPECT_EQ(matchedPlan.workDir, root.path().string());
  EXPECT_EQ(matchedPlan.target, "A:B:Foo");

  const auto mismatched = root.write("Other/Foo.psc", "ScriptName A:B:Foo\nFunction Ping()\nEndFunction\n");
  const auto mismatchedPlan = fo4::papyrus::planCompile(mismatched.string(), out, "", {});
  EXPECT_EQ(mismatchedPlan.workDir, (root.path() / "Other").string());
  EXPECT_EQ(mismatchedPlan.target, "Foo.psc");
}

TEST(PapyrusCompileStaging, DedupesImportsCaseInsensitivelyKeepingFirstOccurrenceOrder) {
  ScratchRoot root;
  const auto source = root.write("Src/Foo.psc", "ScriptName Foo\nFunction Ping()\nEndFunction\n");

  const auto plan = fo4::papyrus::planCompile(
      source.string(), (root.path() / "out").string(),
      "\"C:/Mods/Scripts\";c:/mods/scripts;;  ;D:/Other", {"c:/MODS/scripts", "E:/Base"});
  EXPECT_EQ(plan.importArg, "C:/Mods/Scripts;D:/Other;E:/Base;" + (root.path() / "Src").string());
}
