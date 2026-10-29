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

PexFile names_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Names.psc";
  pex.objects.push_back(object("Base", "", {function("Init", {}, true)}));
  pex.objects.push_back(
      object("Caller", "",
             {function("Run", {call(kCallStatic, {ident("bAsE"), ident("iNiT"), ident("::nonevar")})})}));
  pex.rebuildStringTable();
  return pex;
}

PexFile cross_file_a() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "CrossA.psc";
  pex.objects.push_back(object("Caller", "",
                               {function("Run", {call(kCallStatic, {ident("Twin"), ident("Go"),
                                                                    ident("::nonevar")}),
                                                 call(kCallStatic, {ident("Early"), ident("Go"),
                                                                    ident("::nonevar")})})}));
  pex.objects.push_back(object("Twin", "", {}));
  pex.objects.push_back(object("Early", "", {function("Go", {})}));
  pex.rebuildStringTable();
  return pex;
}

PexFile cross_file_b() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "CrossB.psc";
  pex.objects.push_back(object("Twin", "", {function("Go", {})}));
  pex.objects.push_back(object("Early", "", {function("Go", {})}));
  pex.rebuildStringTable();
  return pex;
}

PexFile typed_receiver_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Typed.psc";
  pex.objects.push_back(object("Base", "", {function("Init", {}, true)}));
  PexFunction run;
  run.name = "Run";
  run.params.push_back({"P", "Base"});
  run.locals.push_back({"L", "Base"});
  run.instructions = {call(kCallMethod, {ident("Init"), ident("Who"), ident("::nonevar")}),
                      call(kCallMethod, {ident("Init"), ident("P"), ident("::nonevar")}),
                      call(kCallMethod, {ident("Init"), ident("L"), ident("::nonevar")})};
  PexObject host = object("Host", "", {});
  host.variables.push_back({"Who", "Base", 0, std::nullopt, false});
  host.states[0].functions.push_back(std::move(run));
  pex.objects.push_back(std::move(host));
  pex.rebuildStringTable();
  return pex;
}

PexFile deep_chain_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Deep.psc";
  pex.objects.push_back(object("DeepA", "DeepB", {function("Run", {})}));
  pex.objects.push_back(object("DeepB", "DeepC", {function("Run", {})}));
  pex.objects.push_back(object("DeepC", "DeepD", {function("Run", {})}));
  pex.objects.push_back(object("DeepD", "", {function("Run", {})}));
  pex.rebuildStringTable();
  return pex;
}

PexFile cycle_and_seed_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Cycles.psc";
  pex.objects.push_back(object("Selfie", "Selfie", {function("Run", {})}));
  pex.objects.push_back(object("Guard", "Sentry", {function("Run", {})}));
  pex.objects.push_back(object("Sentry", "", {function("Run", {})}));
  pex.rebuildStringTable();
  return pex;
}

PexFile handler_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Handlers.psc";
  pex.objects.push_back(object("Base", "", {function("Init", {}, true)}));
  PexObject host = object("Host", "", {function("Run", {})});
  fo4::pex::PexProperty total;
  total.name = "Total";
  total.type = "Int";
  total.flags = 0x03;
  total.readHandler =
      function("get_Total", {call(kCallStatic, {ident("Base"), ident("Init"), ident("::nonevar")})});
  total.writeHandler = function("set_Total", {call(kCallMethod, {ident("Missing"), ident("self"),
                                                                 ident("::nonevar")})});
  host.properties.push_back(std::move(total));
  pex.objects.push_back(std::move(host));
  pex.rebuildStringTable();
  return pex;
}

PexFile grandparent_file() {
  PexFile pex;
  pex.majorVersion = 3;
  pex.minorVersion = 9;
  pex.gameId = 2;
  pex.sourceFileName = "Grand.psc";
  pex.objects.push_back(object("Root", "", {function("Deep", {})}));
  pex.objects.push_back(object("Middle", "Root", {}));
  pex.objects.push_back(object("Leaf", "Middle",
                               {function("Run", {call(kCallParent, {ident("Deep"),
                                                                     ident("::nonevar")})})}));
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

namespace {

// A pointer or handle that a rejected call must overwrite; it is never dereferenced.
template <typename T>
T* sentinel() {
  return reinterpret_cast<T*>(static_cast<std::uintptr_t>(1));
}

}  // namespace

TEST(CApiPexAnalysis, RejectsBadArguments) {
  // Invariant: a rejected call must clear its out-parameter before it validates the rest.
  radicle_pex_callgraph* graph = sentinel<radicle_pex_callgraph>();
  EXPECT_EQ(radicle_pex_callgraph_build(nullptr, 1, &graph), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(graph, nullptr);
  EXPECT_EQ(radicle_pex_callgraph_build(nullptr, 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_pex_callgraph_build(nullptr, 0, &graph), RADICLE_STATUS_OK);
  ASSERT_NE(graph, nullptr);
  EXPECT_EQ(radicle_pex_callgraph_call_count(graph), 0u);
  radicle_pex_call_site site{};
  site.instruction_index = 9u;
  site.resolved = 1;
  EXPECT_EQ(radicle_pex_callgraph_call(graph, 0, &site), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(site.instruction_index, 0u);
  EXPECT_EQ(site.resolved, 0);
  site.instruction_index = 9u;
  EXPECT_EQ(radicle_pex_callgraph_unresolved(graph, 0, &site), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(site.instruction_index, 0u);
  EXPECT_EQ(radicle_pex_callgraph_totals(graph, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_pex_callgraph_destroy(graph);
  radicle_pex_callgraph_destroy(nullptr);

  radicle_pex_chain_report* report = sentinel<radicle_pex_chain_report>();
  EXPECT_EQ(radicle_pex_validate_parent_chains(nullptr, 1, nullptr, 0, 0, &report), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(report, nullptr);
  report = sentinel<radicle_pex_chain_report>();
  EXPECT_EQ(radicle_pex_validate_parent_chains(nullptr, 0, nullptr, 1, 0, &report), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(report, nullptr);
  const char* seeds[] = {nullptr};
  report = sentinel<radicle_pex_chain_report>();
  EXPECT_EQ(radicle_pex_validate_parent_chains(nullptr, 0, seeds, 1, 0, &report), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(report, nullptr);
  EXPECT_EQ(radicle_pex_chain_report_count(nullptr), 0u);
  EXPECT_EQ(radicle_pex_chain_report_checked(nullptr), 0u);

  ASSERT_EQ(radicle_pex_validate_parent_chains(nullptr, 0, nullptr, 0, 0, &report), RADICLE_STATUS_OK);
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> empty(report);
  radicle_pex_chain_finding finding{};
  finding.file_index = 9u;
  finding.problem = RADICLE_PEX_CHAIN_CYCLE;
  EXPECT_EQ(radicle_pex_chain_report_get(empty.get(), 0, &finding), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(finding.file_index, 0u);
  EXPECT_EQ(finding.problem, RADICLE_PEX_CHAIN_MISSING_PARENT);
  const char* name = "stale";
  EXPECT_EQ(radicle_pex_chain_report_name(empty.get(), 0, &name), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(name, static_cast<const char*>(nullptr));
}

TEST(CApiPexAnalysis, MatchesNamesWithoutCaseAndResolvesAcrossFiles) {
  const auto names = parse(names_file());
  ASSERT_NE(names, nullptr);
  const radicle_pex* name_files[] = {names.get()};
  radicle_pex_callgraph* raw = nullptr;
  ASSERT_EQ(radicle_pex_callgraph_build(name_files, 1, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_pex_callgraph, GraphDeleter> graph(raw);

  // Invariant: resolution matches object and function names without case.
  radicle_pex_call_totals totals{};
  ASSERT_EQ(radicle_pex_callgraph_totals(graph.get(), &totals), RADICLE_STATUS_OK);
  EXPECT_EQ(totals.call_count, 1u);
  EXPECT_EQ(totals.unresolved_count, 0u);
  EXPECT_EQ(totals.native_count, 1u);
  radicle_pex_call_site site{};
  ASSERT_EQ(radicle_pex_callgraph_call(graph.get(), 0, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.resolved, 1);
  EXPECT_EQ(site.native_target, 1);
  EXPECT_EQ(site.target_object, 0u);
  EXPECT_EQ(site.target_function, 0u);

  const auto first = parse(cross_file_a());
  const auto second = parse(cross_file_b());
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  const radicle_pex* files[] = {first.get(), second.get()};
  ASSERT_EQ(radicle_pex_callgraph_build(files, 2, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_pex_callgraph, GraphDeleter> cross(raw);

  // Invariant: resolution keeps scanning later files, so a duplicate object name in a later file is considered.
  ASSERT_EQ(radicle_pex_callgraph_call(cross.get(), 0, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(site.object_name), "Twin");
  EXPECT_EQ(site.resolved, 1);
  EXPECT_EQ(site.target_file, 1u);
  EXPECT_EQ(site.target_object, 0u);
  ASSERT_EQ(radicle_pex_callgraph_call(cross.get(), 1, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.resolved, 1);
  EXPECT_EQ(site.target_file, 0u);
  EXPECT_EQ(site.target_object, 2u);
}

TEST(CApiPexAnalysis, ResolvesReceiversThroughDeclarations) {
  const auto handle = parse(typed_receiver_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {handle.get()};
  radicle_pex_callgraph* raw = nullptr;
  ASSERT_EQ(radicle_pex_callgraph_build(files, 1, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_pex_callgraph, GraphDeleter> graph(raw);

  // Invariant: a receiver resolves through its declared type, never through its variable name.
  radicle_pex_call_totals totals{};
  ASSERT_EQ(radicle_pex_callgraph_totals(graph.get(), &totals), RADICLE_STATUS_OK);
  ASSERT_EQ(totals.call_count, 3u);
  EXPECT_EQ(totals.unresolved_count, 0u);
  for (std::size_t i = 0; i < 3; ++i) {
    radicle_pex_call_site site{};
    ASSERT_EQ(radicle_pex_callgraph_call(graph.get(), i, &site), RADICLE_STATUS_OK);
    ASSERT_EQ(site.resolved, 1) << i;
    EXPECT_EQ(std::string(site.object_name), "Base") << i;
    EXPECT_EQ(site.target_file, 0u) << i;
    EXPECT_EQ(site.target_object, 0u) << i;
    EXPECT_EQ(site.native_target, 1) << i;
  }
}

TEST(CApiPexAnalysis, StopsChainsAtTheDepthLimit) {
  const auto handle = parse(deep_chain_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {handle.get()};

  radicle_pex_chain_report* raw = nullptr;
  ASSERT_EQ(radicle_pex_validate_parent_chains(files, 1, nullptr, 0, 3, &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> exact(raw);
  EXPECT_EQ(radicle_pex_chain_report_checked(exact.get()), 4u);
  // Invariant: three parents inside a limit of three is not too deep.
  EXPECT_EQ(radicle_pex_chain_report_count(exact.get()), 0u);

  ASSERT_EQ(radicle_pex_validate_parent_chains(files, 1, nullptr, 0, 2, &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> short_limit(raw);
  ASSERT_EQ(radicle_pex_chain_report_count(short_limit.get()), 1u);
  radicle_pex_chain_finding finding{};
  ASSERT_EQ(radicle_pex_chain_report_get(short_limit.get(), 0, &finding), RADICLE_STATUS_OK);
  EXPECT_EQ(std::string(finding.object_name), "DeepA");
  EXPECT_EQ(finding.problem, RADICLE_PEX_CHAIN_TOO_DEEP);
  EXPECT_EQ(finding.depth, 2u);
  EXPECT_EQ(finding.chain_name_count, 2u);
}

TEST(CApiPexAnalysis, StopsChainsAtSeedTypesAndReportsSelfParents) {
  const auto handle = parse(cycle_and_seed_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {handle.get()};
  const char* seeds[] = {"Sentry"};

  radicle_pex_chain_report* raw = nullptr;
  ASSERT_EQ(radicle_pex_validate_parent_chains(files, 1, seeds, 1, 0, &raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> report(raw);
  ASSERT_EQ(radicle_pex_chain_report_count(report.get()), 1u);
  radicle_pex_chain_finding finding{};
  ASSERT_EQ(radicle_pex_chain_report_get(report.get(), 0, &finding), RADICLE_STATUS_OK);
  // Invariant: a seed type ends the chain before it becomes a missing or cyclic parent.
  EXPECT_EQ(std::string(finding.object_name), "Selfie");
  EXPECT_EQ(finding.problem, RADICLE_PEX_CHAIN_CYCLE);
  EXPECT_EQ(finding.depth, 2u);
}

TEST(CApiPexAnalysis, CountsCallsInsidePropertyHandlers) {
  const auto handle = parse(handler_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {handle.get()};
  radicle_pex_callgraph* raw = nullptr;
  ASSERT_EQ(radicle_pex_callgraph_build(files, 1, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_pex_callgraph, GraphDeleter> graph(raw);

  // Invariant: a call inside a property handler is a call site with the handler slot, not a hidden one.
  radicle_pex_call_totals totals{};
  ASSERT_EQ(radicle_pex_callgraph_totals(graph.get(), &totals), RADICLE_STATUS_OK);
  ASSERT_EQ(totals.call_count, 2u);
  EXPECT_EQ(totals.unresolved_count, 1u);
  EXPECT_EQ(totals.native_count, 1u);

  radicle_pex_call_site site{};
  ASSERT_EQ(radicle_pex_callgraph_call(graph.get(), 0, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.caller_slot, RADICLE_PEX_SLOT_PROPERTY_READ);
  EXPECT_EQ(std::string(site.caller_object_name), "Host");
  EXPECT_EQ(std::string(site.caller_function_name), "Total");
  EXPECT_EQ(std::string(site.function_name), "Init");
  EXPECT_EQ(site.resolved, 1);
  EXPECT_EQ(site.native_target, 1);

  ASSERT_EQ(radicle_pex_callgraph_call(graph.get(), 1, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.caller_slot, RADICLE_PEX_SLOT_PROPERTY_WRITE);
  EXPECT_EQ(std::string(site.caller_function_name), "Total");
  EXPECT_EQ(site.kind, RADICLE_PEX_CALL_METHOD);
  EXPECT_EQ(site.resolved, 0);
}

TEST(CApiPexAnalysis, TreatsNullFilesAndIgnoresGrandparentFunctions) {
  const auto handle = parse(grandparent_file());
  ASSERT_NE(handle, nullptr);
  const radicle_pex* files[] = {nullptr, handle.get()};
  radicle_pex_callgraph* raw = nullptr;
  ASSERT_EQ(radicle_pex_callgraph_build(files, 2, &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_pex_callgraph, GraphDeleter> graph(raw);

  // Invariant: a null file entry contributes nothing and is not an error.
  radicle_pex_call_totals totals{};
  ASSERT_EQ(radicle_pex_callgraph_totals(graph.get(), &totals), RADICLE_STATUS_OK);
  EXPECT_EQ(totals.call_count, 1u);
  ASSERT_EQ(radicle_pex_callgraph_unresolved_count(graph.get()), 1u);
  radicle_pex_call_site site{};
  ASSERT_EQ(radicle_pex_callgraph_unresolved(graph.get(), 0, &site), RADICLE_STATUS_OK);
  EXPECT_EQ(site.caller_file, 1u);
  // Invariant: callparent targets the immediate parent only, so a grandparent function stays unresolved.
  EXPECT_EQ(site.kind, RADICLE_PEX_CALL_PARENT);
  EXPECT_EQ(std::string(site.object_name), "Middle");
  EXPECT_EQ(std::string(site.function_name), "Deep");
  EXPECT_EQ(site.resolved, 0);

  radicle_pex_chain_report* report_raw = nullptr;
  ASSERT_EQ(radicle_pex_validate_parent_chains(files, 2, nullptr, 0, 0, &report_raw), RADICLE_STATUS_OK)
      << radicle_last_error();
  const std::unique_ptr<radicle_pex_chain_report, ReportDeleter> report(report_raw);
  EXPECT_EQ(radicle_pex_chain_report_checked(report.get()), 3u);
  EXPECT_EQ(radicle_pex_chain_report_count(report.get()), 0u);
}

TEST(CApiPexAnalysis, NamesTheFixedOperandRolesOfEveryCallShape) {
  // Must report callmethod's three fixed operands as name, receiver and result, in that order.
  EXPECT_EQ(radicle_pex_call_operand_role(kCallMethod, 0), RADICLE_PEX_OPERAND_FUNCTION_NAME);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallMethod, 1), RADICLE_PEX_OPERAND_RECEIVER);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallMethod, 2), RADICLE_PEX_OPERAND_RESULT);
  // Must report callparent's two fixed operands as name and result, with no receiver in either slot.
  EXPECT_EQ(radicle_pex_call_operand_role(kCallParent, 0), RADICLE_PEX_OPERAND_FUNCTION_NAME);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallParent, 1), RADICLE_PEX_OPERAND_RESULT);
  // Must report callstatic's three fixed operands as owning script, name and result, in that order.
  EXPECT_EQ(radicle_pex_call_operand_role(kCallStatic, 0), RADICLE_PEX_OPERAND_OWNING_SCRIPT);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallStatic, 1), RADICLE_PEX_OPERAND_FUNCTION_NAME);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallStatic, 2), RADICLE_PEX_OPERAND_RESULT);
}

TEST(CApiPexAnalysis, GivesNoRoleToVariadicOrMissingOperands) {
  // Must keep a variadic argument role-free, so a caller never reads an argument as the receiver.
  EXPECT_EQ(radicle_pex_call_operand_role(kCallMethod, 3), RADICLE_PEX_OPERAND_NONE);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallParent, 2), RADICLE_PEX_OPERAND_NONE);
  EXPECT_EQ(radicle_pex_call_operand_role(kCallStatic, 3), RADICLE_PEX_OPERAND_NONE);
  // Must keep a far index role-free rather than reading past the fixed operands.
  EXPECT_EQ(radicle_pex_call_operand_role(kCallMethod, 4096), RADICLE_PEX_OPERAND_NONE);
}

TEST(CApiPexAnalysis, NamesNoRoleOutsideTheThreeCallOpcodes) {
  // Must leave every non-call opcode role-free, so propset and assign cannot be read as calls.
  for (std::uint8_t opcode = 0; opcode <= 46; ++opcode) {
    if (opcode == kCallMethod || opcode == kCallParent || opcode == kCallStatic) continue;
    for (std::size_t index = 0; index < 4; ++index) {
      EXPECT_EQ(radicle_pex_call_operand_role(opcode, index), RADICLE_PEX_OPERAND_NONE)
          << "opcode " << static_cast<int>(opcode) << " index " << index;
    }
  }
  // Must keep an opcode past the 0..46 name table role-free instead of indexing it.
  EXPECT_EQ(radicle_pex_call_operand_role(47, 0), RADICLE_PEX_OPERAND_NONE);
  EXPECT_EQ(radicle_pex_call_operand_role(255, 1), RADICLE_PEX_OPERAND_NONE);
}

TEST(CApiPexAnalysis, AgreesWithTheFixedOperandCountOfEveryCallOpcode) {
  // Must name exactly as many roles as the encoder writes fixed operands for each call opcode.
  for (const std::uint8_t opcode : {kCallMethod, kCallParent, kCallStatic}) {
    std::size_t roles = 0;
    while (roles < 8 && radicle_pex_call_operand_role(opcode, roles) != RADICLE_PEX_OPERAND_NONE) {
      ++roles;
    }
    EXPECT_EQ(roles, static_cast<std::size_t>(PexFile::kOpCodes[opcode].args))
        << "opcode " << static_cast<int>(opcode);
  }
}
