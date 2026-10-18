#include "c_api_env_internal.hpp"
#include "fo4/c_api_patch.h"
#include "fo4/patch/delta_patch.hpp"
#include "fo4/plugin/writer.hpp"

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

extern "C" {

radicle_status radicle_env_build_delta_patch(const radicle_env* env,
                                             const char* older_plugin,
                                             const char* newer_plugin,
                                             const char* output_path,
                                             radicle_delta_patch_counts* out_counts) {
  clear_error();
  if (out_counts != nullptr) *out_counts = radicle_delta_patch_counts{0, 0};
  if (env == nullptr || older_plugin == nullptr || newer_plugin == nullptr ||
      output_path == nullptr || out_counts == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "env, both plugin names, the output path and out_counts are required");
  }
  const auto older = fo4::PluginKey::from_filename(older_plugin);
  const auto newer = fo4::PluginKey::from_filename(newer_plugin);
  if (!env->value.find_plugin(older.filename()).has_value()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the older plugin is not loaded");
  }
  if (!env->value.find_plugin(newer.filename()).has_value()) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the newer plugin is not loaded");
  }
  return guarded("unknown exception while building a delta patch", [&] {
    try {
      auto result = fo4::patch::build_delta_patch(env->value, older, newer);
      fo4::write_plugin_file(result.patch, utf8_path(output_path));
      out_counts->kept = result.counts.kept;
      out_counts->removed = result.counts.removed;
      return RADICLE_STATUS_OK;
    } catch (const fo4::patch::DeltaPatchError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // extern "C"
