#include "c_api_forms_internal.hpp"
#include "fo4/c_api_cell.h"
#include "fo4/environment/cell_view.hpp"
#include "fo4/forms/cell.hpp"

#include <deque>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace radicle_c_cell_detail {

using namespace radicle_c_forms_detail;

struct BaseStorage {
  TextStore text;
  std::vector<radicle_scol_part> parts;
  std::vector<std::vector<radicle_static_placement>> placements;
  radicle_base_object_info info{};
};

struct CellViewStorage {
  TextStore text;
  radicle_cell_summary summary{};
  std::deque<BaseStorage> bases;
  std::vector<radicle_cell_reference> references;
};

}  // namespace radicle_c_cell_detail

struct radicle_cell_view {
  radicle_c_cell_detail::CellViewStorage value;
};

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;
using namespace radicle_c_cell_detail;

radicle_static_placement to_c_placement(const fo4::forms::StaticPlacementView& in) {
  return {{in.x, in.y, in.z}, {in.rx, in.ry, in.rz}, in.scale};
}

void fill_base(const radicle_env& env, const fo4::env::BaseObjectInfo& in, BaseStorage& store) {
  auto& out = store.info;
  out.key = to_c_key(env, in.key);
  const auto signature = in.signature.view();
  for (std::size_t i = 0; i < 4; ++i) out.signature[i] = signature[i];
  out.editor_id = store.text.keep(std::string_view{in.editor_id});
  out.type_name = store.text.keep(std::string_view{in.type_name});
  out.model_path = store.text.keep(std::string_view{in.model_path});
  out.editor_marker = in.editor_marker ? 1 : 0;
  if (in.decal) {
    out.has_decal = 1;
    out.decal_diffuse = store.text.keep(std::string_view{in.decal->diffuse});
    set_optional(in.decal->width, out.has_decal_width, out.decal_width);
    set_optional(in.decal->height, out.has_decal_height, out.decal_height);
  }
  for (const auto& part : in.scol_parts) {
    std::vector<radicle_static_placement> placements;
    placements.reserve(part.placements.size());
    for (const auto& placement : part.placements) placements.push_back(to_c_placement(placement));
    store.placements.push_back(std::move(placements));
    const auto& stored = store.placements.back();
    store.parts.push_back({store.text.keep(std::string_view{part.model_path}), stored.data(),
                           stored.size()});
  }
  out.scol_parts = store.parts.data();
  out.scol_part_count = store.parts.size();
}

void fill_summary(const radicle_env& env, const fo4::env::CellSummary& in, CellViewStorage& view) {
  auto& out = view.summary;
  out.key = to_c_key(env, in.key);
  out.editor_id = view.text.keep(std::string_view{in.editor_id});
  out.name_kind = static_cast<radicle_name_kind>(in.name.kind);
  out.name_text = in.name.kind == fo4::env::NameKind::inline_text
                      ? view.text.keep(in.name.text)
                      : nullptr;
  out.name_string_id = in.name.string_id;
  out.interior = in.interior ? 1 : 0;
  if (in.grid) {
    out.has_grid = 1;
    out.grid_x = in.grid->x;
    out.grid_y = in.grid->y;
  }
  out.has_worldspace = in.worldspace ? 1 : 0;
  out.worldspace = to_c_key(env, in.worldspace);
  out.winning_plugin = in.winning_plugin;
  out.localized = in.localized ? 1 : 0;
}

radicle_cell_reference to_c_reference(const radicle_env& env,
                                      const fo4::env::CellReferenceListing& in,
                                      CellViewStorage& view) {
  radicle_cell_reference out{};
  out.key = to_c_key(env, in.key);
  out.record_type = view.text.keep(std::string_view{in.record_type});
  out.editor_id = view.text.keep(std::string_view{in.editor_id});
  out.has_base = in.base ? 1 : 0;
  out.base = to_c_key(env, in.base);
  if (in.base_info) {
    view.bases.emplace_back();
    auto& base = view.bases.back();
    fill_base(env, *in.base_info, base);
    out.base_info = &base.info;
  }
  out.position[0] = in.transform.x;
  out.position[1] = in.transform.y;
  out.position[2] = in.transform.z;
  out.rotation[0] = in.transform.rx;
  out.rotation[1] = in.transform.ry;
  out.rotation[2] = in.transform.rz;
  out.scale = in.scale;
  out.persistent = in.persistent ? 1 : 0;
  out.initially_disabled = in.initially_disabled ? 1 : 0;
  out.winning_plugin = in.winning_plugin;
  return out;
}

}  // namespace

extern "C" {

radicle_status radicle_env_resolve_cell(const radicle_env* env,
                                        const char* id,
                                        radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (env == nullptr || id == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, id and out_key are required");
  }
  return guarded("unknown exception while resolving a cell", [&] {
    const auto key = fo4::env::resolve_cell_id(env->value, id);
    if (!key) return fail(RADICLE_STATUS_NOT_FOUND, "no CELL matches the identifier");
    *out_key = to_c_key(*env, key);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_resolve_worldspace(const radicle_env* env,
                                              const char* id,
                                              radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (env == nullptr || id == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, id and out_key are required");
  }
  return guarded("unknown exception while resolving a worldspace", [&] {
    const auto key = fo4::env::resolve_worldspace_id(env->value, id);
    if (!key) return fail(RADICLE_STATUS_NOT_FOUND, "no WRLD matches the identifier");
    *out_key = to_c_key(*env, key);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_find_cell_at_grid(const radicle_env* env,
                                             radicle_form_key world,
                                             int32_t x,
                                             int32_t y,
                                             radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (env == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_key are required");
  }
  return guarded("unknown exception while finding a cell at a grid", [&] {
    const auto worldspace = to_form_key(world);
    if (!worldspace) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid worldspace key is required");
    if (!fo4::forms::exterior_cell_coordinate_valid(x) || !fo4::forms::exterior_cell_coordinate_valid(y)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "exterior CELL coordinates must be within -110..110");
    }
    const auto cell = fo4::env::find_cell_at_grid(env->value, *worldspace, x, y);
    if (!cell) return fail(RADICLE_STATUS_NOT_FOUND, "no exterior CELL at that grid");
    *out_key = to_c_key(*env, cell);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_env_cell_view(const radicle_env* env,
                                     radicle_form_key cell,
                                     radicle_cell_view** out_view) {
  clear_error();
  if (out_view != nullptr) *out_view = nullptr;
  if (env == nullptr || out_view == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_view are required");
  }
  return guarded("unknown exception while listing cell references", [&] {
    const auto key = to_form_key(cell);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid CELL key is required");
    fo4::env::CellReferences data;
    try {
      data = fo4::env::list_cell_references(env->value, *key);
    } catch (const fo4::env::CellViewError&) {
      return fail(RADICLE_STATUS_NOT_FOUND, "no CELL with that key");
    }
    auto view = std::make_unique<radicle_cell_view>();
    fill_summary(*env, data.cell, view->value);
    for (const auto& listing : data.references) {
      view->value.references.push_back(to_c_reference(*env, listing, view->value));
    }
    *out_view = view.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_cell_view_summary(const radicle_cell_view* view,
                                         radicle_cell_summary* out_summary) {
  clear_error();
  if (view == nullptr || out_summary == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "view and out_summary are required");
  }
  *out_summary = view->value.summary;
  return RADICLE_STATUS_OK;
}

size_t radicle_cell_view_reference_count(const radicle_cell_view* view) {
  return view == nullptr ? 0 : view->value.references.size();
}

radicle_status radicle_cell_view_reference(const radicle_cell_view* view,
                                           size_t index,
                                           radicle_cell_reference* out_reference) {
  clear_error();
  if (view == nullptr || out_reference == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "view and out_reference are required");
  }
  if (index >= view->value.references.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "reference index is out of range");
  }
  *out_reference = view->value.references[index];
  return RADICLE_STATUS_OK;
}

radicle_status radicle_cell_view_scol_part(const radicle_cell_view* view,
                                           size_t reference_index,
                                           size_t part_index,
                                           radicle_scol_part* out_part) {
  clear_error();
  if (view == nullptr || out_part == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "view and out_part are required");
  }
  if (reference_index >= view->value.references.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "reference index is out of range");
  }
  const auto* base = view->value.references[reference_index].base_info;
  if (base == nullptr || part_index >= base->scol_part_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "SCOL part index is out of range");
  }
  *out_part = base->scol_parts[part_index];
  return RADICLE_STATUS_OK;
}

void radicle_cell_view_free(radicle_cell_view* view) {
  clear_error();
  delete view;
}

radicle_status radicle_env_base_object_info(const radicle_env* env,
                                            radicle_form_key base,
                                            radicle_base_object_info** out_info) {
  clear_error();
  if (out_info != nullptr) *out_info = nullptr;
  if (env == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env and out_info are required");
  }
  return guarded("unknown exception while describing a base object", [&] {
    const auto key = to_form_key(base);
    if (!key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid base object key is required");
    const auto info = fo4::env::describe_base_object(env->value, *key);
    if (!info) return fail(RADICLE_STATUS_NOT_FOUND, "no base object with that key");
    auto storage = std::make_unique<BaseStorage>();
    fill_base(*env, *info, *storage);
    storage->info.internal = storage.get();
    *out_info = &storage.release()->info;
    return RADICLE_STATUS_OK;
  });
}

void radicle_base_object_info_free(radicle_base_object_info* info) {
  clear_error();
  if (info == nullptr) return;
  delete static_cast<BaseStorage*>(info->internal);
}

}  // extern "C"
