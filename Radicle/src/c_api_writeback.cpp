#include "c_api_env_internal.hpp"
#include "fo4/c_api_object_template.h"
#include "fo4/forms/copy_as_new.hpp"
#include "fo4/forms/object_template.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/writer.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

struct ObjectTemplateRowsHolder {
  radicle_object_template_rows view{};
  fo4::forms::ObjectTemplateRows rows;
  std::vector<radicle_form_key> keywords;
  std::vector<radicle_object_template_include> includes;
  std::vector<radicle_object_template_property> properties;
};

radicle_form_key template_key(const fo4::FormKey& key) {
  return key.owner().empty() ? radicle_form_key{nullptr, 0}
                             : radicle_form_key{key.owner().filename().data(), key.local().value()};
}

std::optional<fo4::FormKey> template_input_key(radicle_form_key value) {
  if (value.plugin == nullptr && value.local_id == 0) return fo4::FormKey{};
  return to_form_key(value);
}

bool template_property_input(const radicle_object_template_property& input,
                             fo4::forms::ObjectTemplateProperty& output) {
  if (input.has_form_value > 1) return false;
  output = {input.value_type, input.function_type, input.property, input.value1,
            input.value2, input.step_bits, std::nullopt};
  if (input.has_form_value) {
    const auto key = template_input_key(input.form_value);
    if (!key) return false;
    output.form_value = *key;
  }
  return true;
}

radicle_status placed_error(const fo4::forms::PlacedReferenceError& error) {
  return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
}

// Every RecordCopyStatus must map in exactly one place; both record-copy exports publish through here.
template <typename CopyFn>
radicle_status publish_record_copy(const char* what,
                                   radicle_plugin* destination,
                                   const radicle_plugin* source,
                                   radicle_form_key record,
                                   int32_t overwrite,
                                   radicle_record_copy_status* out_status,
                                   CopyFn&& copy) {
  clear_error();
  const auto key = to_form_key(record);
  if (destination == nullptr || source == nullptr || !key || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "destination, source, a valid record and out_status are required");
  }
  *out_status = RADICLE_RECORD_COPY_INSERTED;
  return guarded(what, [&] {
    try {
      auto next = destination->value;
      switch (copy(next, source->value, *key, overwrite != 0)) {
        case fo4::RecordCopyStatus::inserted:
          *out_status = RADICLE_RECORD_COPY_INSERTED;
          break;
        case fo4::RecordCopyStatus::replaced:
          *out_status = RADICLE_RECORD_COPY_REPLACED;
          break;
        case fo4::RecordCopyStatus::already_exists:
          *out_status = RADICLE_RECORD_COPY_ALREADY_EXISTS;
          break;
        case fo4::RecordCopyStatus::source_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
        case fo4::RecordCopyStatus::source_containment_unsupported:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                      "source record's containment cannot be reproduced");
      }
      destination->value = std::move(next);
      return RADICLE_STATUS_OK;
    } catch (const fo4::FormIdError& error) {
      // Invariant: a malformed key is an argument error; guarded would report PARSE_ERROR.
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_create(const char* filename, radicle_plugin** out_plugin) {
  clear_error();
  if (filename == nullptr || *filename == '\0' || out_plugin == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "filename and out_plugin are required");
  }
  *out_plugin = nullptr;
  return guarded("unknown exception while creating plugin", [&] {
    auto plugin = std::make_unique<radicle_plugin>(fo4::make_empty_plugin(filename));
    *out_plugin = plugin.release();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_copy_placed_ref_override(radicle_plugin* plugin,
                                                       const radicle_env* env,
                                                       const radicle_record* record,
                                                       int32_t overwrite,
                                                       radicle_placed_copy_status* out_status) {
  clear_error();
  if (plugin == nullptr || env == nullptr || record == nullptr || out_status == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, record and out_status are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  const auto& ref = *to_ref(record);
  return guarded("unknown exception while copying placed reference", [&] {
    try {
      const auto status = fo4::forms::copy_placed_reference_as_override(
          plugin->value, env->value.plugin(ref.plugin), ref.record->form_key, overwrite != 0);
      switch (status) {
        case fo4::forms::PlacedReferenceCopyStatus::inserted:
          *out_status = RADICLE_PLACED_COPY_INSERTED;
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyStatus::replaced:
          *out_status = RADICLE_PLACED_COPY_REPLACED;
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyStatus::already_exists:
          *out_status = RADICLE_PLACED_COPY_ALREADY_EXISTS;
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyStatus::source_missing:
          break;
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_copy_placed_ref_new(radicle_plugin* plugin,
                                                  const radicle_env* env,
                                                  const radicle_record* record,
                                                  radicle_form_key* out_key) {
  clear_error();
  if (plugin == nullptr || env == nullptr || record == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, env, record and out_key are required");
  }
  if (!belongs_to_env(env, to_ref(record))) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to this environment");
  }
  *out_key = {nullptr, 0};
  const auto& ref = *to_ref(record);
  return guarded("unknown exception while copying placed reference", [&] {
    try {
      const auto result = fo4::forms::copy_placed_reference_as_new(
          plugin->value, env->value.plugin(ref.plugin), ref.record->form_key);
      switch (result.status) {
        case fo4::forms::PlacedReferenceCopyNewStatus::created:
          *out_key = {plugin->value.key.filename().data(), result.new_reference.local().value()};
          return RADICLE_STATUS_OK;
        case fo4::forms::PlacedReferenceCopyNewStatus::unsupported_signature:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a placed reference");
        case fo4::forms::PlacedReferenceCopyNewStatus::parent_cell_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin does not contain the reference's parent CELL");
        case fo4::forms::PlacedReferenceCopyNewStatus::source_missing:
          break;
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_copy_record_as_new(radicle_plugin* destination,
                                                 const radicle_plugin* source,
                                                 radicle_form_key record,
                                                 const char* editor_id,
                                                 const char* remove_prefix,
                                                 const char* remove_suffix,
                                                 const char* prefix,
                                                 const char* suffix,
                                                 radicle_form_key* out_key) {
  clear_error();
  const auto key = to_form_key(record);
  if (destination == nullptr || source == nullptr || !key || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "destination, source, a valid record and out_key are required");
  }
  *out_key = {nullptr, 0};
  return guarded("unknown exception while copying record", [&] {
    fo4::forms::RecordCopyNewOptions options;
    if (editor_id != nullptr) options.editor_id = editor_id;
    if (remove_prefix != nullptr) options.editor_id_remove_prefix = remove_prefix;
    if (remove_suffix != nullptr) options.editor_id_remove_suffix = remove_suffix;
    if (prefix != nullptr) options.editor_id_prefix = prefix;
    if (suffix != nullptr) options.editor_id_suffix = suffix;
    try {
      auto next = destination->value;
      const auto result = fo4::forms::copy_record_as_new(next, source->value, *key, options);
      switch (result.status) {
        case fo4::forms::RecordCopyNewStatus::created:
          destination->value = std::move(next);
          *out_key = {destination->value.key.filename().data(), result.new_record.local().value()};
          return RADICLE_STATUS_OK;
        case fo4::forms::RecordCopyNewStatus::unsupported_signature:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record type is not copied as new");
        case fo4::forms::RecordCopyNewStatus::parent_cell_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin does not contain the reference's parent CELL");
        case fo4::forms::RecordCopyNewStatus::parent_dial_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin does not contain the INFO's parent DIAL");
        case fo4::forms::RecordCopyNewStatus::source_missing:
          break;
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "source record is not in its plugin");
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    } catch (const fo4::forms::InfoCopyError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_copy_record_override(radicle_plugin* destination,
                                                   const radicle_plugin* source,
                                                   radicle_form_key record,
                                                   int32_t overwrite,
                                                   radicle_record_copy_status* out_status) {
  return publish_record_copy(
      "unknown exception while copying record as an override", destination, source, record,
      overwrite, out_status,
      [](fo4::Plugin& next, const fo4::Plugin& from, const fo4::FormKey& key, bool copy_over) {
        return fo4::copy_record_as_override(next, from, key, copy_over);
      });
}

radicle_status radicle_plugin_copy_record_deep(radicle_plugin* destination,
                                               const radicle_plugin* source,
                                               radicle_form_key record,
                                               int32_t overwrite,
                                               radicle_record_copy_status* out_status) {
  return publish_record_copy(
      "unknown exception while deep copying record", destination, source, record, overwrite,
      out_status,
      [](fo4::Plugin& next, const fo4::Plugin& from, const fo4::FormKey& key, bool copy_over) {
        return fo4::copy_record_deep(next, from, key, copy_over).status;
      });
}

radicle_status radicle_plugin_renumber_record(radicle_plugin* plugin,
                                              radicle_form_key record,
                                              uint32_t new_local_id) {
  clear_error();
  const auto key = to_form_key(record);
  if (plugin == nullptr || !key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record are required");
  }
  return guarded("unknown exception while renumbering record", [&] {
    try {
      switch (fo4::renumber_local_record(plugin->value, *key, fo4::LocalFormId{new_local_id})) {
        case fo4::RecordRenumberStatus::renumbered:
        case fo4::RecordRenumberStatus::unchanged:
          return RADICLE_STATUS_OK;
        case fo4::RecordRenumberStatus::record_missing:
          break;
        case fo4::RecordRenumberStatus::not_local:
        case fo4::RecordRenumberStatus::target_exists:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record cannot be renumbered to that ID");
      }
      return fail(RADICLE_STATUS_NOT_FOUND, "record is not in this plugin");
    } catch (const fo4::FormIdError& error) {
      // Out-of-range IDs and nested records must map to INVALID_ARGUMENT; both surface as throws.
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_inject_local_record(radicle_plugin* plugin,
                                                  radicle_form_key record,
                                                  radicle_form_key new_key) {
  clear_error();
  const auto key = to_form_key(record);
  const auto target = to_form_key(new_key);
  if (plugin == nullptr || !key || !target) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin and valid record and target keys are required");
  }
  return guarded("unknown exception while injecting record", [&] {
    try {
      switch (fo4::inject_local_record(plugin->value, *key, *target)) {
        case fo4::RecordRenumberStatus::renumbered:
        case fo4::RecordRenumberStatus::unchanged:
          return RADICLE_STATUS_OK;
        case fo4::RecordRenumberStatus::record_missing:
          break;
        case fo4::RecordRenumberStatus::not_local:
        case fo4::RecordRenumberStatus::target_exists:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                      "record cannot be injected into that key");
      }
    } catch (const fo4::FormIdError& error) {
      // Invariant: a foreign owner, a nested record and an out-of-range key are argument errors.
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
    return fail(RADICLE_STATUS_NOT_FOUND, "record is not in this plugin");
  });
}

radicle_status radicle_plugin_repoint_record_references(radicle_plugin** plugins,
                                                        size_t plugin_count,
                                                        radicle_form_key from,
                                                        radicle_form_key to,
                                                        size_t* out_reference_count) {
  clear_error();
  if (out_reference_count != nullptr) *out_reference_count = 0;
  const auto from_key = to_form_key(from);
  const auto to_key = to_form_key(to);
  if (plugins == nullptr || plugin_count == 0 || !from_key || !to_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugins and valid from and to keys are required");
  }
  for (size_t index = 0; index < plugin_count; ++index) {
    if (plugins[index] == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin handles must be non-null");
    }
    for (size_t earlier = 0; earlier < index; ++earlier) {
      if (plugins[earlier] == plugins[index]) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin handles must be distinct");
      }
    }
  }
  return guarded("unknown exception while repointing record references", [&] {
    std::vector<fo4::Plugin*> order;
    order.reserve(plugin_count);
    for (size_t index = 0; index < plugin_count; ++index) order.push_back(&plugins[index]->value);
    try {
      const auto result = fo4::repoint_record_references(order, *from_key, *to_key);
      if (result.status == fo4::ReferenceRepointStatus::mismatched_owners) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "from and to must name records in the same plugin");
      }
      if (out_reference_count != nullptr) *out_reference_count = result.references;
      return RADICLE_STATUS_OK;
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_set_placed_transform(radicle_plugin* plugin,
                                                   radicle_form_key reference,
                                                   const float position[3],
                                                   const float rotation[3]) {
  clear_error();
  return guarded("unknown exception while setting transform", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key || position == nullptr || rotation == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid reference, position and rotation are required");
    }
    try {
      const fo4::forms::PositionRotation transform{position[0], position[1], position[2],
                                                   rotation[0], rotation[1], rotation[2]};
      if (!fo4::forms::set_placed_reference_transform(plugin->value, *key, transform)) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_move_placed_ref(radicle_plugin* plugin,
                                              radicle_form_key reference,
                                              const float position[3],
                                              const float rotation[3],
                                              radicle_placed_move_status* out_status) {
  clear_error();
  return guarded("unknown exception while moving placed reference", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key || position == nullptr || rotation == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid reference, position and rotation are required");
    }
    try {
      const fo4::forms::PositionRotation transform{position[0], position[1], position[2],
                                                   rotation[0], rotation[1], rotation[2]};
      const auto status = fo4::forms::move_placed_reference(plugin->value, *key, transform);
      if (status == fo4::forms::PlacedReferenceMoveStatus::reference_missing) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      }
      if (status == fo4::forms::PlacedReferenceMoveStatus::target_cell_missing) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no exterior CELL at the new position; create it first");
      }
      if (out_status != nullptr) {
        *out_status = status == fo4::forms::PlacedReferenceMoveStatus::moved ? RADICLE_PLACED_MOVE_MOVED
                                                                             : RADICLE_PLACED_MOVE_RELOCATED;
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_set_placed_scale(radicle_plugin* plugin,
                                               radicle_form_key reference,
                                               int32_t has_scale,
                                               float scale) {
  clear_error();
  return guarded("unknown exception while setting scale", [&] {
    const auto key = to_form_key(reference);
    if (plugin == nullptr || !key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid reference are required");
    try {
      const auto value = has_scale != 0 ? std::optional<float>{scale} : std::nullopt;
      if (!fo4::forms::set_placed_reference_scale(plugin->value, *key, value)) {
        return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::PlacedReferenceError& error) {
      return placed_error(error);
    }
  });
}

radicle_status radicle_plugin_sort_masters(radicle_plugin* plugin, const radicle_env* env) {
  clear_error();
  if (plugin == nullptr || env == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and env are required");
  return guarded("unknown exception while sorting masters", [&] {
    std::vector<fo4::PluginKey> order;
    for (const auto& info : env->value.plugins()) order.push_back(info.key);
    try {
      plugin->value = fo4::sort_plugin_masters(plugin->value, order);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_object_template_rows(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    const radicle_object_template_rows** out_rows, radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !out_rows || !out_status) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, valid record and outputs are required");
  }
  *out_rows = nullptr;
  return guarded("unknown exception while reading object template", [&] {
    auto holder = std::make_unique<ObjectTemplateRowsHolder>();
    const auto result = fo4::forms::read_object_template_rows(plugin->value, *key, combination, holder->rows);
    *out_status = static_cast<radicle_object_template_write>(result);
    if (result != fo4::forms::ObjectTemplateWrite::updated) return RADICLE_STATUS_OK;
    for (const auto& keyword : holder->rows.keywords) holder->keywords.push_back(template_key(keyword));
    for (const auto& include : holder->rows.includes) {
      holder->includes.push_back({template_key(include.mod), include.attach_point, include.optional,
                                  include.dont_use_all});
    }
    for (const auto& property : holder->rows.properties) {
      holder->properties.push_back({property.value_type, property.function_type, property.property,
          property.value1, property.value2, property.step_bits,
          static_cast<uint8_t>(property.form_value.has_value()),
          property.form_value ? template_key(*property.form_value) : radicle_form_key{nullptr, 0}});
    }
    holder->view = {holder->keywords.data(), holder->keywords.size(), holder->includes.data(),
                    holder->includes.size(), holder->properties.data(), holder->properties.size(), holder.get(),
                    holder->rows.min_level_for_ranks, holder->rows.alt_levels_per_tier};
    *out_rows = &holder.release()->view;
    return RADICLE_STATUS_OK;
  });
}

void radicle_object_template_rows_free(const radicle_object_template_rows* rows) {
  if (rows) delete static_cast<ObjectTemplateRowsHolder*>(rows->internal);
}

radicle_status radicle_plugin_set_object_template_rank_tier(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    uint8_t min_level_for_ranks, uint8_t alt_levels_per_tier,
    radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !out_status) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, record and output are required");
  return guarded("unknown exception while editing object template rank and tier", [&] {
    *out_status = static_cast<radicle_object_template_write>(fo4::forms::set_object_template_rank_tier(
        plugin->value, *key, combination, min_level_for_ranks, alt_levels_per_tier));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_object_template_include_options(
    radicle_plugin* plugin, radicle_form_key record, size_t combination, size_t index,
    uint8_t attach_point, uint8_t optional, uint8_t dont_use_all,
    radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !out_status) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, record and output are required");
  return guarded("unknown exception while editing object template", [&] {
    *out_status = static_cast<radicle_object_template_write>(fo4::forms::set_object_template_include_options(
        plugin->value, *key, combination, index, attach_point, optional, dont_use_all));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_object_template_property_value(
    radicle_plugin* plugin, radicle_form_key record, size_t combination, size_t index,
    uint8_t part, uint32_t value, radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !out_status) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, record and output are required");
  return guarded("unknown exception while editing object template", [&] {
    *out_status = static_cast<radicle_object_template_write>(fo4::forms::set_object_template_property_value(
        plugin->value, *key, combination, index, part, value));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_object_template_property(
    radicle_plugin* plugin, radicle_form_key record, size_t combination, size_t index,
    const radicle_object_template_property* property, radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !property || !out_status) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, record, property and output are required");
  }
  return guarded("unknown exception while editing object template property", [&] {
    fo4::forms::ObjectTemplateProperty input{};
    if (!template_property_input(*property, input)) {
      *out_status = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE;
      return RADICLE_STATUS_OK;
    }
    *out_status = static_cast<radicle_object_template_write>(fo4::forms::set_object_template_property(
        plugin->value, *key, combination, index, input));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_insert_object_template_row(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    radicle_object_template_link_kind kind, size_t index,
    const radicle_object_template_insert_row* row, radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !row || !out_status || kind < RADICLE_OBJECT_TEMPLATE_KEYWORD ||
      kind > RADICLE_OBJECT_TEMPLATE_PROPERTY ||
      (kind == RADICLE_OBJECT_TEMPLATE_PROPERTY && row->property.has_form_value > 1)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, record, row kind, row and output are required");
  }
  return guarded("unknown exception while inserting object template row", [&] {
    fo4::forms::ObjectTemplateInsertRow input{};
    if (kind != RADICLE_OBJECT_TEMPLATE_PROPERTY) {
      const auto link = template_input_key(row->link);
      if (!link) { *out_status = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE; return RADICLE_STATUS_OK; }
      input.link = *link;
    }
    input.attach_point = row->attach_point;
    input.optional = row->optional;
    input.dont_use_all = row->dont_use_all;
    if (kind == RADICLE_OBJECT_TEMPLATE_PROPERTY && !template_property_input(row->property, input.property)) {
      *out_status = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE;
      return RADICLE_STATUS_OK;
    }
    *out_status = static_cast<radicle_object_template_write>(fo4::forms::insert_object_template_row(
        plugin->value, *key, combination, static_cast<fo4::forms::ObjectTemplateRowKind>(kind), index, input));
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_edit_object_template_row(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    radicle_object_template_link_kind kind, radicle_object_template_row_operation operation,
    size_t source, size_t destination, radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  if (!plugin || !key || !out_status || kind < RADICLE_OBJECT_TEMPLATE_KEYWORD ||
      kind > RADICLE_OBJECT_TEMPLATE_PROPERTY || operation < RADICLE_OBJECT_TEMPLATE_CLONE ||
      operation > RADICLE_OBJECT_TEMPLATE_MOVE) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, record, row kind, operation and output are required");
  }
  return guarded("unknown exception while editing object template rows", [&] {
    const auto row_kind = static_cast<fo4::forms::ObjectTemplateRowKind>(kind);
    fo4::forms::ObjectTemplateWrite result;
    switch (operation) {
      case RADICLE_OBJECT_TEMPLATE_CLONE:
        result = fo4::forms::clone_object_template_row(plugin->value, *key, combination, row_kind, source, destination);
        break;
      case RADICLE_OBJECT_TEMPLATE_REMOVE:
        result = fo4::forms::remove_object_template_row(plugin->value, *key, combination, row_kind, source);
        break;
      case RADICLE_OBJECT_TEMPLATE_MOVE:
        result = fo4::forms::move_object_template_row(plugin->value, *key, combination, row_kind, source, destination);
        break;
      default:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown object template row operation");
    }
    *out_status = static_cast<radicle_object_template_write>(result);
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_object_template_link(
    radicle_plugin* plugin, radicle_form_key record, size_t combination,
    radicle_object_template_link_kind kind, size_t index, radicle_form_key target,
    radicle_object_template_write* out_status) {
  clear_error();
  const auto key = to_form_key(record);
  const auto value = target.plugin == nullptr && target.local_id == 0
      ? std::optional<fo4::FormKey>{fo4::FormKey{}} : to_form_key(target);
  if (!plugin || !key || !value || !out_status || kind < RADICLE_OBJECT_TEMPLATE_KEYWORD ||
      kind > RADICLE_OBJECT_TEMPLATE_PROPERTY) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, valid keys, link kind and output are required");
  }
  return guarded("unknown exception while editing object template", [&] {
    fo4::forms::ObjectTemplateWrite result;
    switch (kind) {
      case RADICLE_OBJECT_TEMPLATE_KEYWORD:
        result = fo4::forms::set_object_template_keyword(plugin->value, *key, combination, index, *value);
        break;
      case RADICLE_OBJECT_TEMPLATE_INCLUDE:
        result = fo4::forms::set_object_template_include(plugin->value, *key, combination, index, *value);
        break;
      case RADICLE_OBJECT_TEMPLATE_PROPERTY:
        result = fo4::forms::set_object_template_property_form(plugin->value, *key, combination, index, *value);
        break;
      default:
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown object template link kind");
    }
    *out_status = static_cast<radicle_object_template_write>(result);
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
