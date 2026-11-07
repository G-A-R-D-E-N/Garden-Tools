#include <fo4/c_api.h>
#include <fo4/c_api_papyrus_source.h>
#include <fo4/papyrus_source/parser.hpp>
#include <fo4/papyrus_source/summary.hpp>

#include "papyrus_corpus_roots.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace helper = fo4::papyrus::test;

namespace {

std::vector<std::filesystem::path> corpus_roots() {
  auto roots = helper::rootsFromEnv("FO4_PSC_CORPUS");
  if (roots.empty()) roots.emplace_back(std::filesystem::path(FO4_TESTDATA_DIR) / "papyrus");
  return roots;
}

std::vector<std::filesystem::path> corpus_files() {
  std::vector<std::filesystem::path> found;
  for (const std::filesystem::path& root : corpus_roots()) {
    std::error_code rootError;
    if (!std::filesystem::is_directory(root, rootError)) continue;
    std::error_code walkError;
    for (std::filesystem::recursive_directory_iterator it(root, walkError), end; it != end; it.increment(walkError)) {
      if (walkError) break;
      std::error_code entryError;
      if (!it->is_regular_file(entryError)) continue;
      if (it->path().extension() != ".psc") continue;
      found.push_back(it->path());
    }
  }
  std::sort(found.begin(), found.end());
  return found;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::string();
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(PapyrusSourceRealTest, ParsesEveryCorpusScript) {
  const std::vector<std::filesystem::path> files = corpus_files();
  if (files.empty()) {
    GTEST_SKIP() << "no .psc corpus found; set FO4_PSC_CORPUS to a directory of scripts";
  }
  std::size_t functions = 0;
  std::size_t parsed = 0;
  for (const std::filesystem::path& path : files) {
    const std::string source = read_text(path);
    if (source.empty()) continue;
    const fo4::psc::ParseResult result = fo4::psc::parse(source);
    ASSERT_TRUE(result.ok()) << path.string() << ": " << result.error.message << " at line "
                             << result.error.position.line << " column " << result.error.position.column;
    ASSERT_FALSE(result.script.name.empty()) << path.string();
    const fo4::psc::ScriptSummary summary = fo4::psc::summarize(result.script);
    functions += summary.functions.size();
    ++parsed;
  }
  EXPECT_GT(parsed, 0u);
  EXPECT_GT(functions, 0u);
}

TEST(PapyrusSourceRealTest, CApiSummaryMatchesTheParserOnEveryCorpusScript) {
  const std::vector<std::filesystem::path> files = corpus_files();
  if (files.empty()) {
    GTEST_SKIP() << "no .psc corpus found; set FO4_PSC_CORPUS to a directory of scripts";
  }
  std::size_t checked = 0;
  for (const std::filesystem::path& path : files) {
    const std::string source = read_text(path);
    if (source.empty()) continue;
    char** rows = nullptr;
    size_t count = 0;
    ASSERT_EQ(radicle_papyrus_source_summary(reinterpret_cast<const std::uint8_t*>(source.data()), source.size(),
                                             &rows, &count),
              RADICLE_STATUS_OK)
        << path.string() << ": " << radicle_last_error();
    ASSERT_GT(count, 0u) << path.string();
    const std::string first(rows[0]);
    const fo4::psc::ParseResult direct = fo4::psc::parse(source);
    if (direct.ok()) {
      const fo4::psc::ScriptSummary summary = fo4::psc::summarize(direct.script);
      EXPECT_EQ(first, "script\t" + summary.name + "\t" + summary.extends + "\t" + summary.autoState)
          << path.string();
      EXPECT_EQ(count, 1u + summary.imports.size() + summary.declaredProperties.size() +
                           summary.states.size() + summary.functions.size())
          << path.string();
    } else {
      const std::string prefix = "error\t" + std::to_string(direct.error.position.line) + "\t" +
                                 std::to_string(direct.error.position.column) + "\t";
      EXPECT_EQ(first.rfind(prefix, 0), 0u) << path.string() << ": " << first;
    }
    radicle_free_string_array(rows, count);
    ++checked;
  }
  EXPECT_GT(checked, 0u);
}
