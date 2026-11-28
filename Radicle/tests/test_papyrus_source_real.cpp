#include <fo4/c_api.h>
#include <fo4/c_api_papyrus_source.h>
#include <fo4/papyrus_source/parser.hpp>
#include <fo4/papyrus_source/summary.hpp>

#include "papyrus_corpus_roots.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
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

namespace {

std::filesystem::path find_corpus_script(const std::vector<std::filesystem::path>& files, const char* name) {
  for (const std::filesystem::path& path : files) {
    if (path.filename() == name) return path;
  }
  return {};
}

std::string folded(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return std::tolower(ch); });
  return text;
}

std::string trimmed(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return std::string();
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string param_name_of(std::string param) {
  param = trimmed(param.substr(0, param.find('=')));
  const std::size_t space = param.find_last_of(" \t");
  return space == std::string::npos ? param : param.substr(space + 1);
}

std::vector<std::string> source_event_signatures(const std::string& source) {
  std::vector<std::string> out;
  std::size_t line_start = 0;
  while (line_start < source.size()) {
    std::size_t line_end = source.find('\n', line_start);
    if (line_end == std::string::npos) line_end = source.size();
    const std::size_t first = source.find_first_not_of(" \t\r", line_start);
    if (first != std::string::npos && first < line_end) {
      const std::string word = folded(source.substr(first, 12));
      std::size_t keyword = 0;
      if (word.rfind("event ", 0) == 0 || word.rfind("event\t", 0) == 0) keyword = 5;
      if (word.rfind("customevent ", 0) == 0 || word.rfind("customevent\t", 0) == 0) keyword = 11;
      if (keyword != 0) {
        const std::size_t open = source.find('(', first);
        const bool has_params = open != std::string::npos && open < line_end;
        const std::size_t name_end = has_params ? open : line_end;
        std::string signature = folded(trimmed(source.substr(first + keyword, name_end - first - keyword))) + "(";
        if (has_params) {
          const std::size_t close = source.find(')', open);
          std::string list = close == std::string::npos ? std::string() : source.substr(open + 1, close - open - 1);
          std::replace(list.begin(), list.end(), '\n', ' ');
          std::istringstream parts(list);
          std::string part;
          bool any = false;
          while (std::getline(parts, part, ',')) {
            if (trimmed(part).empty()) continue;
            signature += (any ? "," : "") + folded(param_name_of(part));
            any = true;
          }
        }
        out.push_back(signature + ")");
      }
    }
    line_start = line_end + 1;
  }
  return out;
}

}  // namespace

TEST(PapyrusSourceReal, EventCatalogCoversEveryEventDeclaredInActorAndObjectReference) {
  const std::vector<std::filesystem::path> files = corpus_files();
  if (files.empty()) {
    GTEST_SKIP() << "no .psc corpus found; set FO4_PSC_CORPUS to a directory of scripts";
  }
  const std::filesystem::path actor = find_corpus_script(files, "Actor.psc");
  const std::filesystem::path reference = find_corpus_script(files, "ObjectReference.psc");
  if (actor.empty() || reference.empty()) {
    GTEST_SKIP() << "Actor.psc and ObjectReference.psc are not both in the corpus";
  }
  const std::pair<std::filesystem::path, radicle_event_owner_kind> cases[] = {
      {actor, RADICLE_EVENT_OWNER_ACTOR},
      {reference, RADICLE_EVENT_OWNER_OBJECT_REFERENCE},
  };
  for (const auto& [path, owner] : cases) {
    const std::string source = read_text(path);
    ASSERT_FALSE(source.empty()) << path.string();
    radicle_event_catalog* catalog = nullptr;
    ASSERT_EQ(radicle_papyrus_event_catalog_parse(reinterpret_cast<const std::uint8_t*>(source.data()), source.size(),
                                                  &catalog),
              RADICLE_STATUS_OK)
        << path.string() << ": " << radicle_last_error();
    ASSERT_NE(catalog, nullptr);
    const size_t count = radicle_papyrus_event_catalog_count(catalog);
    std::vector<std::string> expected = source_event_signatures(source);
    std::vector<std::string> actual;
    EXPECT_EQ(count, expected.size()) << path.string();
    for (size_t index = 0; index < count; ++index) {
      radicle_event_info info{};
      ASSERT_EQ(radicle_papyrus_event_catalog_get(catalog, index, &info), RADICLE_STATUS_OK) << path.string();
      std::string signature = folded(info.name) + "(";
      for (size_t param_index = 0; param_index < info.param_count; ++param_index) {
        radicle_event_param param{};
        ASSERT_EQ(radicle_papyrus_event_catalog_param(catalog, index, param_index, &param), RADICLE_STATUS_OK)
            << path.string();
        signature += (param_index == 0 ? "" : ",") + folded(param.name);
      }
      actual.push_back(signature + ")");
      EXPECT_EQ(info.owner_kind, owner) << path.string() << " event " << index;
      EXPECT_EQ(std::string(info.declaring_script), path.stem().string()) << path.string();
    }
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected) << path.string();
    radicle_papyrus_event_catalog_destroy(catalog);
  }
}
