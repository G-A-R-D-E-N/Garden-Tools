#include "fo4/c_api_loose_plugins.h"
#include "fo4/plugin/loose_plugins.hpp"
#include "c_api_internal.hpp"

#include <memory>

struct radicle_loose_plugins {
  fo4::LoosePlugins value;
};

using namespace radicle_c_detail;

extern "C" {

radicle_status radicle_loose_plugins_create(radicle_loose_plugins** out_plugins) {
  clear_error();
  if (!out_plugins) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_plugins is required");
  *out_plugins = nullptr;
  return guard_exceptions("creating loose plugins failed", [&] {
    *out_plugins = new radicle_loose_plugins{};
    return RADICLE_STATUS_OK;
  });
}

void radicle_loose_plugins_destroy(radicle_loose_plugins* plugins) {
  clear_error();
  delete plugins;
}

radicle_status radicle_loose_plugins_replace(radicle_loose_plugins* plugins, const char* name, radicle_plugin* plugin) {
  clear_error();
  if (!plugins || !name || !*name || !plugin) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugins, name and plugin are required");
  return guard_exceptions("replacing loose plugin failed", [&] {
    plugins->value.replace(name, plugin->owner);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_loose_plugins_release(radicle_loose_plugins* plugins, const char* name) {
  clear_error();
  if (!plugins || !name || !*name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugins and name are required");
  return guard_exceptions("releasing loose plugin failed", [&] {
    plugins->value.release(name);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_loose_plugins_set_editable(radicle_loose_plugins* plugins, const char* name, radicle_plugin* plugin) {
  clear_error();
  if (!plugins || !name || !*name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugins and name are required");
  return guard_exceptions("setting editable plugin failed", [&] {
    plugins->value.set_editable(name, plugin ? plugin->owner : nullptr);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_loose_plugins_get(radicle_loose_plugins* plugins, const char* name, radicle_plugin** out_plugin) {
  clear_error();
  if (!plugins || !name || !*name || !out_plugin) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugins, name and out_plugin are required");
  *out_plugin = nullptr;
  return guard_exceptions("getting loose plugin failed", [&] {
    auto plugin = plugins->value.get(name);
    if (!plugin) return RADICLE_STATUS_NOT_FOUND;
    *out_plugin = new radicle_plugin(std::move(plugin));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_loose_plugins_index(radicle_loose_plugins* plugins, const char* name) {
  clear_error();
  if (!plugins || !name || !*name) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugins and name are required");
  return guard_exceptions("indexing loose plugin failed", [&] {
    return plugins->value.index(name) ? RADICLE_STATUS_OK : RADICLE_STATUS_NOT_FOUND;
  });
}

radicle_status radicle_loose_plugins_has_index(radicle_loose_plugins* plugins, const char* name, int32_t* out_has_index) {
  clear_error();
  if (!plugins || !name || !*name || !out_has_index) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugins, name and out_has_index are required");
  return guard_exceptions("checking loose plugin index failed", [&] {
    *out_has_index = plugins->value.has_index(name) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

}
