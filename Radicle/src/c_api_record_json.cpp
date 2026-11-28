#include "fo4/c_api_record_json.h"

#include "c_api_env_internal.hpp"
#include "fo4/graph/record_json.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

}

extern "C" {

radicle_status radicle_env_record_json(const radicle_env* env,
                                       const char* plugin,
                                       const char* form_key,
                                       int32_t include_raw,
                                       char*** out_strings,
                                       size_t* out_count) {
  clear_error();
  if (env == nullptr || plugin == nullptr || form_key == nullptr || out_strings == nullptr ||
      out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, plugin, form_key, out_strings and out_count are required");
  }
  *out_strings = nullptr;
  *out_count = 0;
  return guarded("unknown exception while exporting record JSON", [&] {
    const auto index = env->value.find_plugin(plugin);
    if (!index) return fail(RADICLE_STATUS_NOT_FOUND, "plugin is not loaded in this environment");
    fo4::FormKey key;
    try {
      key = fo4::FormKey::parse(form_key);
    } catch (const std::exception& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
    try {
      const fo4::RecordJsonOptions options{.include_raw = include_raw != 0};
      const auto json = fo4::record_to_json(env->value.plugin(*index), key, options);
      return copy_strings(std::vector<std::string>{json}, out_strings, out_count);
    } catch (const std::out_of_range& error) {
      return fail_exception(RADICLE_STATUS_NOT_FOUND, error);
    }
  });
}

}
