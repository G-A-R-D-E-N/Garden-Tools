#include "fo4/archive/assets.hpp"
#include "fo4/pex/pex.hpp"
#include "fo4/pex_analysis/callgraph.hpp"
#include "fo4/pex_analysis/parent_chain.hpp"
#include "fo4/pex_analysis/script_set.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

std::string folded(std::string text) {
  for (auto& ch : text) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return text;
}

bool official_archive(const std::string& container) {
  const auto name = folded(std::filesystem::path(container).filename().string());
  const bool ba2 = name.size() > 4 && name.compare(name.size() - 4, 4, ".ba2") == 0;
  return ba2 && (name.rfind("fallout4", 0) == 0 || name.rfind("dlc", 0) == 0);
}

struct VanillaScripts {
  std::vector<std::unique_ptr<fo4::pex::PexFile>> files;
  std::vector<const fo4::pex::PexFile*> pointers;
  std::size_t candidates{0};
  std::size_t winners{0};
};

VanillaScripts load_vanilla_scripts() {
  VanillaScripts loaded;
  const std::filesystem::path data_dir = std::getenv("FO4_DATA_DIR");
  const auto archives = fo4::archive::archive_load_order(data_dir, {}, installed_master_and_cc_plugins(data_dir));
  const auto resolver = fo4::archive::AssetResolver::open({}, archives);
  const auto candidates = fo4::pex_analysis::enumerate_script_candidates(resolver);
  const auto winners = fo4::pex_analysis::resolve_script_set(candidates);
  loaded.candidates = candidates.size();
  loaded.winners = winners.size();
  for (const auto& winner : winners) {
    if (!official_archive(winner.winner.container)) continue;
    const auto bytes = resolver.read(winner.winner.location);
    std::vector<std::uint8_t> raw(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      raw[i] = std::to_integer<std::uint8_t>(bytes[i]);
    }
    loaded.files.push_back(std::make_unique<fo4::pex::PexFile>(fo4::pex::PexFile::fromBytes(raw)));
  }
  for (const auto& file : loaded.files) loaded.pointers.push_back(file.get());
  return loaded;
}

}  // namespace

TEST(PexAnalysisReal, OfficialScriptsReportEveryCallSite) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const auto loaded = load_vanilla_scripts();
  ASSERT_EQ(loaded.pointers.size(), 10271u);

  const auto graph = fo4::pex_analysis::build_call_graph(loaded.pointers);
  std::size_t functions = 0;
  std::size_t handler_calls = 0;
  std::size_t handler_functions = 0;
  for (const auto& call : graph.calls) {
    if (call.caller.slot != fo4::pex_analysis::PexFunctionSlot::State) ++handler_calls;
  }
  for (const auto& script : graph.scripts) {
    for (const auto& function : script.functions) {
      ++functions;
      if (function.slot != fo4::pex_analysis::PexFunctionSlot::State) ++handler_functions;
    }
  }

  std::cout << "PexAnalysis scripts=" << graph.scripts.size() << " functions=" << functions
            << " calls="
            << graph.calls.size() << " native=" << graph.nativeCalls
            << " resolved=" << graph.calls.size() - graph.unresolvedCalls
            << " unresolved=" << graph.unresolvedCalls << " roots=" << graph.roots.size()
            << " handler functions=" << handler_functions << " handler calls=" << handler_calls
            << "\n";
  EXPECT_EQ(functions, 30474u);
  EXPECT_EQ(graph.calls.size(), 122001u);
  EXPECT_EQ(graph.nativeCalls, 39084u);
  EXPECT_EQ(graph.unresolvedCalls, 33946u);
  EXPECT_EQ(graph.calls.size() - graph.unresolvedCalls, 88055u);
  EXPECT_EQ(graph.roots.size(), 25680u);
  EXPECT_EQ(handler_functions, 89u);
  EXPECT_EQ(handler_calls, 28u);
}

TEST(PexAnalysisReal, OfficialScriptsResolveEveryParentChain) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const auto loaded = load_vanilla_scripts();
  ASSERT_EQ(loaded.pointers.size(), 10271u);

  const auto report = fo4::pex_analysis::validate_parent_chains(loaded.pointers, {});
  std::size_t missing = 0;
  std::size_t cycles = 0;
  std::size_t too_deep = 0;
  for (const auto& finding : report.findings) {
    switch (finding.problem) {
      case fo4::pex_analysis::PexChainProblem::MissingParent:
        ++missing;
        break;
      case fo4::pex_analysis::PexChainProblem::Cycle:
        ++cycles;
        break;
      case fo4::pex_analysis::PexChainProblem::TooDeep:
        ++too_deep;
        break;
    }
  }

  std::cout << "PexAnalysis parents checked=" << report.checkedObjects << " findings="
            << report.findings.size() << " missing=" << missing << " cycles=" << cycles
            << " too-deep=" << too_deep << "\n";
  EXPECT_EQ(report.checkedObjects, 10271u);
  EXPECT_EQ(missing, 0u);
  EXPECT_EQ(cycles, 0u);
  EXPECT_EQ(too_deep, 0u);
}

TEST(PexAnalysisReal, ParentCallsDeclaredAboveTheImmediateParentStayUnresolved) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  const auto loaded = load_vanilla_scripts();
  ASSERT_EQ(loaded.pointers.size(), 10271u);

  const auto graph = fo4::pex_analysis::build_call_graph(loaded.pointers);
  std::map<std::string, const fo4::pex::PexObject*> by_name;
  for (const auto* file : loaded.pointers) {
    for (const auto& object : file->objects) by_name.try_emplace(folded(object.name), &object);
  }

  std::size_t gap = 0;
  std::vector<std::string> examples;
  for (const auto& call : graph.calls) {
    if (call.kind != fo4::pex_analysis::PexCallKind::Parent || call.resolved) continue;
    if (call.functionName.empty()) continue;
    if (call.caller.fileIndex >= loaded.pointers.size()) continue;
    const auto& objects = loaded.pointers[call.caller.fileIndex]->objects;
    if (call.caller.objectIndex >= objects.size()) continue;
    const auto& object = objects[call.caller.objectIndex];
    std::string current = object.parentClassName;
    std::set<std::string> seen;
    std::size_t depth = 0;
    while (!current.empty() && depth < fo4::pex_analysis::k_max_parent_depth) {
      const auto key = folded(current);
      if (!seen.insert(key).second) break;
      ++depth;
      const auto found = by_name.find(key);
      if (found == by_name.end()) break;
      std::size_t state = 0;
      std::size_t function = 0;
      if (fo4::pex_analysis::pex_find_function(*found->second, call.functionName, &state, &function)) {
        if (depth >= 2) {
          ++gap;
          if (examples.size() < 3) {
            examples.push_back(object.name + "." + call.functionName + " on " + object.parentClassName +
                               " is declared on " + found->second->name + ", " +
                               std::to_string(depth) + " parents up");
          }
        }
        break;
      }
      current = found->second->parentClassName;
    }
  }

  std::cout << "PexAnalysis grandparent-unresolved=" << gap << "\n";
  for (const auto& example : examples) {
    std::cout << "PexAnalysis grandparent-unresolved example " << example << "\n";
  }
  EXPECT_EQ(gap, 4u);
}
