#include "fo4/c_api_quest.h"

#include "c_api_internal.hpp"
#include "fo4/quest/runtime.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace radicle_c_detail;

bool parse_form_key(const char* text, fo4::FormKey& key, radicle_status& status) {
  try {
    key = fo4::FormKey::parse(text);
    return true;
  } catch (const std::bad_alloc& error) {
    status = fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    status = fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (...) {
    status = fail(RADICLE_STATUS_INVALID_ARGUMENT, "form_key is not a form key");
  }
  return false;
}

const char* copy_text(const char* text, size_t size) {
  if (!text) return nullptr;
  auto* copy = static_cast<char*>(std::malloc(size + 1));
  if (!copy) return nullptr;
  std::memcpy(copy, text, size);
  copy[size] = '\0';
  return copy;
}

const char* copy_c_string(const char* text) {
  if (!text) return nullptr;
  return copy_text(text, std::strlen(text));
}

void free_text(radicle_quest_property& property) {
  std::free(const_cast<char*>(property.script));
  std::free(const_cast<char*>(property.name));
  std::free(const_cast<char*>(property.text));
  property.script = nullptr;
  property.name = nullptr;
  property.text = nullptr;
  property.text_size = 0;
}

void append_properties(std::vector<radicle_quest_property>& out,
                       const std::vector<fo4::VmadProperty>& properties,
                       radicle_quest_property_owner owner,
                       std::int32_t alias,
                       const std::string& script) {
  for (const auto& property : properties) {
    radicle_quest_property value{};
    value.owner = owner;
    value.alias = alias;
    value.script = script.c_str();
    value.name = property.name.c_str();
    value.type = property.type;
    value.flags = property.flags;
    if (property.integer) {
      value.has_integer = 1;
      value.integer = *property.integer;
    }
    if (property.real) {
      value.has_real = 1;
      value.real = *property.real;
    }
    if (property.boolean) {
      value.has_boolean = 1;
      value.boolean = *property.boolean ? 1 : 0;
    }
    if (property.text) {
      value.text = property.text->c_str();
      value.text_size = property.text->size();
    }
    if (property.object) {
      value.has_object = 1;
      value.object_form_id = property.object->value();
    }
    if (property.alias) {
      value.has_property_alias = 1;
      value.property_alias = *property.alias;
    }
    if (property.element_count) {
      value.has_element_count = 1;
      value.element_count = *property.element_count;
    }
    out.push_back(value);
  }
}

radicle_status copy_properties(const std::vector<radicle_quest_property>& values,
                               radicle_quest_property** out_properties,
                               size_t* out_count) {
  *out_properties = nullptr;
  *out_count = 0;
  if (values.empty()) {
    return RADICLE_STATUS_OK;
  }

  auto* result = static_cast<radicle_quest_property*>(
      std::calloc(values.size(), sizeof(radicle_quest_property)));
  if (!result) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate quest property array");
  }

  for (size_t i = 0; i < values.size(); ++i) {
    result[i] = values[i];
    result[i].script = copy_c_string(values[i].script);
    result[i].name = copy_c_string(values[i].name);
    result[i].text = copy_text(values[i].text, values[i].text_size);
    const bool failed = (values[i].script && !result[i].script) ||
                        (values[i].name && !result[i].name) ||
                        (values[i].text && !result[i].text);
    if (failed) {
      free_text(result[i]);
      radicle_free_quest_properties(result, i);
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate quest property string");
    }
  }

  *out_properties = result;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

}

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
  radicle_status status = RADICLE_STATUS_OK;
  if (!parse_form_key(form_key, key, status)) {
    return status;
  }

  try {
    const auto data = fo4::quest::quest_runtime_data(plugin->value, key);

    std::vector<std::string> rows;
    rows.push_back("quest\t" + key.to_string() + "\t" + (data.start_game_enabled ? "1" : "0"));
    for (const auto& script : data.scripts) {
      rows.push_back("script\t" + script.name);
    }
    if (!data.fragment_script.empty()) {
      rows.push_back("fragment-script\t" + data.fragment_script);
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

radicle_status radicle_quest_runtime_properties(const radicle_plugin* plugin,
                                                const char* form_key,
                                                radicle_quest_property** out_properties,
                                                size_t* out_count) {
  radicle_c_detail::clear_error();
  if (!plugin || !form_key || !out_properties || !out_count) {
    return radicle_c_detail::fail(
        RADICLE_STATUS_INVALID_ARGUMENT,
        "plugin, form_key, out_properties and out_count are required");
  }

  *out_properties = nullptr;
  *out_count = 0;

  fo4::FormKey key;
  radicle_status status = RADICLE_STATUS_OK;
  if (!parse_form_key(form_key, key, status)) {
    return status;
  }

  try {
    const auto data = fo4::quest::quest_runtime_data(plugin->value, key);

    std::vector<radicle_quest_property> properties;
    const std::string no_script;
    for (const auto& script : data.scripts) {
      append_properties(properties, script.properties, RADICLE_QUEST_PROPERTY_SCRIPT, -1,
                        script.name);
    }
    append_properties(properties, data.fragment_properties, RADICLE_QUEST_PROPERTY_FRAGMENT, -1,
                      no_script);
    for (const auto& alias : data.alias_scripts) {
      for (const auto& script : alias.scripts) {
        append_properties(properties, script.properties, RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT,
                          alias.alias_id, script.name);
      }
    }

    return copy_properties(properties, out_properties, out_count);
  } catch (const std::invalid_argument& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::bad_alloc& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return radicle_c_detail::fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return radicle_c_detail::fail(RADICLE_STATUS_INTERNAL_ERROR,
                                  "unknown exception while decoding quest properties");
  }
}

void radicle_free_quest_properties(radicle_quest_property* properties, size_t count) {
  radicle_c_detail::clear_error();
  if (!properties) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    free_text(properties[i]);
  }
  std::free(properties);
}

}
