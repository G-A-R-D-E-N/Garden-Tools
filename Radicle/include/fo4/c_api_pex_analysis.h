#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_pex.h"

#ifdef __cplusplus
extern "C" {
#endif

// Invariant: call sites follow file order, then object order, then function order, then instruction order.
// Invariant: every string this API returns stays valid until the owning handle is destroyed.
// A handle is not thread-safe; different handles may be used from different threads.

typedef struct radicle_pex_callgraph radicle_pex_callgraph;

typedef enum radicle_pex_call_kind {
  RADICLE_PEX_CALL_METHOD = 0,
  RADICLE_PEX_CALL_PARENT = 1,
  RADICLE_PEX_CALL_STATIC = 2
} radicle_pex_call_kind;

typedef enum radicle_pex_function_slot {
  RADICLE_PEX_SLOT_STATE = 0,
  RADICLE_PEX_SLOT_PROPERTY_READ = 1,
  RADICLE_PEX_SLOT_PROPERTY_WRITE = 2
} radicle_pex_function_slot;

typedef struct radicle_pex_call_site {
  size_t caller_file;
  size_t caller_object;
  size_t caller_state;
  size_t caller_function;
  radicle_pex_function_slot caller_slot;
  const char* caller_object_name;
  const char* caller_function_name;
  size_t instruction_index;
  radicle_pex_call_kind kind;
  const char* object_name;
  const char* function_name;
  int32_t self_target;
  int32_t resolved;
  int32_t native_target;
  size_t target_file;
  size_t target_object;
  size_t target_state;
  size_t target_function;
} radicle_pex_call_site;

typedef struct radicle_pex_call_totals {
  size_t call_count;
  size_t unresolved_count;
  size_t native_count;
  size_t root_count;
} radicle_pex_call_totals;

// Invariant: a null file entry stands for no file and contributes nothing.
RADICLE_API radicle_status radicle_pex_callgraph_build(const radicle_pex* const* files,
                                                       size_t file_count,
                                                       radicle_pex_callgraph** out_graph);
RADICLE_API void radicle_pex_callgraph_destroy(radicle_pex_callgraph* graph);
RADICLE_API radicle_status radicle_pex_callgraph_totals(const radicle_pex_callgraph* graph,
                                                        radicle_pex_call_totals* out_totals);
RADICLE_API size_t radicle_pex_callgraph_call_count(const radicle_pex_callgraph* graph);
RADICLE_API radicle_status radicle_pex_callgraph_call(const radicle_pex_callgraph* graph,
                                                      size_t index,
                                                      radicle_pex_call_site* out_site);
RADICLE_API size_t radicle_pex_callgraph_unresolved_count(const radicle_pex_callgraph* graph);
RADICLE_API radicle_status radicle_pex_callgraph_unresolved(const radicle_pex_callgraph* graph,
                                                            size_t index,
                                                            radicle_pex_call_site* out_site);

typedef struct radicle_pex_chain_report radicle_pex_chain_report;

typedef enum radicle_pex_chain_problem {
  RADICLE_PEX_CHAIN_MISSING_PARENT = 0,
  RADICLE_PEX_CHAIN_CYCLE = 1,
  RADICLE_PEX_CHAIN_TOO_DEEP = 2
} radicle_pex_chain_problem;

typedef struct radicle_pex_chain_finding {
  size_t file_index;
  size_t object_index;
  const char* object_name;
  radicle_pex_chain_problem problem;
  size_t depth;
  size_t first_chain_name;
  size_t chain_name_count;
} radicle_pex_chain_finding;

// Invariant: depth_limit of 0 means the default 64; a seed type ends the chain it appears in.
RADICLE_API radicle_status radicle_pex_validate_parent_chains(const radicle_pex* const* files,
                                                              size_t file_count,
                                                              const char* const* seed_types,
                                                              size_t seed_type_count,
                                                              size_t depth_limit,
                                                              radicle_pex_chain_report** out_report);
RADICLE_API void radicle_pex_chain_report_destroy(radicle_pex_chain_report* report);
RADICLE_API size_t radicle_pex_chain_report_checked(const radicle_pex_chain_report* report);
RADICLE_API size_t radicle_pex_chain_report_count(const radicle_pex_chain_report* report);
RADICLE_API radicle_status radicle_pex_chain_report_get(const radicle_pex_chain_report* report,
                                                        size_t index,
                                                        radicle_pex_chain_finding* out_finding);
RADICLE_API radicle_status radicle_pex_chain_report_name(const radicle_pex_chain_report* report,
                                                         size_t index,
                                                         const char** out_name);

#ifdef __cplusplus
}
#endif
