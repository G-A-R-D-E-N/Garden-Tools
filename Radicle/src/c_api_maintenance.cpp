#include "fo4/c_api_maintenance.h"

#include "c_api_env_internal.hpp"
#include "fo4/plugin/maintenance.hpp"
#include "fo4/plugin/master_usage.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status find_index(const radicle_env* env, const char* plugin, std::size_t& index) {
  if (radicle_env_find_plugin(env, plugin, &index) != RADICLE_STATUS_OK) {
    return fail(RADICLE_STATUS_NOT_FOUND, (std::string("plugin is not loaded: ") + plugin).c_str());
  }
  return RADICLE_STATUS_OK;
}

}  // namespace

extern "C" {

radicle_status radicle_env_identical_to_master(const radicle_env* env,
                                               const char* plugin,
                                               size_t limit,
                                               char*** out_strings,
                                               size_t* out_count) {
  clear_error();
  if (env == nullptr || plugin == nullptr || out_strings == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, plugin, out_strings and out_count are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  return guarded("unknown exception while finding identical-to-master records", [&] {
    std::size_t index = 0;
    if (const auto status = find_index(env, plugin, index); status != RADICLE_STATUS_OK) return status;
    std::vector<const fo4::Plugin*> load_order;
    for (std::size_t i = 0; i <= index; ++i) load_order.push_back(&env->value.plugin(i));
    std::vector<std::string> rows;
    for (const auto& item : fo4::find_identical_to_master(load_order, index, limit)) {
      rows.push_back(item.key.to_string() + '\t' + item.signature.to_string() + '\t' +
                     std::string(item.previous.filename()));
    }
    return copy_strings(rows, out_strings, out_count);
  });
}

radicle_status radicle_env_deleted_counts(const radicle_env* env,
                                          const char* plugin,
                                          size_t* out_records,
                                          size_t* out_references,
                                          size_t* out_navmeshes) {
  clear_error();
  if (env == nullptr || plugin == nullptr || out_records == nullptr || out_references == nullptr ||
      out_navmeshes == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, plugin and the three count outputs are required");
  }
  *out_records = *out_references = *out_navmeshes = 0;
  return guarded("unknown exception while counting deleted records", [&] {
    std::size_t index = 0;
    if (const auto status = find_index(env, plugin, index); status != RADICLE_STATUS_OK) return status;
    const auto counts = fo4::count_deleted_records(env->value.plugin(index));
    *out_records = counts.records;
    *out_references = counts.references;
    *out_navmeshes = counts.navmeshes;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_unused_masters(const radicle_env* env,
                                          const char* plugin,
                                          char*** out_strings,
                                          size_t* out_count,
                                          int32_t* out_complete) {
  clear_error();
  if (env == nullptr || plugin == nullptr || out_strings == nullptr || out_count == nullptr ||
      out_complete == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "env, plugin, out_strings, out_count and out_complete are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  *out_complete = 0;
  return guarded("unknown exception while scanning master usage", [&] {
    std::size_t index = 0;
    if (const auto status = find_index(env, plugin, index); status != RADICLE_STATUS_OK) return status;
    const auto& target = env->value.plugin(index);
    const auto usage = fo4::scan_master_usage(target);
    const auto kept = fo4::used_masters(target, usage);
    std::vector<std::string> unused;
    for (const auto& master : target.header.masters) {
      const bool used = std::any_of(kept.begin(), kept.end(),
                                    [&](const auto& item) { return item.key == master.key; });
      if (!used) unused.emplace_back(master.key.filename());
    }
    *out_complete = usage.complete() ? 1 : 0;
    return copy_strings(unused, out_strings, out_count);
  });
}

radicle_status radicle_env_esl_eligibility(const radicle_env* env,
                                           const char* plugin,
                                           size_t* out_native_records,
                                           size_t* out_out_of_range,
                                           size_t* out_capacity) {
  clear_error();
  if (env == nullptr || plugin == nullptr || out_native_records == nullptr || out_out_of_range == nullptr ||
      out_capacity == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, plugin and the three outputs are required");
  }
  *out_native_records = *out_out_of_range = *out_capacity = 0;
  return guarded("unknown exception while checking ESL eligibility", [&] {
    std::size_t index = 0;
    if (const auto status = find_index(env, plugin, index); status != RADICLE_STATUS_OK) return status;
    const auto esl = fo4::esl_eligibility(env->value.plugin(index));
    *out_native_records = esl.native_records;
    *out_out_of_range = esl.out_of_range;
    *out_capacity = esl.capacity;
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
