#include "c_api_env_internal.hpp"
#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/forms/record_flags.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

radicle_status bad_argument(const std::exception& error) {
  return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
}

radicle_status map_scalar_status(fo4::ScalarMutationStatus status) {
  switch (status) {
    case fo4::ScalarMutationStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::ScalarMutationStatus::subrecord_missing:
      return fail(RADICLE_STATUS_NOT_FOUND,
                  "record has no such field occurrence, or the field is outside its supported payload layout");
    case fo4::ScalarMutationStatus::next_alias_id_too_low:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "QUST Next Alias ID must stay above every existing alias ID");
    default:
      return RADICLE_STATUS_OK;
  }
}

radicle_status map_list_status(fo4::schema::FormLinkListMutationStatus status) {
  using Status = fo4::schema::FormLinkListMutationStatus;
  switch (status) {
    case Status::updated:
    case Status::unchanged:
      return RADICLE_STATUS_OK;
    case Status::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case Status::container_index_out_of_range:
    case Status::index_out_of_range:
      return fail(RADICLE_STATUS_NOT_FOUND, "list index is out of range");
    case Status::record_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "list path does not apply to this record type");
    case Status::target_type_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "target record type is not allowed in this list");
    case Status::insertion_boundary_unavailable:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "this list cannot be inserted into here");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown list mutation status");
}

radicle_status map_group_status(fo4::SubrecordGroupMutationStatus status) {
  using Status = fo4::SubrecordGroupMutationStatus;
  switch (status) {
    case Status::updated:
    case Status::unchanged:
      return RADICLE_STATUS_OK;
    case Status::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case Status::index_out_of_range:
      return fail(RADICLE_STATUS_NOT_FOUND, "subrecord group index is out of range");
    case Status::subrecord_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "record has no such subrecord group");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown subrecord group mutation status");
}

template <typename Mutate>
radicle_status subrecord_group_edit(radicle_plugin* plugin, radicle_form_key key, const char* group_path, Mutate&& mutate,
                                    bool allow_scoped = true) {
  const auto form_key = to_form_key(key);
  if (plugin == nullptr || !form_key || group_path == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and group path are required");
  }
  fo4::assign_record_form_keys(plugin->value);
  try {
    fo4::ScopedGroupPath group;
    switch (fo4::resolve_group_path(plugin->value, *form_key, group_path, group)) {
      case fo4::FieldStatus::ok: break;
      case fo4::FieldStatus::record_missing: return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      case fo4::FieldStatus::index_out_of_range:
        return fail(RADICLE_STATUS_NOT_FOUND, "parent element index is out of range");
      default: return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown subrecord group path");
    }
    if (!allow_scoped && group.within) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a scoped group path is not supported by this call");
    }
    return map_group_status(mutate(plugin->value, *form_key, group.group,
                                   group.within ? &*group.within : nullptr));
  } catch (const fo4::schema::SubrecordGroupError& error) {
    return bad_argument(error);
  }
}

std::optional<fo4::schema::FormLinkListDescriptor> list_descriptor(const char* path) {
  const std::string_view text{path};
  if (text.size() < 6 || text[4] != '\\') return std::nullopt;
  return fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(text.substr(0, 4)), text);
}

fo4::FormKey optional_target(const radicle_form_key* target) {
  return target == nullptr ? fo4::FormKey{} : *to_form_key(*target);
}

bool target_valid(const radicle_form_key* target) {
  return target == nullptr || to_form_key(*target).has_value();
}

radicle_status map_record_flag_status(fo4::RecordFlagWriteStatus status, int32_t* out_changed) {
  switch (status) {
    case fo4::RecordFlagWriteStatus::changed:
      if (out_changed != nullptr) *out_changed = 1;
      return RADICLE_STATUS_OK;
    case fo4::RecordFlagWriteStatus::unchanged:
      return RADICLE_STATUS_OK;
    case fo4::RecordFlagWriteStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::RecordFlagWriteStatus::unknown_flag:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown record flag path");
    case fo4::RecordFlagWriteStatus::signature_mismatch:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "the record flag row belongs to another record type");
    case fo4::RecordFlagWriteStatus::header_record:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the TES4 header carries no schema record flag");
    case fo4::RecordFlagWriteStatus::deleted_bit:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "the Deleted bit is not a schema record flag; mark the override deleted instead");
    case fo4::RecordFlagWriteStatus::target_cell_missing:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "the reference has no cell to move to for its new persistence");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown record flag status");
}

}

extern "C" {

radicle_status radicle_plugin_set_record_flag(radicle_plugin* plugin,
                                              radicle_form_key key,
                                              const char* path,
                                              int32_t value,
                                              int32_t* out_changed) {
  clear_error();
  if (out_changed != nullptr) *out_changed = 0;
  return guarded("unknown exception while setting a record flag", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and path are required");
    }
    if (value != 0 && value != 1) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a record flag value must be 0 or 1");
    }
    try {
      return map_record_flag_status(
          fo4::write_record_flag(plugin->value, *form_key, path, value != 0), out_changed);
    } catch (const std::invalid_argument& error) {
      // Constraint: a record whose payload cannot be decoded is refused by the setter before any bit moves.
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

radicle_status radicle_plugin_get_record_flag(const radicle_plugin* plugin,
                                              radicle_form_key key,
                                              const char* path,
                                              int32_t* out_value) {
  clear_error();
  if (out_value != nullptr) *out_value = 0;
  return guarded("unknown exception while reading a record flag", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || out_value == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "plugin, a valid record key, path and out_value are required");
    }
    const auto* descriptor = fo4::schema::record_flag_descriptor(path);
    if (descriptor == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown record flag path");
    const auto* record = fo4::find_record(plugin->value, *form_key);
    if (record == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    if (record->header.signature != descriptor->signature) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "the record flag row belongs to another record type");
    }
    *out_value = fo4::schema::record_flag_set(*record, *descriptor) ? 1 : 0;
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_plugin_set_record_scalar_at(radicle_plugin* plugin,
                                                   radicle_form_key key,
                                                   const char* path,
                                                   size_t index,
                                                   const char* value) {
  clear_error();
  return guarded("unknown exception while setting scalar field", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || value == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and value are required");
    }
    const auto field = fo4::schema::scalar_field_descriptor(path);
    if (!field) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown scalar field path");
    try {
      return map_scalar_status(fo4::set_record_scalar(plugin->value, *form_key, *field, index, value));
    } catch (const fo4::schema::ScalarError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_set_record_string(radicle_plugin* plugin,
                                                radicle_form_key key,
                                                const char* path,
                                                size_t index,
                                                const char* utf8) {
  clear_error();
  return guarded("unknown exception while setting string field", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || utf8 == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and text are required");
    }
    const auto field = fo4::schema::scalar_field_descriptor(path);
    if (!field) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown string field path");
    const auto stored = fo4::text::utf8_to_windows1252(utf8);
    if (!stored) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "text is not valid UTF-8 representable in Windows-1252");
    try {
      return map_scalar_status(fo4::set_record_string(plugin->value, *form_key, *field, index, *stored));
    } catch (const fo4::schema::ScalarError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_set_record_formlink(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  const char* path,
                                                  size_t index,
                                                  const radicle_form_key* target) {
  clear_error();
  return guarded("unknown exception while setting FormLink field", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || !target_valid(target)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and a valid target or NULL are required");
    }
    const auto field = fo4::schema::formlink_field_descriptor(path);
    if (!field) {
      const auto list = fo4::schema::packed_formlink_list_path(path);
      if (!list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown FormLink field path");
      try {
        return map_list_status(fo4::schema::set_record_formlink_list_element(
            plugin->value, *form_key, *list, index, optional_target(target)));
      } catch (const fo4::schema::FormLinkListError& error) {
        return bad_argument(error);
      } catch (const fo4::FormIdError& error) {
        return bad_argument(error);
      }
    }
    try {
      switch (fo4::set_record_formlink_field(plugin->value, *form_key, *field, index, optional_target(target))) {
        case fo4::FormLinkFieldMutationStatus::record_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
        case fo4::FormLinkFieldMutationStatus::record_mismatch:
          return fail(RADICLE_STATUS_INVALID_ARGUMENT, "field path does not apply to this record type");
        case fo4::FormLinkFieldMutationStatus::subrecord_missing:
          return fail(RADICLE_STATUS_NOT_FOUND, "record has no such field occurrence, or the field is outside its supported payload layout");
        default:
          return RADICLE_STATUS_OK;
      }
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return bad_argument(error);
    } catch (const fo4::FormIdError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_record_field_count(const radicle_plugin* plugin,
                                                 radicle_form_key key,
                                                 const char* path,
                                                 size_t* out_count) {
  clear_error();
  return guarded("unknown exception while counting field occurrences", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || out_count == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and out_count are required");
    }
    *out_count = 0;
    try {
      const auto count = fo4::record_field_count(plugin->value, *form_key, path);
      if (!count) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      *out_count = *count;
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::ScalarError& error) {
      return bad_argument(error);
    } catch (const fo4::schema::FormLinkFieldError& error) {
      return bad_argument(error);
    } catch (const fo4::schema::FormLinkListError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_record_scalar_text(const radicle_plugin* plugin,
                                                 radicle_form_key key,
                                                 const char* path,
                                                 size_t index,
                                                 char** out_text) {
  clear_error();
  return guarded("unknown exception while reading scalar field text", [&] {
    if (out_text != nullptr) *out_text = nullptr;
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || out_text == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "plugin, a valid record key, path and out_text are required");
    }
    const auto field = fo4::schema::scalar_field_descriptor(path);
    if (!field) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown scalar field path");
    const auto* record = fo4::find_record(plugin->value, *form_key);
    if (record == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    if (record->header.signature != field->record) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "field path does not apply to this record type");
    }
    try {
      const auto resolved = fo4::schema::resolve_field_slots(
          *record, fo4::schema::scalar_field_shape(field->scalar), field->scalar.address);
      if (index >= resolved.slots.size() || !resolved.slots[index].valid) {
        return fail(RADICLE_STATUS_NOT_FOUND,
                    "record has no such field occurrence, or the field is outside its supported payload layout");
      }
      const auto slot = resolved.slots[index];
      const auto text = fo4::schema::decode_scalar(
          fo4::schema::scalar_slot_bytes(record->subrecords[slot.subrecord].data, slot, field->scalar),
          field->scalar);
      auto* copy = static_cast<char*>(std::malloc(text.size() + 1));
      if (copy == nullptr) return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate scalar text");
      std::memcpy(copy, text.data(), text.size());
      copy[text.size()] = '\0';
      *out_text = copy;
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_free_text(char* text) { std::free(text); }

radicle_status radicle_plugin_formlink_list_size(const radicle_plugin* plugin,
                                                 radicle_form_key key,
                                                 const char* path,
                                                 size_t* out_count) {
  clear_error();
  return guarded("unknown exception while sizing FormLink list", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || out_count == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and out_count are required");
    }
    *out_count = 0;
    const auto list = list_descriptor(path);
    if (!list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown FormLink list path");
    try {
      const auto count = fo4::schema::record_formlink_list_size(plugin->value, *form_key, *list);
      if (!count) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
      *out_count = *count;
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::FormLinkListError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_formlink_list_insert(radicle_plugin* plugin,
                                                   radicle_form_key key,
                                                   const char* path,
                                                   size_t at,
                                                   const radicle_form_key* target) {
  clear_error();
  return guarded("unknown exception while inserting FormLink list element", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || !target_valid(target)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and a valid target or NULL are required");
    }
    const auto list = list_descriptor(path);
    if (!list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown FormLink list path");
    try {
      if (target == nullptr) {
        return map_list_status(fo4::schema::insert_record_formlink_list_null_element(
            plugin->value, *form_key, *list, at));
      }
      return map_list_status(fo4::schema::insert_record_formlink_list_element(
          plugin->value, *form_key, *list, at, optional_target(target)));
    } catch (const fo4::schema::FormLinkListError& error) {
      return bad_argument(error);
    } catch (const fo4::FormIdError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_formlink_list_remove(radicle_plugin* plugin,
                                                   radicle_form_key key,
                                                   const char* path,
                                                   size_t at) {
  clear_error();
  return guarded("unknown exception while removing FormLink list element", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and path are required");
    }
    const auto list = list_descriptor(path);
    if (!list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown FormLink list path");
    try {
      return map_list_status(fo4::schema::remove_record_formlink_list_element(
          plugin->value, *form_key, *list, at));
    } catch (const fo4::schema::FormLinkListError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_formlink_list_set(radicle_plugin* plugin,
                                                radicle_form_key key,
                                                const char* path,
                                                size_t at,
                                                const radicle_form_key* target) {
  clear_error();
  return guarded("unknown exception while setting FormLink list element", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr || !target_valid(target)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key, path and a valid target or NULL are required");
    }
    const auto list = list_descriptor(path);
    if (!list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown FormLink list path");
    if (target == nullptr && !list->nullable) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "this list does not allow NULL elements");
    }
    try {
      return map_list_status(fo4::schema::set_record_formlink_list_element(
          plugin->value, *form_key, *list, at, optional_target(target)));
    } catch (const fo4::schema::FormLinkListError& error) {
      return bad_argument(error);
    } catch (const fo4::FormIdError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_formlink_list_clear(radicle_plugin* plugin,
                                                  radicle_form_key key,
                                                  const char* path) {
  clear_error();
  return guarded("unknown exception while clearing FormLink list", [&] {
    const auto form_key = to_form_key(key);
    if (plugin == nullptr || !form_key || path == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, a valid record key and path are required");
    }
    const auto list = list_descriptor(path);
    if (!list) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "unknown FormLink list path");
    try {
      return map_list_status(fo4::schema::clear_record_formlink_list(plugin->value, *form_key, *list));
    } catch (const fo4::schema::FormLinkListError& error) {
      return bad_argument(error);
    }
  });
}

radicle_status radicle_plugin_subrecord_group_member_set(radicle_plugin* plugin,
                                                         radicle_form_key key,
                                                         const char* group_path,
                                                         size_t index,
                                                         const char* member,
                                                         int32_t present) {
  clear_error();
  return guarded("unknown exception while setting a subrecord group member", [&] {
    const std::string_view member_text{member == nullptr ? std::string_view{} : std::string_view{member}};
    if (member_text.size() != 4) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a four-byte member signature is required");
    }
    return subrecord_group_edit(plugin, key, group_path,
                                [&](fo4::Plugin& target, const fo4::FormKey& form_key,
                                    const fo4::schema::SubrecordGroupDescriptor& group,
                                    const fo4::SubrecordGroupSpan* within) {
                                  return fo4::set_record_subrecord_group_member(
                                      target, form_key, group, index,
                                      fo4::Signature::from_string(member_text), present != 0, within);
                                });
  });
}

radicle_status radicle_plugin_subrecord_group_insert(radicle_plugin* plugin,
                                                     radicle_form_key key,
                                                     const char* group_path,
                                                     size_t index) {
  clear_error();
  return guarded("unknown exception while inserting a subrecord group member", [&] {
    return subrecord_group_edit(plugin, key, group_path,
                                [&](fo4::Plugin& target, const fo4::FormKey& form_key,
                                    const fo4::schema::SubrecordGroupDescriptor& group,
                                    const fo4::SubrecordGroupSpan* within) {
                                  return fo4::insert_record_subrecord_group(target, form_key, group, index, within);
                                });
  });
}

radicle_status radicle_plugin_subrecord_group_remove(radicle_plugin* plugin,
                                                     radicle_form_key key,
                                                     const char* group_path,
                                                     size_t index) {
  clear_error();
  return guarded("unknown exception while removing a subrecord group member", [&] {
    return subrecord_group_edit(plugin, key, group_path,
                                [&](fo4::Plugin& target, const fo4::FormKey& form_key,
                                    const fo4::schema::SubrecordGroupDescriptor& group,
                                    const fo4::SubrecordGroupSpan* within) {
                                  return fo4::remove_record_subrecord_group(target, form_key, group, index, within);
                                });
  });
}

radicle_status radicle_plugin_subrecord_group_run_insert(radicle_plugin* plugin,
                                                         radicle_form_key key,
                                                         const char* group_path,
                                                         size_t run,
                                                         size_t run_index) {
  clear_error();
  return guarded("unknown exception while inserting a subrecord group run", [&] {
    return subrecord_group_edit(plugin, key, group_path,
                                [&](fo4::Plugin& target, const fo4::FormKey& form_key,
                                    const fo4::schema::SubrecordGroupDescriptor& group,
                                    const fo4::SubrecordGroupSpan*) {
                                  return fo4::insert_record_subrecord_group_run(target, form_key, group, run,
                                                                                run_index);
                                }, false);
  });
}

radicle_status radicle_plugin_subrecord_group_run_remove(radicle_plugin* plugin,
                                                         radicle_form_key key,
                                                         const char* group_path,
                                                         size_t run,
                                                         size_t run_index) {
  clear_error();
  return guarded("unknown exception while removing a subrecord group run", [&] {
    return subrecord_group_edit(plugin, key, group_path,
                                [&](fo4::Plugin& target, const fo4::FormKey& form_key,
                                    const fo4::schema::SubrecordGroupDescriptor& group,
                                    const fo4::SubrecordGroupSpan*) {
                                  return fo4::remove_record_subrecord_group_run(target, form_key, group, run,
                                                                                run_index);
                                }, false);
  });
}

}
