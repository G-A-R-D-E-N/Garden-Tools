#include "c_api_env_internal.hpp"
#include "fo4/c_api_vmad.h"
#include "fo4/plugin/vmad_writer.hpp"

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

}
