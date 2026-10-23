#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"
#include "fo4/c_api_pex_analysis.h"

#include "fo4/pex/pex.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexState;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

constexpr std::uint8_t kCallMethod = 23;
constexpr std::uint8_t kCallParent = 24;
constexpr std::uint8_t kCallStatic = 25;

PexValue ident(std::string name) {
  PexValue value;
  value.type = PexValueType::Identifier;
  value.str = std::move(name);
  return value;
}

PexInstruction call(std::uint8_t opcode, std::initializer_list<PexValue> args) {
  PexInstruction instruction;
  instruction.opCode = opcode;
  instruction.args.assign(args);
  return instruction;
}

PexFunction function(std::string name, std::vector<PexInstruction> instructions, bool is_native = false) {
  PexFunction value;
  value.name = std::move(name);
  value.isNative = is_native;
  value.instructions = std::move(instructions);
  return value;
}

PexObject object(std::string name, std::string parent, std::vector<PexFunction> functions) {
  PexObject value;
  value.name = std::move(name);
  value.parentClassName = std::move(parent);
  PexState state;
  state.name = "";
  state.functions = std::move(functions);
  value.states.push_back(std::move(state));
  return value;
}

PexFile call_site_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Garden.psc";
  pex.objects.push_back(object("Base", "", {function("Init", {}, true)}));
  pex.objects.push_back(object("Child", "Base",
                               {function("Run", {call(kCallMethod, {ident("Run"), ident("self"),
                                                                   ident("::nonevar")}),
                                                 call(kCallParent, {ident("Init"), ident("::nonevar")}),
                                                 call(kCallStatic, {ident("Ghost"), ident("Nope"),
                                                                    ident("::nonevar")})})}));
  pex.rebuildStringTable();
  return pex;
}

PexFile parent_chain_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Chains.psc";
  pex.objects.push_back(object("Good", "ObjectReference", {function("Run", {})}));
  pex.objects.push_back(object("Orphan", "Ghost", {function("Run", {})}));
  pex.objects.push_back(object("LoopA", "LoopB", {function("Run", {})}));
  pex.objects.push_back(object("LoopB", "LoopA", {function("Run", {})}));
  pex.objects.push_back(object("Deep0", "Deep1", {function("Run", {})}));
  pex.objects.push_back(object("Deep1", "Deep2", {function("Run", {})}));
  pex.objects.push_back(object("Deep2", "", {function("Run", {})}));
  pex.rebuildStringTable();
  return pex;
}

struct PexDeleter {
  void operator()(radicle_pex* pex) const { radicle_pex_destroy(pex); }
};

using PexHandle = std::unique_ptr<radicle_pex, PexDeleter>;

PexHandle parse(PexFile pex) {
  const auto bytes = pex.toBytes();
  radicle_pex* handle = nullptr;
  EXPECT_EQ(radicle_pex_parse(bytes.data(), bytes.size(), &handle), RADICLE_STATUS_OK) << radicle_last_error();
  return PexHandle(handle);
}

struct GraphDeleter {
  void operator()(radicle_pex_callgraph* graph) const { radicle_pex_callgraph_destroy(graph); }
};

struct ReportDeleter {
  void operator()(radicle_pex_chain_report* report) const { radicle_pex_chain_report_destroy(report); }
};

}  // namespace

TEST(CApiPexAnalysis, BuildsEveryCallSiteWithKindTargetAndTotals) {
  const auto handle = parse(call_site_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {handle.get()};
  radicle_pex_callgraph* raw = nullptr;
  ASSERT_EQ(radicle_pex_callgraph_build(files, 1, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_pex_callgraph, GraphDeleter> graph(raw);

  radicle_pex_call_totals totals{};
  ASSERT_EQ(radicle_pex_callgraph_totals(graph.get(), &totals), RADICLE_STATUS_OK);
  EXPECT_EQ(totals.call_count, 3u);
  EXPECT_EQ(totals.unresolved_count, 1u);
  EXPECT_EQ(totals.native_count, 1u);
  EXPECT_EQ(totals.root_count, 0u);
  ASSERT_EQ(radicle_pex_callgraph_call_count(graph.get()), 3u);

  radicle_pex_call_site site{};
  ASSERT_EQ(radicle_pex_callgraph_call(graph.get(), 0, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.kind, RADICLE_PEX_CALL_METHOD);
  EXPECT_EQ(std::string(site.caller_object_name), "Child");
  EXPECT_EQ(std::string(site.caller_function_name), "Run");
  EXPECT_EQ(site.caller_slot, RADICLE_PEX_SLOT_STATE);
  EXPECT_EQ(std::string(site.object_name), "Child");
  EXPECT_EQ(std::string(site.function_name), "Run");
  EXPECT_EQ(site.self_target, 1);
  EXPECT_EQ(site.resolved, 1);
  EXPECT_EQ(site.native_target, 0);

  ASSERT_EQ(radicle_pex_callgraph_call(graph.get(), 1, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.kind, RADICLE_PEX_CALL_PARENT);
  EXPECT_EQ(std::string(site.object_name), "Base");
  EXPECT_EQ(std::string(site.function_name), "Init");
  EXPECT_EQ(site.resolved, 1);
  EXPECT_EQ(site.native_target, 1);
  EXPECT_EQ(site.target_object, 0u);
  EXPECT_EQ(site.target_function, 0u);

  ASSERT_EQ(radicle_pex_callgraph_call_count(graph.get()), 3u);
  ASSERT_EQ(radicle_pex_callgraph_unresolved_count(graph.get()), 1u);
  radicle_pex_call_site unresolved{};
  ASSERT_EQ(radicle_pex_callgraph_unresolved(graph.get(), 0, &unresolved), RADICLE_STATUS_OK);
  EXPECT_EQ(unresolved.kind, RADICLE_PEX_CALL_STATIC);
  EXPECT_EQ(std::string(unresolved.function_name), "Nope");
  EXPECT_EQ(unresolved.resolved, 0);
}

TEST(CApiPexAnalysis, WalksParentChainsWithSeedsAndADepthLimit) {
  const auto handle = parse(parent_chain_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {handle.get()};
  const char* seeds[] = {"ObjectReference"};

  radicle_pex_chain_report* raw = nullptr;
  ASSERT_EQ(radicle_pex_validate_parent_chains(files, 1, seeds, 1, 0, &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> report(raw);
  EXPECT_EQ(radicle_pex_chain_report_checked(report.get()), 7u);
  ASSERT_EQ(radicle_pex_chain_report_count(report.get()), 3u);

  radicle_pex_chain_finding finding{};
  ASSERT_EQ(radicle_pex_chain_report_get(report.get(), 0, &finding), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(finding.object_name), "Orphan");
  EXPECT_EQ(finding.problem, RADICLE_PEX_CHAIN_MISSING_PARENT);
  ASSERT_EQ(finding.chain_name_count, 1u);
  const char* name = nullptr;
  ASSERT_EQ(radicle_pex_chain_report_name(report.get(), finding.first_chain_name, &name), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(name), "Ghost");

  ASSERT_EQ(radicle_pex_chain_report_get(report.get(), 1, &finding), RADICLE_STATUS_OK);
  EXPECT_EQ(finding.problem, RADICLE_PEX_CHAIN_CYCLE);
  EXPECT_EQ(finding.depth, 3u);
  ASSERT_EQ(radicle_pex_chain_report_get(report.get(), 2, &finding), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(finding.object_name), "LoopB");
  EXPECT_EQ(finding.problem, RADICLE_PEX_CHAIN_CYCLE);

  radicle_pex_chain_report* limited_raw = nullptr;
  ASSERT_EQ(radicle_pex_validate_parent_chains(files, 1, seeds, 1, 1, &limited_raw), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> limited(limited_raw);
  bool saw_deep = false;
  for (std::size_t i = 0; i < radicle_pex_chain_report_count(limited.get()); ++i) {
    radicle_pex_chain_finding entry{};
    ASSERT_EQ(radicle_pex_chain_report_get(limited.get(), i, &entry), RADICLE_STATUS_OK);
    if (std::string(entry.object_name) == "Deep0") {
      saw_deep = entry.problem == RADICLE_PEX_CHAIN_TOO_DEEP && entry.depth == 1u;
    }
  }
  EXPECT_TRUE(saw_deep);
}

TEST(CApiPexAnalysis, RejectsBadArguments) {
  radicle_pex_callgraph* graph = nullptr;
  EXPECT_EQ(radicle_pex_callgraph_build(nullptr, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_callgraph_build(nullptr, 1, &graph), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_callgraph_build(nullptr, 0, &graph), RADICLE_STATUS_OK);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(radicle_pex_callgraph_call_count(graph), 0u);
  radicle_pex_call_site site{};
  EXPECT_EQ(radicle_pex_callgraph_call(graph, 0, &site), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_callgraph_unresolved(graph, 0, &site), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_callgraph_totals(graph, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_pex_callgraph_destroy(graph);
  radicle_pex_callgraph_destroy(nullptr);

  radicle_pex_chain_report* report = nullptr;
  EXPECT_EQ(radicle_pex_validate_parent_chains(nullptr, 1, nullptr, 0, 0, &report), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_validate_parent_chains(nullptr, 0, nullptr, 1, 0, &report), RADICLE_STATUS_INVALID_ARGUMENT);
  const char* seeds[] = {nullptr};
  EXPECT_EQ(radicle_pex_validate_parent_chains(nullptr, 0, seeds, 1, 0, &report), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_pex_chain_report_count(nullptr), 0u);
  EXPECT_EQ(radicle_pex_chain_report_checked(nullptr), 0u);
}
