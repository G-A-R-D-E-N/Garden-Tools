#include "fo4/c_api_itpo.h"

#include "c_api_env_internal.hpp"
#include "fo4/plugin/itpo.hpp"

#include <string>
#include <utility>
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

}

extern "C" {

radicle_status radicle_env_identical_to_previous_override(const radicle_env* env,
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
  return guarded("unknown exception while finding identical-to-previous-override records", [&] {
    std::size_t index = 0;
    if (const auto status = find_index(env, plugin, index); status != RADICLE_STATUS_OK) return status;
    std::vector<const fo4::Plugin*> load_order;
    for (std::size_t i = 0; i <= index; ++i) load_order.push_back(&env->value.plugin(i));
    std::vector<std::string> rows;
    for (const auto& item : fo4::find_identical_to_previous_override(load_order, index, limit)) {
      rows.push_back(item.key.to_string() + '\t' + item.signature.to_string() + '\t' +
                     std::string(item.previous.filename()));
    }
    return copy_strings(rows, out_strings, out_count);
  });
}

radicle_status radicle_plugin_remove_identical_to_previous_override(radicle_plugin* plugin,
                                                                    const radicle_env* env,
                                                                    size_t* out_removed) {
  clear_error();
  if (plugin == nullptr || env == nullptr || out_removed == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env and out_removed are required");
  }
  *out_removed = 0;
  return guarded("unknown exception while removing identical-to-previous-override records", [&] {
    const std::string name(plugin->value.key.filename());
    std::size_t index = 0;
    if (const auto status = find_index(env, name.c_str(), index); status != RADICLE_STATUS_OK) return status;
    std::vector<const fo4::Plugin*> load_order;
    for (std::size_t i = 0; i <= index; ++i) load_order.push_back(&env->value.plugin(i));
    auto next = plugin->value;
    const auto removed = fo4::remove_identical_to_previous_override(next, load_order, index);
    plugin->value = std::move(next);
    *out_removed = removed;
    return RADICLE_STATUS_OK;
  });
}

}
