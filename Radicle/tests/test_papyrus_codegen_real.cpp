#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/pex/pex.hpp>

#include "papyrus_corpus_roots.hpp"
#include "papyrus_pex_comparer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace fp = fo4::papyrus;
namespace pex = fo4::pex;
namespace helper = fo4::papyrus::test;

namespace {

constexpr double kMinimumIdenticalFraction = 0.95;
constexpr double kMinimumDecompileFidelity = 0.802;
constexpr std::size_t kDecompileSampleLimit = 1500;

struct Pair {
  fs::path pex;
  fs::path psc;
};

struct Group {
  int count{0};
  std::string first;
};

void addTo(std::map<std::string, Group>& groups, const std::string& key,
           const std::string& example) {
  auto& group = groups[key];
  if (group.count == 0) group.first = example;
  ++group.count;
}

std::string envValue(const char* name) {
  const char* raw = std::getenv(name);
  return raw ? raw : "";
}

bool isFallout4(const fs::path& pex) {
  std::ifstream stream(pex, std::ios::binary);
  std::array<unsigned char, 8> head{};
  if (!stream.read(reinterpret_cast<char*>(head.data()),
                   static_cast<std::streamsize>(head.size()))) {
    return false;
  }
  const auto magic = static_cast<std::uint32_t>(head[0]) |
                     (static_cast<std::uint32_t>(head[1]) << 8) |
                     (static_cast<std::uint32_t>(head[2]) << 16) |
                     (static_cast<std::uint32_t>(head[3]) << 24);
  const auto gameId = static_cast<std::uint16_t>(head[6] | (head[7] << 8));
  return magic == 0xFA57C0DEu && head[4] == 3 && head[5] == 9 && gameId == 2;
}

std::optional<pex::PexFile> readPex(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
  try {
    return pex::PexFile::fromBytes(bytes);
  } catch (const std::runtime_error&) {
    return std::nullopt;
  }
}

std::optional<fs::path> sourceFor(const fs::path& pex) {
  std::vector<fs::path> parts;
  for (const auto& part : pex) parts.push_back(part);
  for (auto i = static_cast<std::ptrdiff_t>(parts.size()) - 2; i >= 0; --i) {
    const auto split = static_cast<std::size_t>(i) + 1;
    fs::path anchor;
    for (std::size_t j = 0; j < split; ++j) anchor /= parts[j];

    std::size_t start = split;
    if (parts.size() > split + 1 && helper::cmpIequals(parts[split].string(), "Compiled")) {
      start = split + 1;
    }
    fs::path relative;
    for (std::size_t j = start; j < parts.size(); ++j) relative /= parts[j];
    if (relative.has_root_directory()) continue;

    const auto name = relative.string();
    const fs::path source = name.substr(0, name.size() - 4) + ".psc";
    for (const auto& candidate :
         {anchor / "Source" / "User" / source, anchor / "Source" / source}) {
      std::error_code error;
      if (fs::is_regular_file(candidate, error)) return candidate;
    }
  }
  return std::nullopt;
}

std::vector<Pair> pairsFrom(const std::vector<fs::path>& roots) {
  std::vector<Pair> pairs;
  std::set<std::string> seen;
  for (const auto& root : roots) {
    for (const auto& pex : helper::enumerateFiles(root, ".pex")) {
      const auto psc = sourceFor(pex);
      if (!psc || !isFallout4(pex)) continue;
      if (!seen.insert(helper::cmpLower(pex.filename().string())).second) continue;
      pairs.push_back({pex, *psc});
    }
  }
  return pairs;
}

std::vector<fs::path> rootsFor(const fs::path& psc) {
  const auto directory = psc.parent_path();
  std::vector<fs::path> parts;
  for (const auto& part : directory) parts.push_back(part);
  for (std::size_t i = parts.size(); i-- > 0;) {
    if (!helper::cmpIequals(parts[i].string(), "Source")) continue;
    fs::path source;
    for (std::size_t j = 0; j <= i; ++j) source /= parts[j];
    std::vector<fs::path> roots;
    std::error_code error;
    if (fs::is_directory(source / "User", error)) roots.push_back(source / "User");
    roots.push_back(source);
    return roots;
  }
  return {directory};
}

std::string refusalKey(const fp::PapyrusCompileResult& result) {
  std::string key = "no error diagnostic";
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.severity == fp::Severity::Error) {
      key = std::string(fp::codeString(diagnostic.code));
      break;
    }
  }
  if (!result.sourcesComplete) key += " (incomplete sources)";
  return key;
}

std::string firstErrorCode(const fp::PapyrusCompileResult& result) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.severity == fp::Severity::Error) {
      return std::string(fp::codeString(diagnostic.code));
    }
  }
  return "unknown";
}

const pex::PexProperty* findProperty(const pex::PexObject& object, const std::string& name) {
  for (const auto& property : object.properties) {
    if (helper::cmpIequals(property.name, name)) return &property;
  }
  return nullptr;
}

std::string handlerDifference(const pex::PexObject& theirs, const pex::PexObject& ours) {
  for (const auto& property : theirs.properties) {
    const auto* mine = findProperty(ours, property.name);
    if (!mine) continue;
    if (property.readHandler && mine->readHandler) {
      const auto difference =
          helper::functionDifference(*property.readHandler, *mine->readHandler);
      if (!difference.empty()) return property.name + ".Get: " + difference;
    }
    if (property.writeHandler && mine->writeHandler) {
      const auto difference =
          helper::functionDifference(*property.writeHandler, *mine->writeHandler);
      if (!difference.empty()) return property.name + ".Set: " + difference;
    }
  }
  return {};
}

std::string pexDifference(const pex::PexFile& reference, const pex::PexFile& ours) {
  if (reference.objects.empty() || ours.objects.empty()) return "no object";
  const auto& theirs = reference.objects.front();
  const auto& mine = ours.objects.front();
  const auto difference = helper::firstDifference(theirs, mine);
  return difference.empty() ? handlerDifference(theirs, mine) : difference;
}

std::string differenceClass(const std::string& difference) {
  const std::pair<std::string_view, std::string_view> rules[] = {
      {" original: ", "instruction"},
      {"parent '", "parent class"},
      {"missing state", "missing state"},
      {"missing function", "missing function"},
      {"extra function", "extra function"},
      {"missing property", "missing property"},
      {"user flags", "property user flags"},
      {" flags", "property flags"},
      {"native flag", "native flag"},
      {"global flag", "global flag"},
      {"returns ", "return type"},
      {"parameter count", "parameter count"},
      {"instructions vs", "instruction count"},
      {"no object", "no object"},
  };
  for (const auto& [needle, label] : rules) {
    if (difference.find(needle) != std::string::npos) return std::string(label);
  }
  return "other";
}

}  // namespace

TEST(PapyrusCodegenReal, CompiledOutputMatchesCreationKitInstructionForInstruction) {
  auto pexRoots = helper::rootsFromEnv("FO4RE_PEX_CORPUS");
  if (pexRoots.empty()) pexRoots = helper::rootsFromEnv("FO4_PEX_CORPUS");
  if (pexRoots.empty()) {
    helper::skipOrFailPexCorpus("FO4RE_PEX_CORPUS is not set to a directory holding .pex files");
    return;
  }

  auto extra = helper::rootsFromEnv("FO4RE_PEX_IMPORT_ROOTS");
  if (extra.empty()) extra = helper::rootsFromEnv("FO4RE_PSC_ROOTS");
  const auto release = envValue("FO4RE_PEX_RELEASE") == "1";
  const auto pairs = pairsFrom(pexRoots);

  std::cout << "mode=" << (release ? "release" : "debug")
            << " (set FO4RE_PEX_RELEASE=1 for a vanilla corpus)\n";
  if (pairs.empty()) {
    helper::skipOrFailPexCorpus("FO4RE_PEX_CORPUS holds no .psc/.pex pairs");
    return;
  }

  fp::PapyrusCompileOptions options;
  options.emitDebugOnlyCode = !release;
  options.emitBetaOnlyCode = !release;

  int compiled = 0;
  int refused = 0;
  int identical = 0;
  std::map<std::string, Group> refusals;
  std::map<std::string, Group> differences;

  // Invariant: ScriptIndex holds no per-compile state, only its roots and a path-keyed parse cache.
  std::map<std::string, fp::ScriptIndex> indexes;
  const auto indexFor = [&indexes](const std::vector<fs::path>& roots) -> fp::ScriptIndex& {
    std::string key;
    for (const auto& root : roots) key += fs::absolute(root).lexically_normal().string() + ';';
    const auto [entry, added] = indexes.try_emplace(key);
    if (added) {
      for (const auto& root : roots) entry->second.addRoot(root);
    }
    return entry->second;
  };

  for (const auto& pair : pairs) {
    const auto name = pair.psc.filename().string();
    auto roots = rootsFor(pair.psc);
    roots.insert(roots.end(), extra.begin(), extra.end());
    auto& index = indexFor(roots);

    const auto* script = index.parseCached(pair.psc);
    if (!script) {
      addTo(refusals,
            std::string(fp::codeString(fp::DiagnosticCode::CannotEmit)) + " (incomplete sources)",
            name);
      ++refused;
      continue;
    }

    const auto result = fp::compileScript(
        index, script, fs::absolute(pair.psc).lexically_normal().string(), options);
    if (!result.success()) {
      addTo(refusals, refusalKey(result), name);
      ++refused;
      continue;
    }
    ++compiled;

    const auto reference = readPex(pair.pex);
    if (!reference) continue;
    const auto difference = pexDifference(*reference, *result.pex);
    if (difference.empty()) {
      ++identical;
    } else {
      addTo(differences, differenceClass(difference), name + ": " + difference);
    }
  }

  std::cout << "pairs=" << pairs.size() << " compiled=" << compiled << " refused=" << refused
            << " identical=" << identical << '\n';
  for (const auto& [key, group] : refusals) {
    std::cout << "  REFUSED " << key << " x" << group.count << ": " << group.first << '\n';
  }
  for (const auto& [key, group] : differences) {
    std::cout << "  DIFF " << key << " x" << group.count << ": " << group.first << '\n';
  }

  SCOPED_TRACE("compiled=" + std::to_string(compiled) + " identical=" + std::to_string(identical));
  ASSERT_TRUE(compiled > 0);
  ASSERT_TRUE(static_cast<double>(identical) / static_cast<double>(compiled) >=
              kMinimumIdenticalFraction);
}

TEST(PapyrusCodegenReal, DecompiledCorpusRecompilesToTheSameProgram) {
  const auto roots = helper::rootsFromEnv("FO4RE_PSC_ROOTS");
  if (roots.empty()) {
    GTEST_SKIP() << "FO4RE_PSC_ROOTS is not set to a directory holding .psc sources";
  }

  fp::ScriptIndex index;
  std::vector<fs::path> files;
  for (const auto& root : roots) {
    index.addRoot(root);
    const auto found = helper::enumerateFiles(root, ".psc");
    files.insert(files.end(), found.begin(), found.end());
  }
  const auto candidates = files.size();
  const auto sample = helper::stratifiedSample(files, roots, kDecompileSampleLimit);
  if (!sample.files.empty()) {
    std::cout << "SAMPLE candidates=" << candidates << " selected=" << sample.files.size()
              << " stride=" << sample.stride << " first=" << sample.files.front().generic_string()
              << " last=" << sample.files.back().generic_string() << '\n';
  }

  fp::PapyrusCompileOptions options;
  options.emitDebugOnlyCode = true;
  options.emitBetaOnlyCode = true;

  int compiled = 0;
  int matched = 0;
  int differed = 0;
  int assembly = 0;
  int failed = 0;
  std::map<std::string, int> reasons;
  std::vector<std::string> differences;

  for (const auto& path : sample.files) {
    const auto name = path.filename().string();

    fp::PapyrusCompileResult first;
    std::vector<std::uint8_t> bytes;
    try {
      const auto* script = index.parseCached(path);
      if (!script || script->hasErrors()) continue;
      first = fp::compileScript(index, script, name, options);
      if (!first.success()) continue;
      ++compiled;
      bytes = first.pex->toBytes();
    } catch (const std::exception&) {
      continue;
    }
    const auto& original = *first.pex;

    try {
      const auto text = fp::Decompiler::toSource(pex::PexFile::fromBytes(bytes));
      if (helper::cmpLower(text).find(".code") != std::string::npos) {
        ++assembly;
        continue;
      }

      const auto reparsed = fp::Parser::parse(text, name);
      if (reparsed.hasErrors()) {
        ++failed;
        ++reasons["decompiled source did not parse"];
        continue;
      }

      const auto second = fp::compileScript(index, &reparsed, name, options);
      if (!second.success()) {
        ++failed;
        ++reasons["did not recompile: " + firstErrorCode(second)];
        continue;
      }

      const auto difference = pexDifference(original, *second.pex);
      if (difference.empty()) {
        ++matched;
      } else {
        ++differed;
        if (differences.size() < 20) differences.push_back(name + ": " + difference);
      }
    } catch (const std::exception&) {
      ++failed;
      ++reasons["decompiler threw"];
    }
  }

  const double percent = compiled == 0 ? 0.0 : matched * 100.0 / compiled;
  std::cout << "DECOMPILE files=" << sample.files.size() << " compiled=" << compiled
            << " matched=" << matched << " differed=" << differed << " assembly=" << assembly
            << " failed=" << failed << " fidelity=" << std::format("{:.1f}", percent) << "%\n";

  std::vector<std::pair<std::string, int>> ranked(reasons.begin(), reasons.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  for (std::size_t i = 0; i < ranked.size() && i < 15; ++i) {
    std::cout << "  REASON " << std::setw(5) << ranked[i].second << "  " << ranked[i].first
              << '\n';
  }
  for (const auto& difference : differences) std::cout << "  DIFF " << difference << '\n';

  SCOPED_TRACE("compiled=" + std::to_string(compiled) + " matched=" + std::to_string(matched));
  ASSERT_FALSE(sample.files.empty());
  ASSERT_TRUE(compiled > 0);
  ASSERT_TRUE(static_cast<double>(matched) / static_cast<double>(compiled) >=
              kMinimumDecompileFidelity);
}
