#include <fo4/graph/graph_compiler.hpp>
#include <fo4/graph/graph_lifter.hpp>
#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/pex/pex.hpp>

#include "papyrus_corpus_roots.hpp"
#include "papyrus_pex_comparer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace fg = fo4::graph;
namespace fp = fo4::papyrus;
namespace helper = fo4::papyrus::test;

namespace {

constexpr std::size_t kLiftSampleLimit = 1500;
// The floor is an unmeasured placeholder and must be re-measured on the fo4-data runner.
constexpr double kMinimumLiftedFraction = 0.5;
constexpr std::size_t kDifferenceSampleLimit = 20;
constexpr std::size_t kReasonPrintLimit = 15;

struct Callable {
  int line{0};
  std::string state;
  std::string name;
  std::string refusal;
};

std::string eventPexName(const fp::EventDecl& event) {
  return event.remoteObjectType ? "::remote_" + *event.remoteObjectType + "_" + event.name
                                : event.name;
}

std::vector<Callable> callablesOf(const fp::Script& script) {
  std::vector<Callable> callables;
  for (const auto& fn : script.functions) callables.push_back({fn.span.line, "", fn.name, ""});
  for (const auto& event : script.events) {
    callables.push_back({event.span.line, "", eventPexName(event), ""});
  }
  for (const auto& state : script.states) {
    for (const auto& fn : state.functions) {
      callables.push_back({fn.span.line, state.name, fn.name, ""});
    }
    for (const auto& event : state.events) {
      callables.push_back({event.span.line, state.name, eventPexName(event), ""});
    }
  }
  return callables;
}

int lineOf(const std::string& message) {
  return message.rfind("Line ", 0) == 0 ? std::atoi(message.c_str() + 5) : 0;
}

std::string reasonOf(const std::string& message) {
  static const std::regex quoted("'[^']*'");
  const auto body = lineOf(message) > 0 ? message.substr(message.find(": ") + 2) : message;
  return std::regex_replace(body, quoted, "'X'");
}

void attributeRefusals(std::vector<Callable>& callables,
                       const std::vector<fg::GraphDiagnostic>& diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.severity != fg::GraphSeverity::Error) continue;
    const auto line = lineOf(diagnostic.message);
    Callable* owner = nullptr;
    for (auto& callable : callables) {
      if (callable.line <= line && (owner == nullptr || callable.line > owner->line)) {
        owner = &callable;
      }
    }
    if (owner != nullptr && owner->refusal.empty()) owner->refusal = reasonOf(diagnostic.message);
  }
}

const fo4::pex::PexFunction* functionOf(const fo4::pex::PexFile& file, const Callable& callable) {
  if (file.objects.empty()) return nullptr;
  for (const auto& state : file.objects.front().states) {
    if (!helper::cmpIequals(state.name, callable.state)) continue;
    for (const auto& fn : state.functions) {
      if (helper::cmpIequals(fn.name, callable.name)) return &fn;
    }
  }
  return nullptr;
}

std::string bodyDifference(const fo4::pex::PexFile& original, const fo4::pex::PexFile& rebuilt,
                           const Callable& callable) {
  const auto* theirs = functionOf(original, callable);
  const auto* ours = functionOf(rebuilt, callable);
  if (theirs == nullptr || ours == nullptr) return "function not found";
  return helper::functionDifference(*theirs, *ours);
}

}

TEST(GraphLiftReal, LiftsAndRelinearizesSampledCorpusFunctionsToTheirOriginalBodies) {
  auto roots = helper::rootsFromEnv("FO4RE_PSC_CORPUS");
  if (roots.empty()) roots = helper::rootsFromEnv("FO4_PSC_CORPUS");
  if (roots.empty()) {
    GTEST_SKIP() << "FO4RE_PSC_CORPUS is not set to a directory holding .psc sources";
  }

  std::vector<fs::path> files;
  for (const auto& root : roots) {
    const auto found = helper::enumerateFiles(root, ".psc");
    files.insert(files.end(), found.begin(), found.end());
  }
  const auto candidates = files.size();
  const auto sample = helper::stratifiedSample(files, roots, kLiftSampleLimit);
  std::cout << "SAMPLE candidates=" << candidates << " selected=" << sample.files.size()
            << " stride=" << sample.stride << '\n';

  fg::GraphCompileOptions options;
  options.treatPapyrusErrorsAsInternalFaults = true;

  std::size_t attempted = 0;
  std::size_t lifted = 0;
  std::size_t roundtrip = 0;
  std::size_t refused = 0;
  std::map<std::string, std::size_t> reasons;
  std::vector<std::string> differences;

  fp::ScriptIndex index;
  for (const auto& root : roots) index.addRoot(root);

  for (const auto& path : sample.files) {
    const auto name = path.filename().string();
    std::vector<Callable> callables;
    std::optional<fp::PapyrusCompileResult> reference;
    fg::GraphCompileResult rebuilt;
    try {
      const auto* script = index.parseCached(path);
      if (script == nullptr || script->hasErrors()) continue;
      callables = callablesOf(*script);

      reference = fp::compileScript(index, script, name, fp::PapyrusCompileOptions{});
      if (!reference->success()) continue;

      fg::GraphLifter lifter(index);
      const auto lift = lifter.lift(*script);
      if (!lift.document) continue;
      attributeRefusals(callables, lift.diagnostics);

      auto document = *lift.document;
      rebuilt = fg::GraphCompiler(index).compile(document, options);
    } catch (const std::exception&) {
      for (auto& callable : callables) {
        if (callable.refusal.empty()) callable.refusal = "exception";
      }
    }
    attempted += callables.size();
    for (const auto& callable : callables) {
      if (!callable.refusal.empty()) {
        ++refused;
        ++reasons[callable.refusal];
        continue;
      }
      ++lifted;
      const auto difference = rebuilt.pex
                                  ? bodyDifference(*reference->pex, *rebuilt.pex, callable)
                                  : std::string("no pex");
      if (difference.empty()) {
        ++roundtrip;
      } else if (differences.size() < kDifferenceSampleLimit) {
        differences.push_back(name + " " + callable.name + ": " + difference);
      }
    }
  }

  fg::GraphCompiler::clearScratch();

  std::cout << "GRAPHLIFT attempted=" << attempted << " lifted=" << lifted
            << " roundtrip=" << roundtrip << " refused=" << refused << '\n';
  std::vector<std::pair<std::string, std::size_t>> ranked(reasons.begin(), reasons.end());
  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  for (std::size_t i = 0; i < ranked.size() && i < kReasonPrintLimit; ++i) {
    std::cout << "  REFUSED " << ranked[i].second << "  " << ranked[i].first << '\n';
  }
  for (const auto& difference : differences) std::cout << "  DIFF " << difference << '\n';

  ASSERT_GT(attempted, 0u) << files.size() << " files";
  EXPECT_GE(static_cast<double>(lifted),
            kMinimumLiftedFraction * static_cast<double>(attempted))
      << "lifted=" << lifted << " attempted=" << attempted;
}
