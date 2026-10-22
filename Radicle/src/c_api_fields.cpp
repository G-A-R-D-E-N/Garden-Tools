#include "c_api_env_internal.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/text/encoding.hpp"

#include <optional>
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

}

extern "C" {

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

}
