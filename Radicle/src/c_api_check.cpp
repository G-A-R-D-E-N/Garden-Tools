#include "fo4/c_api_env.h"

#include "c_api_env_internal.hpp"
#include "fo4/graph/check_errors.hpp"

#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

}  // namespace

extern "C" {

radicle_status radicle_env_check_errors(const radicle_env* env,
                                        const char* plugin,
                                        size_t limit,
                                        char*** out_strings,
                                        size_t* out_count) {
  clear_error();
  if (env == nullptr || out_strings == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, out_strings and out_count are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  return guarded("unknown exception while checking for errors", [&] {
    std::vector<const fo4::Plugin*> load_order;
    for (std::size_t i = 0; i < env->value.plugins().size(); ++i) {
      load_order.push_back(&env->value.plugin(i));
    }
    fo4::CheckOptions options;
    options.limit = limit;
    if (plugin != nullptr) {
      std::size_t index = 0;
      if (radicle_env_find_plugin(env, plugin, &index) != RADICLE_STATUS_OK) {
        return fail(RADICLE_STATUS_NOT_FOUND, (std::string("plugin is not loaded: ") + plugin).c_str());
      }
      options.plugins.push_back(load_order[index]->key);
    }

    std::vector<std::string> rows;
    for (const auto& finding : fo4::check_load_order(load_order, options)) {
      std::string row;
      row += fo4::check_code_name(finding.code);
      row += '\t';
      row += fo4::check_severity_name(finding.severity);
      row += '\t';
      row += finding.plugin.filename();
      row += '\t';
      row += finding.record.to_string();
      row += '\t';
      if (finding.subrecord != fo4::Signature{}) row += finding.subrecord.to_string();
      row += '\t';
      row += std::to_string(finding.offset);
      row += '\t';
      row += finding.target.to_string();
      row += '\t';
      row += finding.message;
      if (!finding.detail.empty()) row += " (" + finding.detail + ")";
      rows.push_back(std::move(row));
    }
    return copy_strings(rows, out_strings, out_count);
  });
}

}  // extern "C"
