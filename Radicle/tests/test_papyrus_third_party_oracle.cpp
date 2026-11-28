#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/pex/pex.hpp>

#include "papyrus_corpus_roots.hpp"
#include "papyrus_pex_comparer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace fp = fo4::papyrus;
namespace helper = fo4::papyrus::test;

namespace {

constexpr std::size_t kDefaultSampleLimit = 100;

std::string envValue(const char* name) {
  const char* raw = std::getenv(name);
  return raw ? raw : "";
}

double envFraction(const char* name) {
  const std::string text = envValue(name);
  if (text.empty()) return 0.0;
  try {
    return std::stod(text);
  } catch (const std::exception&) {
    ADD_FAILURE() << name << " must be a number, not '" << text << "'";
    return 0.0;
  }
}

std::string quote(const fs::path& path) { return "\"" + path.string() + "\""; }

std::string readText(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
  const std::string text = readText(path);
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

void replaceAll(std::string& text, const std::string& token, const std::string& value) {
  for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) {
    text.replace(at, token.size(), value);
  }
}

std::string expand(std::string pattern, const std::map<std::string, std::string>& values) {
  for (const auto& [token, value] : values) replaceAll(pattern, token, value);
  return pattern;
}

int runIn(const fs::path& directory, const std::string& command) {
#ifdef _WIN32
  return std::system(("cd /d " + quote(directory) + " && " + command).c_str());
#else
  return std::system(("cd " + quote(directory) + " && " + command).c_str());
#endif
}

class Workspace {
public:
  explicit Workspace(const std::string& name) {
    std::error_code error;
    root_ = fs::temp_directory_path() / "radicle_third_party_oracle" / name;
    fs::remove_all(root_, error);
    fs::create_directories(root_, error);
  }
  ~Workspace() {
    std::error_code error;
    fs::remove_all(root_, error);
  }
  const fs::path& root() const { return root_; }

private:
  fs::path root_;
};

struct Corpus {
  std::vector<fs::path> roots;
  std::vector<fs::path> sample;
};

Corpus loadCorpus() {
  Corpus corpus;
  corpus.roots = helper::rootsFromEnv("FO4RE_PSC_ROOTS");
  std::vector<fs::path> files;
  for (const auto& root : corpus.roots) {
    const auto found = helper::enumerateFiles(root, ".psc");
    files.insert(files.end(), found.begin(), found.end());
  }
  const std::size_t limit = helper::corpusFloor("FO4_ORACLE_LIMIT", kDefaultSampleLimit);
  corpus.sample = helper::stratifiedSample(files, corpus.roots, limit).files;
  return corpus;
}

std::string importList(const std::vector<fs::path>& roots) {
  std::string joined;
  for (const auto& root : roots) {
    if (!joined.empty()) joined += helper::rootListSeparator();
    joined += fs::absolute(root).string();
  }
  return joined;
}

std::vector<std::string> declarationFacts(const fp::Script& script) {
  std::vector<std::string> facts;
  const auto add = [&facts](const char* kind, const std::string& name, std::size_t parameters) {
    facts.push_back(std::string(kind) + ":" + helper::cmpLower(name) + "/" + std::to_string(parameters));
  };
  for (const auto& function : script.functions) add("function", function.name, function.parameters.size());
  for (const auto& event : script.events) add("event", event.name, event.parameters.size());
  for (const auto& property : script.properties) add("property", property.name, 0);
  for (const auto& state : script.states) {
    add("state", state.name, 0);
    for (const auto& function : state.functions) add("function", state.name + "." + function.name, function.parameters.size());
    for (const auto& event : state.events) add("event", state.name + "." + event.name, event.parameters.size());
  }
  std::sort(facts.begin(), facts.end());
  return facts;
}

}  // namespace

TEST(PapyrusThirdPartyOracle, CapricaAndRadicleCompileTheSameProgram) {
  const fs::path caprica = envValue("FO4_CAPRICA");
  if (caprica.empty()) GTEST_SKIP() << "set FO4_CAPRICA to a local Caprica executable to run the compiler oracle";
  const Corpus corpus = loadCorpus();
  if (corpus.sample.empty()) GTEST_SKIP() << "FO4RE_PSC_ROOTS is not set to a directory holding .psc sources";

  std::string pattern = envValue("FO4_CAPRICA_ARGS");
  if (pattern.empty()) pattern = "--game fallout4 -i \"{imports}\" -o \"{out}\" \"{src}\"";

  fp::ScriptIndex index;
  for (const auto& root : corpus.roots) index.addRoot(root);
  fp::PapyrusCompileOptions options;
  options.emitDebugOnlyCode = false;
  options.emitBetaOnlyCode = false;

  int compared = 0;
  int identical = 0;
  int toolFailed = 0;
  int radicleFailed = 0;
  std::map<std::string, int> differences;
  std::string firstDifference;

  for (const auto& source : corpus.sample) {
    const Workspace workspace("capr_" + source.stem().string());
    const std::string command = quote(caprica) + " " +
                                expand(pattern, {{"{imports}", importList(corpus.roots)},
                                                 {"{out}", workspace.root().string()},
                                                 {"{src}", fs::absolute(source).string()}}) +
                                " > " + quote(workspace.root() / "tool.log") + " 2>&1";
    const fs::path produced = workspace.root() / (source.stem().string() + ".pex");
    if (runIn(workspace.root(), command) != 0 || !fs::exists(produced)) {
      ++toolFailed;
      continue;
    }

    const auto* script = index.parseCached(source);
    if (!script || script->hasErrors()) {
      ++radicleFailed;
      continue;
    }
    const auto result = fp::compileScript(index, script, fs::absolute(source).string(), options);
    if (!result.success()) {
      ++radicleFailed;
      continue;
    }

    const auto theirs = fo4::pex::PexFile::fromBytes(readBytes(produced));
    if (theirs.objects.empty() || result.pex->objects.empty()) {
      ++radicleFailed;
      continue;
    }
    ++compared;
    const std::string difference = helper::firstDifference(theirs.objects.front(), result.pex->objects.front());
    if (difference.empty()) {
      ++identical;
    } else {
      ++differences[difference.substr(0, difference.find(':'))];
      if (firstDifference.empty()) firstDifference = source.filename().string() + ": " + difference;
    }
  }

  std::cout << "caprica oracle: sampled=" << corpus.sample.size() << " compared=" << compared
            << " identical=" << identical << " caprica_failed=" << toolFailed
            << " radicle_failed=" << radicleFailed << std::endl;
  for (const auto& [key, count] : differences) std::cout << "  DIFF " << key << " x" << count << std::endl;
  if (!firstDifference.empty()) std::cout << "  FIRST " << firstDifference << std::endl;

  ASSERT_GT(compared, 0) << "no script compiled on both sides; check FO4_CAPRICA_ARGS against the tool's real options";
  EXPECT_GE(static_cast<double>(identical) / compared, envFraction("FO4_ORACLE_MIN_IDENTICAL"));
}

TEST(PapyrusThirdPartyOracle, ChampollionAndRadicleDecompileToTheSameDeclarations) {
  const fs::path champollion = envValue("FO4_CHAMPOLLION");
  if (champollion.empty()) GTEST_SKIP() << "set FO4_CHAMPOLLION to a local Champollion executable to run the decompiler oracle";
  const Corpus corpus = loadCorpus();
  if (corpus.sample.empty()) GTEST_SKIP() << "FO4RE_PSC_ROOTS is not set to a directory holding .psc sources";

  std::string pattern = envValue("FO4_CHAMPOLLION_ARGS");
  if (pattern.empty()) pattern = "-p \"{out}\" \"{pex}\"";

  fp::ScriptIndex index;
  for (const auto& root : corpus.roots) index.addRoot(root);
  fp::PapyrusCompileOptions options;
  options.emitDebugOnlyCode = false;
  options.emitBetaOnlyCode = false;

  int compared = 0;
  int matched = 0;
  int toolFailed = 0;
  int skipped = 0;
  std::string firstDifference;

  for (const auto& source : corpus.sample) {
    const auto* script = index.parseCached(source);
    if (!script || script->hasErrors()) {
      ++skipped;
      continue;
    }
    const auto result = fp::compileScript(index, script, fs::absolute(source).string(), options);
    if (!result.success()) {
      ++skipped;
      continue;
    }

    const Workspace workspace("champ_" + source.stem().string());
    const fs::path pexPath = workspace.root() / (source.stem().string() + ".pex");
    {
      const auto bytes = result.pex->toBytes();
      std::ofstream stream(pexPath, std::ios::binary);
      stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    const fs::path out = workspace.root() / "out";
    std::error_code error;
    fs::create_directories(out, error);
    const std::string command = quote(champollion) + " " +
                                expand(pattern, {{"{out}", out.string()}, {"{pex}", pexPath.string()}}) +
                                " > " + quote(workspace.root() / "tool.log") + " 2>&1";
    fs::path decompiled;
    if (runIn(workspace.root(), command) == 0) {
      for (fs::recursive_directory_iterator it(out, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && helper::endsWithIgnoreCase(it->path().string(), ".psc")) decompiled = it->path();
      }
    }
    if (decompiled.empty()) {
      ++toolFailed;
      continue;
    }

    const auto theirs = fp::Parser::parse(readText(decompiled), decompiled.filename().string());
    const auto ours = fp::Parser::parse(fp::Decompiler::toSource(*result.pex), source.filename().string());
    if (theirs.hasErrors() || ours.hasErrors()) {
      ++toolFailed;
      continue;
    }
    ++compared;
    if (declarationFacts(theirs) == declarationFacts(ours)) {
      ++matched;
    } else if (firstDifference.empty()) {
      firstDifference = source.filename().string();
    }
  }

  std::cout << "champollion oracle: sampled=" << corpus.sample.size() << " compared=" << compared
            << " matched=" << matched << " tool_failed=" << toolFailed << " skipped=" << skipped << std::endl;
  if (!firstDifference.empty()) std::cout << "  FIRST " << firstDifference << std::endl;

  ASSERT_GT(compared, 0) << "no script decompiled on both sides; check FO4_CHAMPOLLION_ARGS against the tool's real options";
  EXPECT_GE(static_cast<double>(matched) / compared, envFraction("FO4_ORACLE_MIN_IDENTICAL"));
}
