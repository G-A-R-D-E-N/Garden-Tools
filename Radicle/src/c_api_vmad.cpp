#include "c_api_env_internal.hpp"
#include "fo4/c_api_vmad.h"
#include "fo4/plugin/vmad_writer.hpp"

#include <limits>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

template <class Value>
radicle_status edit(radicle_plugin* plugin, radicle_form_key key, size_t script,
                     size_t property, size_t element, Value value) {
  clear_error();
  const auto record_key = to_form_key(key);
  if (plugin == nullptr || !record_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while editing VMAD", [&] {
    try {
      const auto status = fo4::set_vmad_property(plugin->value, *record_key, script,
                                                 property, value(), element);
      if (status == fo4::ScalarMutationStatus::record_missing ||
          status == fo4::ScalarMutationStatus::subrecord_missing) {
        return fail(RADICLE_STATUS_NOT_FOUND, "VMAD record, property or array element is absent");
      }
      return RADICLE_STATUS_OK;
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status fragment_status(fo4::VmadStructureMutationStatus status) {
  switch (status) {
    case fo4::VmadStructureMutationStatus::updated:
    case fo4::VmadStructureMutationStatus::unchanged:
      return RADICLE_STATUS_OK;
    case fo4::VmadStructureMutationStatus::record_missing:
    case fo4::VmadStructureMutationStatus::subrecord_missing:
    case fo4::VmadStructureMutationStatus::binding_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "VMAD record, VMAD or quest fragment binding is absent");
    case fo4::VmadStructureMutationStatus::not_authorable:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "only QUST records carry quest fragment bindings");
    default:
      return fail(RADICLE_STATUS_INTERNAL_ERROR, "unexpected quest fragment status");
  }
}

template <class Mutate>
radicle_status fragment_edit(radicle_plugin* plugin, radicle_form_key key, Mutate mutate) {
  clear_error();
  const auto record_key = to_form_key(key);
  if (plugin == nullptr || !record_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while editing quest VMAD fragment", [&] {
    try {
      return fragment_status(mutate(*record_key));
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status structure_status(fo4::VmadStructureMutationStatus status, const char* target) {
  const auto message = [&](const char* tail) { return std::string(target) + tail; };
  switch (status) {
    case fo4::VmadStructureMutationStatus::updated:
    case fo4::VmadStructureMutationStatus::unchanged:
      return RADICLE_STATUS_OK;
    case fo4::VmadStructureMutationStatus::already_present: {
      const auto text = message(" is already present and a second copy is refused");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
    case fo4::VmadStructureMutationStatus::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::VmadStructureMutationStatus::subrecord_missing:
      return fail(RADICLE_STATUS_NOT_FOUND,
                  "record has no VMAD subrecord; add one before editing its structure");
    case fo4::VmadStructureMutationStatus::binding_missing: {
      const auto text = message(" binding is absent from the VMAD");
      return fail(RADICLE_STATUS_NOT_FOUND, text.c_str());
    }
    case fo4::VmadStructureMutationStatus::index_out_of_range: {
      const auto text = message(" index is out of range");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
    case fo4::VmadStructureMutationStatus::not_authorable: {
      const auto text = message(" structure edits are not authorable on this record type");
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, text.c_str());
    }
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown VMAD structure status");
}

template <class Mutate>
radicle_status structure_edit(radicle_plugin* plugin, radicle_form_key key, const char* target,
                              Mutate mutate) {
  clear_error();
  const auto record_key = to_form_key(key);
  if (plugin == nullptr || !record_key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid record key are required");
  }
  return guarded("unknown exception while editing the VMAD structure", [&] {
    try {
      return structure_status(mutate(*record_key), target);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

void require_name(const char* value, const char* what) {
  if (value == nullptr || value[0] == '\0') {
    throw fo4::schema::ScalarError(std::string(what) + " is required");
  }
  if (!fo4::text::is_valid_utf8(value)) {
    throw fo4::schema::ScalarError(std::string(what) + " must be valid UTF-8");
  }
}

std::int16_t alias_from_int(int value) {
  if (value < std::numeric_limits<std::int16_t>::min() || value > std::numeric_limits<std::int16_t>::max()) {
    throw fo4::schema::ScalarError("VMAD alias does not fit in 16 bits");
  }
  return static_cast<std::int16_t>(value);
}

fo4::VmadProperty default_property(const char* name, std::uint8_t type, std::uint8_t flags) {
  require_name(name, "VMAD property name");
  fo4::VmadProperty property;
  property.name = name;
  property.type = type;
  property.flags = flags;
  switch (type) {
    case 1:
      property.object = fo4::RawFormId{0u};
      property.alias = alias_from_int(-1);
      return property;
    case 2:
      property.text = std::string{};
      return property;
    case 3:
      property.integer = std::int32_t{0};
      return property;
    case 4:
      property.real = 0.0f;
      return property;
    case 5:
      property.boolean = false;
      return property;
    case 7:
      property.struct_elements = std::vector<fo4::VmadStruct>{fo4::VmadStruct{}};
      return property;
    case 11:
      property.element_count = 1u;
      property.object_elements =
          std::vector<fo4::VmadObjectElement>{{fo4::RawFormId{0u}, alias_from_int(-1)}};
      return property;
    case 12:
      property.element_count = 1u;
      property.text_elements = std::vector<std::string>{std::string{}};
      return property;
    case 13:
      property.element_count = 1u;
      property.integer_elements = std::vector<std::int32_t>{std::int32_t{0}};
      return property;
    case 14:
      property.element_count = 1u;
      property.real_elements = std::vector<float>{0.0f};
      return property;
    case 15:
      property.element_count = 1u;
      property.boolean_elements = std::vector<bool>{false};
      return property;
    case 17:
      property.element_count = 1u;
      property.struct_elements = std::vector<fo4::VmadStruct>{fo4::VmadStruct{}};
      return property;
    default:
      throw fo4::schema::ScalarError(
          "VMAD property type is not authorable; 0 and 16 have no fixed value layout");
  }
}

}

extern "C" {

radicle_status radicle_plugin_set_vmad_object(radicle_plugin* plugin, radicle_form_key key,
                                              size_t script, size_t property, size_t element,
                                              radicle_form_key target, int16_t alias) {
  return edit(plugin, key, script, property, element, [&]() -> fo4::VmadValue {
    if (target.plugin == nullptr && target.local_id == 0) return fo4::VmadObjectValue{fo4::FormKey{}, alias};
    const auto form_key = to_form_key(target);
    if (!form_key) throw fo4::schema::ScalarError("invalid VMAD object target");
    return fo4::VmadObjectValue{*form_key, alias};
  });
}

radicle_status radicle_plugin_set_vmad_string(radicle_plugin* plugin, radicle_form_key key,
                                              size_t script, size_t property, size_t element,
                                              const char* value) {
  return edit(plugin, key, script, property, element, [&]() -> fo4::VmadValue {
    if (value == nullptr) throw fo4::schema::ScalarError("VMAD text is required");
    if (!fo4::text::is_valid_utf8(value)) throw fo4::schema::ScalarError("VMAD text must be valid UTF-8");
    return std::string(value);
  });
}

radicle_status radicle_plugin_set_vmad_int(radicle_plugin* plugin, radicle_form_key key,
                                           size_t script, size_t property, size_t element, int32_t value) {
  return edit(plugin, key, script, property, element, [&]() -> fo4::VmadValue { return value; });
}

radicle_status radicle_plugin_set_vmad_float(radicle_plugin* plugin, radicle_form_key key,
                                             size_t script, size_t property, size_t element, float value) {
  return edit(plugin, key, script, property, element, [&]() -> fo4::VmadValue { return value; });
}

radicle_status radicle_plugin_set_vmad_bool(radicle_plugin* plugin, radicle_form_key key,
                                            size_t script, size_t property, size_t element, int32_t value) {
  return edit(plugin, key, script, property, element, [&]() -> fo4::VmadValue {
    if (value != 0 && value != 1) throw fo4::schema::ScalarError("VMAD boolean must be 0 or 1");
    return value != 0;
  });
}

radicle_status radicle_plugin_set_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key,
                                                      uint16_t stage, int32_t stage_index,
                                                      const char* script, const char* function) {
  return fragment_edit(plugin, key, [&](const fo4::FormKey& record) {
    if (script == nullptr || function == nullptr) {
      throw fo4::schema::ScalarError("VMAD fragment script and function names are required");
    }
    if (!fo4::text::is_valid_utf8(script) || !fo4::text::is_valid_utf8(function)) {
      throw fo4::schema::ScalarError("VMAD fragment names must be valid UTF-8");
    }
    return fo4::set_quest_vmad_fragment(plugin->value, record, stage, stage_index, script, function);
  });
}

radicle_status radicle_plugin_remove_quest_vmad_fragment(radicle_plugin* plugin, radicle_form_key key,
                                                         uint16_t stage, int32_t stage_index) {
  return fragment_edit(plugin, key, [&](const fo4::FormKey& record) {
    return fo4::remove_quest_vmad_fragment(plugin->value, record, stage, stage_index);
  });
}

radicle_status radicle_plugin_add_vmad(radicle_plugin* plugin, radicle_form_key key) {
  return structure_edit(plugin, key, "VMAD", [&](const fo4::FormKey& record) {
    return fo4::add_vmad(plugin->value, record);
  });
}

radicle_status radicle_plugin_add_vmad_script(radicle_plugin* plugin, radicle_form_key key,
                                              size_t index, const char* script_name,
                                              uint8_t flags, size_t* out_script_index) {
  if (out_script_index != nullptr) *out_script_index = 0;
  return structure_edit(plugin, key, "VMAD script", [&](const fo4::FormKey& record) {
    require_name(script_name, "VMAD script name");
    const auto status = fo4::add_vmad_script(plugin->value, record, index, script_name, flags);
    if (status == fo4::VmadStructureMutationStatus::updated && out_script_index != nullptr) {
      *out_script_index = index;
    }
    return status;
  });
}

radicle_status radicle_plugin_remove_vmad_script(radicle_plugin* plugin, radicle_form_key key,
                                                 size_t script_index) {
  return structure_edit(plugin, key, "VMAD script", [&](const fo4::FormKey& record) {
    return fo4::remove_vmad_script(plugin->value, record, script_index);
  });
}

radicle_status radicle_plugin_add_vmad_property(radicle_plugin* plugin, radicle_form_key key,
                                                size_t script_index, size_t index,
                                                const char* name, uint8_t type, uint8_t flags,
                                                size_t* out_property_index) {
  if (out_property_index != nullptr) *out_property_index = 0;
  return structure_edit(plugin, key, "VMAD property", [&](const fo4::FormKey& record) {
    const auto property = default_property(name, type, flags);
    const auto status = fo4::add_vmad_property(plugin->value, record, script_index, index, property);
    if (status == fo4::VmadStructureMutationStatus::updated && out_property_index != nullptr) {
      *out_property_index = index;
    }
    return status;
  });
}

radicle_status radicle_plugin_remove_vmad_property(radicle_plugin* plugin, radicle_form_key key,
                                                   size_t script_index, size_t property_index) {
  return structure_edit(plugin, key, "VMAD property", [&](const fo4::FormKey& record) {
    return fo4::remove_vmad_property(plugin->value, record, script_index, property_index);
  });
}

radicle_status radicle_plugin_add_vmad_fragment(radicle_plugin* plugin, radicle_form_key key,
                                                uint32_t fragment_key, const char* script,
                                                const char* function) {
  return structure_edit(plugin, key, "VMAD fragment", [&](const fo4::FormKey& record) {
    require_name(script, "VMAD fragment script name");
    require_name(function, "VMAD fragment function name");
    return fo4::add_vmad_fragment(plugin->value, record, fragment_key, script, function);
  });
}

radicle_status radicle_plugin_remove_vmad_fragment(radicle_plugin* plugin, radicle_form_key key,
                                                   uint32_t fragment_key) {
  return structure_edit(plugin, key, "VMAD fragment", [&](const fo4::FormKey& record) {
    return fo4::remove_vmad_fragment(plugin->value, record, fragment_key);
  });
}

}
