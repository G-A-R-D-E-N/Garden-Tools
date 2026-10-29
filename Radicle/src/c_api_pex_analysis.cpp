#include "c_api_env_internal.hpp"
#include "c_api_pex_internal.hpp"
#include "fo4/c_api_pex_analysis.h"
#include "fo4/pex_analysis/callgraph.hpp"
#include "fo4/pex_analysis/parent_chain.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct radicle_pex_callgraph {
  fo4::pex_analysis::PexCallGraph graph;
  std::vector<std::size_t> unresolved;  // invariant: indexes into graph.calls
};

struct radicle_pex_chain_report {
  std::size_t checked{0};
  std::vector<fo4::pex_analysis::PexChainFinding> findings;
  std::vector<std::string> chain_names;
  std::vector<std::size_t> first_chain_name;  // invariant: parallel to findings
};

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

const fo4::pex::PexFile* to_file(const radicle_pex* pex) {
  return pex == nullptr ? nullptr : &pex->value;
}

radicle_pex_function_slot to_c_slot(fo4::pex_analysis::PexFunctionSlot slot) {
  switch (slot) {
    case fo4::pex_analysis::PexFunctionSlot::PropertyRead:
      return RADICLE_PEX_SLOT_PROPERTY_READ;
    case fo4::pex_analysis::PexFunctionSlot::PropertyWrite:
      return RADICLE_PEX_SLOT_PROPERTY_WRITE;
    case fo4::pex_analysis::PexFunctionSlot::State:
      break;
  }
  return RADICLE_PEX_SLOT_STATE;
}

radicle_pex_call_kind to_c_kind(fo4::pex_analysis::PexCallKind kind) {
  switch (kind) {
    case fo4::pex_analysis::PexCallKind::Parent:
      return RADICLE_PEX_CALL_PARENT;
    case fo4::pex_analysis::PexCallKind::Static:
      return RADICLE_PEX_CALL_STATIC;
    case fo4::pex_analysis::PexCallKind::Method:
      break;
  }
  return RADICLE_PEX_CALL_METHOD;
}

radicle_pex_chain_problem to_c_problem(fo4::pex_analysis::PexChainProblem problem) {
  switch (problem) {
    case fo4::pex_analysis::PexChainProblem::Cycle:
      return RADICLE_PEX_CHAIN_CYCLE;
    case fo4::pex_analysis::PexChainProblem::TooDeep:
      return RADICLE_PEX_CHAIN_TOO_DEEP;
    case fo4::pex_analysis::PexChainProblem::MissingParent:
      break;
  }
  return RADICLE_PEX_CHAIN_MISSING_PARENT;
}

void fill_site(const fo4::pex_analysis::PexCallSite& site, radicle_pex_call_site* out) {
  *out = {};
  out->caller_file = site.caller.fileIndex;
  out->caller_object = site.caller.objectIndex;
  out->caller_state = site.caller.stateIndex;
  out->caller_function = site.caller.functionIndex;
  out->caller_slot = to_c_slot(site.caller.slot);
  out->caller_object_name = site.caller.objectName.c_str();
  out->caller_function_name = site.caller.functionName.c_str();
  out->instruction_index = site.instructionIndex;
  out->kind = to_c_kind(site.kind);
  out->object_name = site.objectName.c_str();
  out->function_name = site.functionName.c_str();
  out->self_target = site.selfTarget ? 1 : 0;
  out->resolved = site.resolved ? 1 : 0;
  out->native_target = site.nativeTarget ? 1 : 0;
  out->target_file = site.target.fileIndex;
  out->target_object = site.target.objectIndex;
  out->target_state = site.target.stateIndex;
  out->target_function = site.target.functionIndex;
}

std::vector<const fo4::pex::PexFile*> to_files(const radicle_pex* const* files, size_t file_count) {
  std::vector<const fo4::pex::PexFile*> inputs;
  inputs.reserve(file_count);
  for (size_t i = 0; i < file_count; ++i) inputs.push_back(to_file(files[i]));
  return inputs;
}

}  // namespace

extern "C" {

radicle_status radicle_pex_callgraph_build(const radicle_pex* const* files,
                                            size_t file_count,
                                            radicle_pex_callgraph** out_graph) {
  clear_error();
  if (out_graph == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_graph is required");
  // Invariant: the out-parameter must be cleared before any later check rejects the call.
  *out_graph = nullptr;
  if (file_count != 0 && files == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "files is required when file_count is not zero");
  }
  return guarded("unknown exception while building the call graph", [&] {
    const auto inputs = to_files(files, file_count);
    auto graph = std::make_unique<radicle_pex_callgraph>();
    graph->graph = fo4::pex_analysis::build_call_graph(inputs);
    for (std::size_t i = 0; i < graph->graph.calls.size(); ++i) {
      if (!graph->graph.calls[i].resolved) graph->unresolved.push_back(i);
    }
    *out_graph = graph.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_pex_callgraph_destroy(radicle_pex_callgraph* graph) {
  clear_error();
  delete graph;
}

radicle_status radicle_pex_callgraph_totals(const radicle_pex_callgraph* graph,
                                             radicle_pex_call_totals* out_totals) {
  clear_error();
  if (graph == nullptr || out_totals == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "graph and out_totals are required");
  }
  *out_totals = {graph->graph.calls.size(), graph->graph.unresolvedCalls, graph->graph.nativeCalls,
                 graph->graph.roots.size()};
  return RADICLE_STATUS_OK;
}

radicle_pex_operand_role radicle_pex_call_operand_role(uint8_t opcode, size_t index) {
  clear_error();
  switch (fo4::pex_analysis::pex_call_operand_role(opcode, index)) {
    case fo4::pex_analysis::PexOperandRole::FunctionName:
      return RADICLE_PEX_OPERAND_FUNCTION_NAME;
    case fo4::pex_analysis::PexOperandRole::Receiver:
      return RADICLE_PEX_OPERAND_RECEIVER;
    case fo4::pex_analysis::PexOperandRole::OwningScript:
      return RADICLE_PEX_OPERAND_OWNING_SCRIPT;
    case fo4::pex_analysis::PexOperandRole::Result:
      return RADICLE_PEX_OPERAND_RESULT;
    case fo4::pex_analysis::PexOperandRole::None:
      break;
  }
  return RADICLE_PEX_OPERAND_NONE;
}

size_t radicle_pex_callgraph_call_count(const radicle_pex_callgraph* graph) {
  clear_error();
  return graph == nullptr ? 0 : graph->graph.calls.size();
}

radicle_status radicle_pex_callgraph_call(const radicle_pex_callgraph* graph,
                                           size_t index,
                                           radicle_pex_call_site* out_site) {
  clear_error();
  if (graph == nullptr || out_site == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "graph and out_site are required");
  }
  *out_site = {};
  if (index >= graph->graph.calls.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "call index is out of range");
  fill_site(graph->graph.calls[index], out_site);
  return RADICLE_STATUS_OK;
}

size_t radicle_pex_callgraph_unresolved_count(const radicle_pex_callgraph* graph) {
  clear_error();
  return graph == nullptr ? 0 : graph->unresolved.size();
}

radicle_status radicle_pex_callgraph_unresolved(const radicle_pex_callgraph* graph,
                                                 size_t index,
                                                 radicle_pex_call_site* out_site) {
  clear_error();
  if (graph == nullptr || out_site == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "graph and out_site are required");
  }
  *out_site = {};
  if (index >= graph->unresolved.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unresolved index is out of range");
  }
  fill_site(graph->graph.calls[graph->unresolved[index]], out_site);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_pex_validate_parent_chains(const radicle_pex* const* files,
                                                   size_t file_count,
                                                   const char* const* seed_types,
                                                   size_t seed_type_count,
                                                   size_t depth_limit,
                                                   radicle_pex_chain_report** out_report) {
  clear_error();
  if (out_report == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_report is required");
  // Invariant: the out-parameter must be cleared before any later check rejects the call.
  *out_report = nullptr;
  if (file_count != 0 && files == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "files is required when file_count is not zero");
  }
  if (seed_type_count != 0 && seed_types == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "seed_types is required when seed_type_count is not zero");
  }
  return guarded("unknown exception while walking parent chains", [&] {
    const auto inputs = to_files(files, file_count);
    std::vector<std::string> seeds;
    seeds.reserve(seed_type_count);
    for (size_t i = 0; i < seed_type_count; ++i) {
      if (seed_types[i] == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "seed types must not be null");
      seeds.emplace_back(seed_types[i]);
    }
    const auto limit = depth_limit == 0 ? fo4::pex_analysis::k_max_parent_depth : depth_limit;
    auto report = std::make_unique<radicle_pex_chain_report>();
    auto result = fo4::pex_analysis::validate_parent_chains(inputs, seeds, limit);
    report->checked = result.checkedObjects;
    report->findings = std::move(result.findings);
    for (auto& finding : report->findings) {
      report->first_chain_name.push_back(report->chain_names.size());
      for (auto& name : finding.chain) report->chain_names.push_back(std::move(name));
    }
    *out_report = report.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_pex_chain_report_destroy(radicle_pex_chain_report* report) {
  clear_error();
  delete report;
}

size_t radicle_pex_chain_report_checked(const radicle_pex_chain_report* report) {
  clear_error();
  return report == nullptr ? 0 : report->checked;
}

size_t radicle_pex_chain_report_count(const radicle_pex_chain_report* report) {
  clear_error();
  return report == nullptr ? 0 : report->findings.size();
}

radicle_status radicle_pex_chain_report_get(const radicle_pex_chain_report* report,
                                             size_t index,
                                             radicle_pex_chain_finding* out_finding) {
  clear_error();
  if (report == nullptr || out_finding == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "report and out_finding are required");
  }
  *out_finding = {};
  if (index >= report->findings.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "finding index is out of range");
  const auto& finding = report->findings[index];
  out_finding->file_index = finding.fileIndex;
  out_finding->object_index = finding.objectIndex;
  out_finding->object_name = finding.objectName.c_str();
  out_finding->problem = to_c_problem(finding.problem);
  out_finding->depth = finding.depth;
  out_finding->first_chain_name = report->first_chain_name[index];
  out_finding->chain_name_count = finding.chain.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_pex_chain_report_name(const radicle_pex_chain_report* report,
                                              size_t index,
                                              const char** out_name) {
  clear_error();
  if (report == nullptr || out_name == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "report and out_name are required");
  }
  *out_name = nullptr;
  if (index >= report->chain_names.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "chain name index is out of range");
  *out_name = report->chain_names[index].c_str();
  return RADICLE_STATUS_OK;
}

}  // extern "C"
