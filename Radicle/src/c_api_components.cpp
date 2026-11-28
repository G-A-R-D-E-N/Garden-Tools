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

}  // namespace

extern "C" {

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
