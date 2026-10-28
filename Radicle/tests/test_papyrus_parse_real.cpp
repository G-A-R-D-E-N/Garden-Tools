#include <fo4/papyrus/parser.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// The documented malformed set must be 19 headerless scripts/modified/ fragments plus one script with two ScriptName lines.
constexpr std::size_t kDocumentedFiles = 18802;
constexpr std::size_t kDocumentedClean = 18782;
constexpr std::size_t kDocumentedHeaderless = 19;
constexpr std::size_t kDocumentedRepeated = 1;

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

bool equals_ignore_case(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

bool ends_ignore_case(std::string_view value, std::string_view suffix) {
  if (value.size() < suffix.size()) return false;
  return equals_ignore_case(value.substr(value.size() - suffix.size()), suffix);
}

// Roots are ';'-separated because ':' must stay the Windows drive separator, the variable PET reads.
std::vector<std::filesystem::path> corpus_roots() {
  std::vector<std::filesystem::path> roots;
  const char* configured = std::getenv("FO4_PSC_CORPUS");
  if (configured == nullptr || *configured == '\0') configured = std::getenv("FO4RE_PSC_CORPUS");
  if (configured != nullptr && *configured != '\0') {
    const std::string text(configured);
    std::size_t start = 0;
    for (;;) {
      const std::size_t end = text.find(';', start);
      const std::string part = end == std::string::npos ? text.substr(start) : text.substr(start, end - start);
      const std::string cleaned = trim(part);
      if (!cleaned.empty()) roots.emplace_back(cleaned);
      if (end == std::string::npos) break;
      start = end + 1;
    }
  }
  return roots;
}

std::vector<std::filesystem::path> enumerate_scripts(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> pending{root};
  std::error_code error;
  while (!pending.empty()) {
    const std::filesystem::path directory = pending.back();
    pending.pop_back();
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
      std::error_code entry_error;
      if (it->is_symlink(entry_error)) continue;
      if (it->is_directory(entry_error)) {
        pending.push_back(it->path());
      } else if (it->is_regular_file(entry_error) && ends_ignore_case(it->path().extension().string(), ".psc")) {
        files.push_back(it->path());
      }
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::vector<std::filesystem::path> corpus_files() {
  std::vector<std::filesystem::path> files;
  for (const std::filesystem::path& root : corpus_roots()) {
    const auto found = enumerate_scripts(root);
    files.insert(files.end(), found.begin(), found.end());
  }
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// A header line's first token must be ScriptName; the count recognises the two documented malformed shapes.
std::size_t header_lines(std::string_view text) {
  std::size_t count = 0;
  std::size_t position = 0;
  while (position <= text.size()) {
    const std::size_t newline = text.find('\n', position);
    const std::size_t stop = newline == std::string_view::npos ? text.size() : newline;
    std::string_view line = text.substr(position, stop - position);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const std::string trimmed = trim(std::string(line));
    const std::string_view word = std::string_view(trimmed).substr(0, 10);
    if (equals_ignore_case(word, "ScriptName")) {
      const bool token_ends =
          trimmed.size() == 10 || !std::isalnum(static_cast<unsigned char>(trimmed[10]));
      if (token_ends) ++count;
    }
    if (newline == std::string_view::npos) break;
    position = newline + 1;
  }
  return count;
}

bool under_modified_directory(const std::filesystem::path& path) {
  for (const auto& part : path) {
    if (equals_ignore_case(part.string(), "modified")) return true;
  }
  return false;
}

std::string join(const std::vector<std::string>& names) {
  std::string text;
  for (const auto& name : names) {
    if (!text.empty()) text += "; ";
    text += name;
  }
  return text;
}

}  // namespace

TEST(PapyrusParseReal, ParsesEveryCorpusScriptWithoutCrashing) {
  const std::vector<std::filesystem::path> files = corpus_files();
  if (files.empty()) {
    GTEST_SKIP() << "no .psc corpus found; set FO4_PSC_CORPUS to the corpus roots";
  }

  std::size_t clean = 0;
  std::size_t unreadable = 0;
  std::size_t crashes = 0;
  std::vector<std::filesystem::path> erroring;
  std::map<std::string, int> by_code;

  for (const std::filesystem::path& path : files) {
    std::optional<fo4::papyrus::Script> script;
    try {
      script = fo4::papyrus::Parser::parseFile(path);
    } catch (const std::exception& error) {
      ++crashes;
      ADD_FAILURE() << "parsing threw on " << path.string() << ": " << error.what();
      continue;
    }
    if (!script) {
      ++unreadable;
      continue;
    }
    if (!script->hasErrors()) {
      ++clean;
      continue;
    }
    erroring.push_back(path);
    for (const auto& diagnostic : script->diagnostics) {
      if (diagnostic.severity == fo4::papyrus::Severity::Error) {
        ++by_code[std::string(fo4::papyrus::codeString(diagnostic.code))];
      }
    }
  }

  std::vector<std::string> headerless;
  std::vector<std::string> repeated;
  std::vector<std::string> unexplained;
  for (const std::filesystem::path& path : erroring) {
    const auto text = read_text(path);
    if (!text) {
      unexplained.push_back(path.string() + " (unreadable)");
      continue;
    }
    const std::size_t headers = header_lines(*text);
    if (headers == 0 && under_modified_directory(path)) {
      headerless.push_back(path.string());
    } else if (headers >= 2) {
      repeated.push_back(path.string());
    } else {
      unexplained.push_back(path.string() + " (" + std::to_string(headers) + " ScriptName lines)");
    }
  }

  std::string codes;
  for (const auto& [code, count] : by_code) {
    if (!codes.empty()) codes += ", ";
    codes += code + " x" + std::to_string(count);
  }

  EXPECT_EQ(crashes, 0u);
  EXPECT_EQ(unreadable, 0u) << "a corpus file could not be read";
  EXPECT_EQ(clean + erroring.size(), files.size());
  const char* strict = std::getenv("FO4RE_PSC_STRICT");
  const bool strict_shape = strict != nullptr && *strict != '\0';
  const double rate = files.empty() ? 0.0 : static_cast<double>(clean) / static_cast<double>(files.size());
  EXPECT_GT(rate, 0.995);

  if (strict_shape) {
    EXPECT_TRUE(unexplained.empty())
        << "files that error but are not one of the documented malformed shapes: "
        << join(unexplained);
  }

  if (strict_shape && files.size() == kDocumentedFiles) {
    EXPECT_EQ(clean, kDocumentedClean);
    EXPECT_EQ(headerless.size(), kDocumentedHeaderless) << join(headerless);
    EXPECT_EQ(repeated.size(), kDocumentedRepeated) << join(repeated);
  }

  RecordProperty("files", static_cast<int>(files.size()));
  RecordProperty("clean", static_cast<int>(clean));
  RecordProperty("malformed", static_cast<int>(erroring.size()));
  RecordProperty("error_codes", codes.c_str());
}
