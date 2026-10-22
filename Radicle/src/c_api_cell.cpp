#include "c_api_forms_internal.hpp"
#include "fo4/c_api_cell.h"
#include "fo4/forms/cell.hpp"

#include <optional>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

const fo4::env::RecordRef* ref_for(const radicle_env& env,
                                   const fo4::FormKey& key,
                                   const fo4::Record* record) {
  for (const auto& version : env.value.versions(key)) {
    if (version.record == record) return &version;
  }
  return nullptr;
}

radicle_status find_cell(const radicle_env* env,
                         radicle_form_key worldspace,
                         int32_t x,
                         int32_t y,
                         bool persistent,
                         const radicle_record** out_record) {
  if (env == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_record are required");
  }
  *out_record = nullptr;
  const auto key = to_form_key(worldspace);
  if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid worldspace key is required");
  if (!persistent && (!fo4::forms::exterior_cell_coordinate_valid(x) ||
                      !fo4::forms::exterior_cell_coordinate_valid(y))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "exterior CELL coordinates must be within -110..110");
  }
  return guarded("unknown exception while finding an exterior cell", [&] {
    const auto index = env->value.find_plugin(key->owner().filename());
    if (!index) return fail(RADICLE_STATUS_NOT_FOUND, "the worldspace plugin is not loaded");
    try {
      const auto& plugin = env->value.plugin(*index);
      const auto* record = persistent ? fo4::forms::find_persistent_exterior_cell(plugin, *key)
                                      : fo4::forms::find_exterior_cell(plugin, *key, x, y);
      if (record == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "no matching exterior cell");
      const auto* ref = ref_for(*env, record->form_key, record);
      if (ref == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "cell is not indexed by the environment");
      *out_record = to_handle(ref);
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::CellError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

}  // namespace

extern "C" {

radicle_status radicle_exterior_cell_layout_for(int32_t x,
                                                int32_t y,
                                                radicle_exterior_cell_layout* out_layout) {
  clear_error();
  if (out_layout == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_layout is required");
  return guarded("unknown exception while computing an exterior cell layout", [&] {
    try {
      const auto layout = fo4::forms::exterior_cell_grid_layout(x, y);
      out_layout->cell_x = layout.cell_x;
      out_layout->cell_y = layout.cell_y;
      out_layout->block_x = layout.block_x;
      out_layout->block_y = layout.block_y;
      out_layout->subblock_x = layout.subblock_x;
      out_layout->subblock_y = layout.subblock_y;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::CellError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_exterior_cell_coordinate_valid(int32_t coordinate, int32_t* out_valid) {
  clear_error();
  if (out_valid == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "out_valid is required");
  *out_valid = fo4::forms::exterior_cell_coordinate_valid(coordinate) ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_plugin_create_exterior_cell(radicle_plugin* plugin,
                                                   const radicle_exterior_cell_creation* creation,
                                                   radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || creation == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, creation and out_key are required");
  }
  const auto worldspace = to_form_key(creation->worldspace);
  if (!worldspace) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid worldspace key is required");
  if (!text_representable(creation->editor_id)) return bad_text();
  return guarded("unknown exception while creating an exterior cell", [&] {
    fo4::forms::ExteriorCellCreation input;
    input.worldspace = *worldspace;
    input.x = creation->x;
    input.y = creation->y;
    input.editor_id = optional_view(creation->editor_id);
    input.flags = creation->flags;
    try {
      const auto key = fo4::forms::create_exterior_cell(plugin->value, input);
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::CellError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_create_interior_cell(radicle_plugin* plugin,
                                                   const char* editor_id,
                                                   radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || editor_id == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, editor ID and out_key are required");
  }
  if (!text_representable(editor_id)) return bad_text();
  return guarded("unknown exception while creating an interior cell", [&] {
    try {
      const auto key = fo4::forms::create_interior_cell(plugin->value, editor_id);
      *out_key = {plugin->value.key.filename().data(), key.local().value()};
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::CellError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_env_find_exterior_cell(const radicle_env* env,
                                              radicle_form_key worldspace,
                                              int32_t x,
                                              int32_t y,
                                              const radicle_record** out_record) {
  clear_error();
  return find_cell(env, worldspace, x, y, false, out_record);
}

radicle_status radicle_env_find_persistent_exterior_cell(const radicle_env* env,
                                                         radicle_form_key worldspace,
                                                         const radicle_record** out_record) {
  clear_error();
  return find_cell(env, worldspace, 0, 0, true, out_record);
}

}  // extern "C"
