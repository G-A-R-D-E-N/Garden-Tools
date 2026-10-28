#include <fo4/papyrus/declaration_check.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/file_walk.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/resolver.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/symbols.hpp>
#include <fo4/papyrus/type_checker.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kVanillaFiles = 7854;
constexpr std::size_t kVanillaComplete = 7832;

std::vector<std::filesystem::path> corpus_roots(const char* variable) {
  std::vector<std::filesystem::path> roots;
  const char* configured = std::getenv(variable);
  if (configured == nullptr || *configured == '\0') return roots;
  std::istringstream stream(configured);
  std::string part;
  while (std::getline(stream, part, ';')) {
    if (!part.empty() && std::filesystem::is_directory(part)) roots.emplace_back(part);
  }
  return roots;
}

std::vector<std::filesystem::path> papyrus_files_in(
    const std::vector<std::filesystem::path>& roots) {
  std::vector<std::filesystem::path> files;
  for (const auto& root : roots) {
    auto found = fo4::papyrus::enumeratePapyrusFiles(root);
    files.insert(files.end(), found.begin(), found.end());
  }
  return files;
}

}

TEST(PapyrusSemanticReal, YieldsSymbolsAcrossTheCorpusWithoutCrashing) {
  const auto roots = corpus_roots("FO4RE_PSC_CORPUS");
  if (roots.empty()) GTEST_SKIP() << "FO4RE_PSC_CORPUS is not set; corpus sweep skipped.";

  const auto files = papyrus_files_in(roots);
  fo4::papyrus::ScriptIndex index;
  for (const auto& root : roots) index.addRoot(root);

  std::int64_t symbols = 0;
  for (const auto& file : files) {
    std::optional<fo4::papyrus::Script> script;
    try {
      script = fo4::papyrus::Parser::parseFile(file);
    } catch (const std::exception& ex) {
      FAIL() << "Parsing threw on " << file.string() << ": " << ex.what();
    }
    if (!script) continue;

    symbols += static_cast<std::int64_t>(fo4::papyrus::Symbols::documentSymbols(*script).size());

    const auto length = std::max(1, script->span.end());
    for (auto offset = 0; offset < length; offset += std::max(1, static_cast<int>(length) / 25)) {
      (void)fo4::papyrus::Symbols::hover(index, *script, offset);
    }
  }

  ASSERT_GT(symbols, 0) << files.size() << " files";
}

TEST(PapyrusSemanticReal, ResolvesAndTypeChecksEveryCompleteCorpusScriptClean) {
  const auto roots = corpus_roots("FO4RE_PSC_CORPUS");
  if (roots.empty()) GTEST_SKIP() << "FO4RE_PSC_CORPUS is not set; corpus sweep skipped.";

  const auto files = papyrus_files_in(roots);
  fo4::papyrus::ScriptIndex index;
  for (const auto& root : roots) index.addRoot(root);

  std::size_t complete = 0;
  std::vector<std::string> problems;
  for (const auto& file : files) {
    const fo4::papyrus::Script* script = index.parseCached(file);
    if (script == nullptr) continue;
    const auto resolution = fo4::papyrus::Resolver(index).resolve(script);
    if (!resolution.baseChainComplete) continue;
    ++complete;

    if (script->hasErrors()) problems.push_back(file.filename().string() + ": does not parse clean");
    for (const auto& d : resolution.diagnostics) {
      problems.push_back(file.filename().string() + ": " + d.toString());
    }
    for (const auto& d : fo4::papyrus::TypeChecker(index).check(resolution)) {
      problems.push_back(file.filename().string() + ": " + d.toString());
    }
  }

  ASSERT_GT(complete, 0u) << files.size() << " files";
  if (files.size() == kVanillaFiles) {
    EXPECT_EQ(complete, kVanillaComplete);
  }
  std::string flagged;
  for (const auto& line : problems) flagged += "\n  " + line;
  EXPECT_TRUE(problems.empty()) << complete << " complete scripts;" << flagged;
}

TEST(PapyrusSemanticReal, DuplicateDeclarationCheckFlagsNothingInTheVanillaSources) {
  const auto roots = corpus_roots("FO4RE_PSC_ROOTS");
  if (roots.empty()) GTEST_SKIP() << "FO4RE_PSC_ROOTS is not set; sweep skipped.";

  const auto files = papyrus_files_in(roots);
  std::size_t parsed = 0;
  std::size_t unparseable = 0;
  std::vector<std::string> offenders;
  for (const auto& file : files) {
    std::optional<fo4::papyrus::Script> script;
    try {
      script = fo4::papyrus::Parser::parseFile(file);
    } catch (...) {
      ++unparseable;
      continue;
    }
    if (!script || script->hasErrors()) {
      ++unparseable;
      continue;
    }
    ++parsed;

    for (const auto& problem : fo4::papyrus::declarationCheck(*script)) {
      offenders.push_back(file.filename().string() + ": " + problem.message);
    }
  }

  ASSERT_GT(parsed, 0u) << files.size() << " files";
  std::string flagged;
  for (const auto& line : offenders) flagged += "\n  FLAGGED " + line;
  EXPECT_TRUE(offenders.empty()) << "files=" << files.size() << " parsed=" << parsed
                                 << " unparseable=" << unparseable << flagged;
}
