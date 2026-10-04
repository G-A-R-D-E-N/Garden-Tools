#include "fo4/c_api_quest.h"

#include "c_api_internal.hpp"
#include "fo4/quest/runtime.hpp"

#include <new>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {

radicle_status radicle_quest_runtime_rows(const radicle_plugin* plugin,
                                          const char* form_key,
                                          char*** out_rows,
                                          size_t* out_count) {
  radicle_c_detail::clear_error();
  if (!plugin || !form_key || !out_rows || !out_count) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT,
                                  "plugin, form_key, out_rows and out_count are required");
  }

  *out_rows = nullptr;
  *out_count = 0;

  fo4::FormKey key;
  try {
    key = fo4::FormKey::parse(form_key);
  } catch (const std::bad_alloc& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (...) {
    return radicle_c_detail::fail(RADICLE_STATUS_INVALID_ARGUMENT, "form_key is not a form key");
  }

  try {
    const auto data = fo4::quest::quest_runtime_data(plugin->value, key);

    std::vector<std::string> rows;
    rows.push_back("quest\t" + key.to_string() + "\t" + (data.start_game_enabled ? "1" : "0"));
    for (const auto& script : data.scripts) {
      rows.push_back("script\t" + script.name);
      for (const auto& property : script.properties) {
        rows.push_back("script-property\t" + script.name + "\t" + property.name + "\t" +
                       std::to_string(property.type));
      }
    }
    if (!data.fragment_script.empty()) {
      rows.push_back("fragment-script\t" + data.fragment_script);
    }
    for (const auto& property : data.fragment_properties) {
      rows.push_back("fragment-property\t" + property.name + "\t" + std::to_string(property.type));
    }
    for (const auto& fragment : data.fragments) {
      rows.push_back("fragment\t" + std::to_string(fragment.stage) + "\t" +
                     std::to_string(fragment.stage_index) + "\t" + fragment.script + "\t" +
                     fragment.function);
    }
    for (const auto& alias : data.alias_scripts) {
      rows.push_back("alias\t" + std::to_string(alias.alias_id));
      for (const auto& script : alias.scripts) {
        rows.push_back("alias-script\t" + std::to_string(alias.alias_id) + "\t" + script.name);
        for (const auto& property : script.properties) {
          rows.push_back("alias-script-property\t" + std::to_string(alias.alias_id) + "\t" +
                         script.name + "\t" + property.name + "\t" +
                         std::to_string(property.type));
        }
      }
    }
    for (const auto& reason : data.unsupported) {
      rows.push_back("unsupported\t" + reason);
    }

    return radicle_c_detail::copy_strings(rows, out_rows, out_count);
  } catch (const std::invalid_argument& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::bad_alloc& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return radicle_c_detail::fail(RADICLE_STATUS_INTERNAL_ERROR,
                                  "unknown exception while decoding quest runtime data");
  }
}

}  // extern "C"
