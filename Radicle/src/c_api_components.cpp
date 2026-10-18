#include "c_api_forms_internal.hpp"
#include "fo4/c_api_components.h"
#include "fo4/forms/components.hpp"
#include "fo4/plugin/reader.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;

struct ComponentsHolder {
  radicle_components view{};
  std::vector<radicle_component_entry> entries;
};

}  // namespace

extern "C" {

radicle_status radicle_record_supports_components(const char* signature, int32_t* out_supported) {
  clear_error();
  if (out_supported != nullptr) *out_supported = 0;
  if (signature == nullptr || out_supported == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature and out_supported are required");
  }
  const auto parsed = to_signature(signature);
  if (!parsed) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "signature must be four characters");
  *out_supported = fo4::forms::record_supports_components(*parsed) ? 1 : 0;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_components_decode(const radicle_env* env,
                                         const radicle_record* record,
                                         radicle_components** out_components) {
  clear_error();
  if (out_components != nullptr) *out_components = nullptr;
  if (const auto status = require_env_record(env, record, out_components); status != RADICLE_STATUS_OK) return status;
  return guarded("unknown exception while decoding components", [&] {
    try {
      const auto& ref = *to_ref(record);
      const auto data = fo4::forms::decode_components(env->value.plugin(ref.plugin),
                                                      ref.record->form_key);
      auto holder = std::make_unique<ComponentsHolder>();
      holder->entries.reserve(data.size());
      for (const auto& entry : data) {
        holder->entries.push_back(radicle_component_entry{to_c_key(*env, entry.component), entry.count});
      }
      holder->view.entries = holder->entries.empty() ? nullptr : holder->entries.data();
      holder->view.entry_count = holder->entries.size();
      holder->view.internal = holder.get();
      *out_components = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ComponentError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_components_free(radicle_components* components) {
  clear_error();
  if (components == nullptr) return;
  delete static_cast<ComponentsHolder*>(components->internal);
}

radicle_status radicle_plugin_set_record_components(radicle_plugin* plugin,
                                                    radicle_form_key key,
                                                    const radicle_component_entry* entries,
                                                    size_t count) {
  clear_error();
  if (plugin == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  if (count != 0 && entries == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "entries are required when count is not zero");
  }
  const auto form_key = to_form_key(key);
  if (!form_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record key is required");
  if (fo4::find_record(plugin->value, *form_key) == nullptr) {
    return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
  }
  return guarded("unknown exception while setting record components", [&] {
    std::vector<fo4::forms::ComponentEntry> converted;
    converted.reserve(count);
    for (size_t index = 0; index < count; ++index) {
      const auto component = to_form_key(entries[index].component);
      if (!component) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "every component entry requires a record key");
      }
      converted.push_back(fo4::forms::ComponentEntry{*component, entries[index].count});
    }
    try {
      fo4::forms::set_record_components(plugin->value, *form_key, converted);
      return RADICLE_STATUS_OK;
    } catch (const fo4::forms::ComponentError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // extern "C"
