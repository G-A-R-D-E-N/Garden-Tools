#include "c_api_forms_internal.hpp"
#include "fo4/c_api_condition_live.h"
#include "fo4/environment/condition_live.hpp"
#include "fo4/environment/dialogue.hpp"
#include "fo4/forms/npc_gameplay.hpp"
#include "fo4/schema/ctda.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;
using fo4::env::ConditionFact;

static_assert(static_cast<int>(ConditionFact::current_time) == RADICLE_CONDITION_FACT_CURRENT_TIME);
static_assert(static_cast<int>(ConditionFact::is_voice_type) == RADICLE_CONDITION_FACT_IS_VOICE_TYPE);
static_assert(static_cast<int>(ConditionFact::is_sex) == RADICLE_CONDITION_FACT_IS_SEX);
static_assert(static_cast<int>(ConditionFact::item_count) == RADICLE_CONDITION_FACT_ITEM_COUNT);
static_assert(static_cast<int>(ConditionFact::is_alias_ref) == RADICLE_CONDITION_FACT_IS_ALIAS_REF);

constexpr std::uint8_t k_needs_more_context =
    fo4::k_ctda_flag_aliases | fo4::k_ctda_flag_pack_data | fo4::k_ctda_flag_swap;

radicle_status check_live(const radicle_condition_live_state* live) {
  if (live == nullptr || live->struct_size < sizeof(radicle_condition_live_state) || live->query == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a live state with struct_size and query is required");
  }
  return RADICLE_STATUS_OK;
}

// Constraint: the value is narrowed to float because a stored comparison is a float, so a host double must round the same way; a non-finite or out-of-float-range number is unknown.
bool ask(const radicle_condition_live_state& live, const radicle_condition_query& query, double& out) {
  double value = 0.0;
  if (live.query(live.user, &query, &value) == 0) return false;
  if (!std::isfinite(value) || std::fabs(value) > static_cast<double>(std::numeric_limits<float>::max())) return false;
  out = static_cast<double>(static_cast<float>(value));
  return true;
}

bool resolved_form(const radicle_condition_param& param) {
  return param.kind == RADICLE_CONDITION_PARAM_KIND_FORM && param.form.plugin != nullptr;
}

struct GlobAnswer {
  radicle_glob_live_reason reason{RADICLE_GLOB_LIVE_NO_VALUE};
  double value{0.0};
};

// Requires: ref is the GLOB winner for key or NULL; throws GameplayError when a Constant GLOB's stored value is malformed.
GlobAnswer glob_answer(const radicle_condition_live_state& live, radicle_form_key key, const fo4::env::RecordRef* ref) {
  radicle_condition_query query{};
  query.fact = RADICLE_CONDITION_FACT_GLOBAL_VALUE;
  query.form = key;
  GlobAnswer answer;
  if (ask(live, query, answer.value)) {
    answer.reason = RADICLE_GLOB_LIVE_HOST;
    return answer;
  }
  answer.value = 0.0;
  if (ref == nullptr) return answer;
  const auto stored = fo4::forms::read_global_value(*ref, true);
  if (stored.reason == fo4::forms::GlobalValueReason::ok) {
    answer.reason = RADICLE_GLOB_LIVE_RECORD_CONSTANT;
    answer.value = stored.value;
  }
  return answer;
}

// Constraint: only a QUST row (itself) and a SCEN row (its parent quest) name their quest; any other record, and a malformed scene, give a NULL-plugin key.
radicle_form_key owning_quest(const radicle_env& env, const radicle_record& record) {
  const auto& ref = *to_ref(&record);
  if (ref.record->header.signature == fo4::Signature::from_string("QUST")) return to_c_key(env, ref.record->form_key);
  if (ref.record->header.signature != fo4::Signature::from_string("SCEN")) return {};
  try {
    return to_c_key(env, fo4::env::decode_scene(env.value, ref).parent_quest);
  } catch (const std::exception&) {
    return {};
  }
}

radicle_condition_value value_of(const radicle_env& env, const radicle_record& record,
                                 const radicle_condition_live_state& live, const radicle_condition& row,
                                 const radicle_condition_typed& typed) {
  radicle_condition_value out{};
  if ((row.flags_byte & k_needs_more_context) != 0 || typed.function_name == nullptr) return out;
  const auto fact = fo4::env::condition_fact(typed.function_name);
  if (!fact) return out;
  radicle_condition_query query{};
  query.fact = static_cast<radicle_condition_fact>(*fact);
  query.run_on = row.run_on;
  query.reference_raw = row.reference;
  query.param3 = row.unknown3;
  if (resolved_form(typed.reference)) query.reference = typed.reference.form;
  if (fo4::env::condition_fact_needs_form(*fact)) {
    if (!resolved_form(typed.param1)) return out;
    query.form = typed.param1.form;
  }
  if (*fact == ConditionFact::quest_stage_done) query.integer = row.param2;
  if (*fact == ConditionFact::is_alias_ref) {
    query.integer = typed.param1.raw;
    query.form = owning_quest(env, record);
  }
  double value = 0.0;
  if (!ask(live, query, value)) return out;
  if (*fact == ConditionFact::is_sex) value = value == static_cast<double>(typed.param1.raw) ? 1.0 : 0.0;
  out.known = 1;
  out.value = value;
  out.resolved_run_on = row.run_on;
  out.resolved_reference = row.reference;
  if ((row.flags_byte & fo4::k_ctda_flag_use_global) != 0 && resolved_form(typed.comparison)) {
    const auto key = to_form_key(typed.comparison.form);
    const auto* ref = key ? env.value.winner(*key) : nullptr;
    try {
      const auto answer = glob_answer(live, typed.comparison.form, ref);
      if (answer.reason != RADICLE_GLOB_LIVE_NO_VALUE) {
        out.global_known = 1;
        out.global_value = answer.value;
      }
    } catch (const fo4::forms::GameplayError&) {
    }
  }
  return out;
}

}  // namespace

extern "C" {

radicle_status radicle_condition_live_values(const radicle_env* env, const radicle_record* record,
                                             const radicle_condition* rows, size_t count,
                                             const radicle_condition_live_state* live,
                                             radicle_condition_value* out_values) {
  clear_error();
  if (const auto status = check_live(live); status != RADICLE_STATUS_OK) return status;
  if (env == nullptr || record == nullptr || (count != 0 && (rows == nullptr || out_values == nullptr))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record, rows and out_values are required");
  }
  return guarded("unknown exception while reading live condition values", [&] {
    std::vector<radicle_condition_typed> typed(count);
    if (const auto status = radicle_condition_params(env, record, rows, count, typed.data());
        status != RADICLE_STATUS_OK) {
      return status;
    }
    for (size_t i = 0; i < count; ++i) out_values[i] = value_of(*env, *record, *live, rows[i], typed[i]);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_conditions_evaluate_live(const radicle_env* env, const radicle_record* record,
                                                const radicle_condition* rows, size_t count,
                                                const radicle_condition_live_state* live,
                                                radicle_condition_result* out) {
  clear_error();
  if (out == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out is required");
  if (const auto status = check_live(live); status != RADICLE_STATUS_OK) return status;
  if (env == nullptr || record == nullptr || (count != 0 && rows == nullptr)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and rows are required");
  }
  return guarded("unknown exception while evaluating live conditions", [&] {
    std::vector<radicle_condition_value> values(count);
    if (const auto status = radicle_condition_live_values(env, record, rows, count, live, values.data());
        status != RADICLE_STATUS_OK) {
      return status;
    }
    return radicle_conditions_evaluate(rows, values.data(), count, out);
  });
}

radicle_status radicle_glob_value_live(const radicle_env* env, const radicle_record* glob,
                                       const radicle_condition_live_state* live, double* out_value,
                                       radicle_glob_live_reason* out_reason) {
  clear_error();
  if (out_value == nullptr || out_reason == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_value and out_reason are required");
  }
  *out_value = 0.0;
  *out_reason = RADICLE_GLOB_LIVE_NO_VALUE;
  if (const auto status = check_live(live); status != RADICLE_STATUS_OK) return status;
  if (const auto status = require_env_record(env, glob, out_value); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading a live GLOB value", [&] {
    const auto& ref = *to_ref(glob);
    if (ref.record->header.signature != fo4::Signature::from_string("GLOB")) {
      *out_reason = RADICLE_GLOB_LIVE_NOT_GLOB;
      return RADICLE_STATUS_OK;
    }
    try {
      const auto answer = glob_answer(*live, to_c_key(*env, ref.record->form_key), &ref);
      *out_value = answer.value;
      *out_reason = answer.reason;
    } catch (const fo4::forms::GameplayError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
