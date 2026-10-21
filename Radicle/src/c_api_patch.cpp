#include "c_api_env_internal.hpp"
#include "fo4/c_api_patch.h"
#include "fo4/forms/leveled_list.hpp"
#include "fo4/patch/delta_patch.hpp"
#include "fo4/patch/list_merge.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"

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

radicle_status radicle_env_build_list_merge_patch(const radicle_env* env,
                                                  const char* signature,
                                                  const char* patch_name,
                                                  const char* output_path,
                                                  radicle_list_merge_counts* out_counts) {
  clear_error();
  if (out_counts != nullptr) *out_counts = radicle_list_merge_counts{0, 0};
  if (env == nullptr || patch_name == nullptr || output_path == nullptr || out_counts == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "env, the patch name, the output path and out_counts are required");
  }
  const auto record_signature = to_signature(signature);
  if (!record_signature.has_value()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature must be four characters");
  }
  return guarded("unknown exception while merging lists", [&] {
    try {
      auto result = fo4::patch::build_list_merge_patch(env->value, *record_signature, patch_name);
      fo4::write_plugin_file(result.patch, utf8_path(output_path));
      out_counts->merged = result.counts.merged;
      out_counts->unchanged = result.counts.unchanged;
      return RADICLE_STATUS_OK;
    } catch (const fo4::patch::ListMergeError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::forms::LeveledListError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::FormLinkListError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // extern "C"
