#include "c_api_env_internal.hpp"
#include "fo4/c_api_materials.h"
#include "fo4/environment/material_swap.hpp"

#include <memory>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct SwapHolder {
  radicle_material_swap view{};
  std::vector<fo4::env::MaterialSubstitution> data;
  std::vector<radicle_material_substitution> substitutions;
};

radicle_status require(const radicle_env* env, const radicle_record* record, const void* out) {
  if (env == nullptr || record == nullptr || out == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and an output are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_material_swap_decode(const radicle_env* env,
                                            const radicle_record* record,
                                            radicle_material_swap** out_swap) {
  clear_error();
  if (const auto status = require(env, record, out_swap); status != RADICLE_STATUS_OK) return status;
  *out_swap = nullptr;
  return guarded("unknown exception while decoding MSWP", [&] {
    auto holder = std::make_unique<SwapHolder>();
    holder->data = fo4::env::decode_material_swap(env->value, *to_ref(record));
    for (const auto& s : holder->data) {
      radicle_material_substitution c{};
      c.original_material = stored_text(*env, s.original_material);
      c.replacement_material = stored_text(*env, s.replacement_material);
      set_optional(s.color_remapping_index, c.has_color_remapping_index, c.color_remapping_index);
      holder->substitutions.push_back(c);
    }
    auto& v = holder->view;
    v.substitutions = holder->substitutions.empty() ? nullptr : holder->substitutions.data();
    v.substitution_count = holder->substitutions.size();
    v.internal = holder.get();
    *out_swap = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_material_swap_free(radicle_material_swap* swap) {
  clear_error();
  if (swap != nullptr) delete static_cast<SwapHolder*>(swap->internal);
}

radicle_status radicle_placed_reference_material_swap(const radicle_env* env,
                                                      const radicle_record* record,
                                                      radicle_form_key* out_swap) {
  clear_error();
  if (const auto status = require(env, record, out_swap); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading XMSP", [&] {
    *out_swap = to_c_key(*env, fo4::env::placed_material_swap(env->value, *to_ref(record)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_primary_model_material_swap(const radicle_env* env,
                                                          const radicle_record* record,
                                                          radicle_form_key* out_swap) {
  clear_error();
  if (const auto status = require(env, record, out_swap); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading the model material swap", [&] {
    *out_swap = to_c_key(*env, fo4::env::primary_model_material_swap(env->value, *to_ref(record)));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_record_primary_model_color_remapping_index(const radicle_env* env,
                                                                 const radicle_record* record,
                                                                 float* out_index) {
  clear_error();
  if (const auto status = require(env, record, out_index); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while reading the model color remapping index", [&] {
    const auto index = fo4::env::primary_model_color_remapping_index(env->value, *to_ref(record));
    if (!index) return fail(RADICLE_STATUS_NOT_FOUND, "the primary model has no color remapping index");
    *out_index = *index;
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
