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
    const auto text = fo4::text::utf8_to_windows1252(value);
    if (!text) throw fo4::schema::ScalarError("VMAD text must be UTF-8 representable in Windows-1252");
    return *text;
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

}
