#include "fo4/c_api_quest.h"

#include "c_api_internal.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/quest/runtime.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <new>
#include <utility>
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

struct ElementStore {
  fo4::PluginFormIdContext context;
  std::deque<std::string> plugins;
  std::vector<std::vector<radicle_script_element>> arrays;
  std::vector<std::vector<radicle_script_struct_member>> members;
  std::vector<std::vector<radicle_script_struct>> structs;
};

void free_elements(radicle_script_element* elements, size_t count) {
  if (!elements) return;
  for (size_t index = 0; index < count; ++index) {
    std::free(const_cast<char*>(elements[index].object.plugin));
    std::free(const_cast<char*>(elements[index].text));
  }
  std::free(elements);
}

void free_members(radicle_script_struct_member* members, size_t count) {
  if (!members) return;
  for (size_t index = 0; index < count; ++index) {
    std::free(const_cast<char*>(members[index].name));
    std::free(const_cast<char*>(members[index].value.object.plugin));
    std::free(const_cast<char*>(members[index].value.text));
  }
  std::free(members);
}

void free_structs(radicle_script_struct* structs, size_t count) {
  if (!structs) return;
  for (size_t index = 0; index < count; ++index) {
    free_members(const_cast<radicle_script_struct_member*>(structs[index].members),
                 structs[index].member_count);
  }
  std::free(structs);
}

void free_owned(radicle_quest_property& property) {
  std::free(const_cast<char*>(property.script));
  std::free(const_cast<char*>(property.name));
  std::free(const_cast<char*>(property.text));
  free_elements(const_cast<radicle_script_element*>(property.elements), property.element_value_count);
  free_structs(const_cast<radicle_script_struct*>(property.structs), property.struct_count);
  property.script = nullptr;
  property.name = nullptr;
  property.text = nullptr;
  property.text_size = 0;
  property.elements = nullptr;
  property.element_value_count = 0;
  property.structs = nullptr;
  property.struct_count = 0;
}

radicle_form_key element_form_key(fo4::RawFormId raw, ElementStore& store) {
  const auto key = fo4::resolve_plugin_link(raw, store.context);
  if (key.owner().empty()) return {nullptr, 0};
  store.plugins.emplace_back(key.owner().filename());
  return {store.plugins.back().c_str(), key.local().value()};
}

void append_elements(radicle_quest_property& value,
                     const fo4::VmadProperty& property,
                     ElementStore& store) {
  std::vector<radicle_script_element> elements;
  if (property.object_elements) {
    for (const auto& element : *property.object_elements) {
      radicle_script_element out{};
      out.object = element_form_key(element.object, store);
      out.alias = element.alias;
      elements.push_back(out);
    }
  } else if (property.text_elements) {
    for (const auto& text : *property.text_elements) {
      radicle_script_element out{};
      out.text = text.c_str();
      elements.push_back(out);
    }
  } else if (property.integer_elements) {
    for (const auto integer : *property.integer_elements) {
      radicle_script_element out{};
      out.integer = integer;
      elements.push_back(out);
    }
  } else if (property.real_elements) {
    for (const auto real : *property.real_elements) {
      radicle_script_element out{};
      out.real = real;
      elements.push_back(out);
    }
  } else if (property.boolean_elements) {
    for (const auto boolean : *property.boolean_elements) {
      radicle_script_element out{};
      out.boolean = boolean ? 1 : 0;
      elements.push_back(out);
    }
  } else {
    return;
  }
  auto& array = store.arrays.emplace_back(std::move(elements));
  value.has_elements = 1;
  value.elements = array.empty() ? nullptr : array.data();
  value.element_value_count = array.size();
}

void append_structs(radicle_quest_property& value,
                    const fo4::VmadProperty& property,
                    ElementStore& store) {
  if (!property.struct_elements) return;
  std::vector<radicle_script_struct> structs;
  for (const auto& structure : *property.struct_elements) {
    auto& members = store.members.emplace_back();
    for (const auto& member : structure.members) {
      radicle_script_struct_member out{};
      out.name = member.name.c_str();
      out.type = member.type;
      out.flags = member.flags;
      if (member.object) {
        out.value.object = element_form_key(member.object->object, store);
        out.value.alias = member.object->alias;
      }
      if (member.text) out.value.text = member.text->c_str();
      if (member.integer) out.value.integer = *member.integer;
      if (member.real) out.value.real = *member.real;
      if (member.boolean) out.value.boolean = *member.boolean ? 1 : 0;
      members.push_back(out);
    }
    structs.push_back({members.data(), members.size()});
  }
  auto& array = store.structs.emplace_back(std::move(structs));
  value.has_structs = 1;
  value.structs = array.empty() ? nullptr : array.data();
  value.struct_count = array.size();
}

void append_properties(std::vector<radicle_quest_property>& out,
                       const std::vector<fo4::VmadProperty>& properties,
                       radicle_quest_property_owner owner,
                       std::int32_t alias,
                       const std::string& script,
                       ElementStore& store) {
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
    append_elements(value, property, store);
    append_structs(value, property, store);
    out.push_back(value);
  }
}

radicle_script_element* copy_elements(const radicle_script_element* source, size_t count) {
  if (count == 0) return nullptr;
  auto* copy = static_cast<radicle_script_element*>(
      std::calloc(count, sizeof(radicle_script_element)));
  if (!copy) return nullptr;
  for (size_t index = 0; index < count; ++index) {
    copy[index] = source[index];
    copy[index].object.plugin = copy_c_string(source[index].object.plugin);
    copy[index].text = copy_c_string(source[index].text);
    if ((source[index].object.plugin && !copy[index].object.plugin) ||
        (source[index].text && !copy[index].text)) {
      free_elements(copy, count);
      return nullptr;
    }
  }
  return copy;
}

radicle_script_struct_member* copy_members(const radicle_script_struct_member* source, size_t count) {
  if (count == 0) return nullptr;
  auto* copy = static_cast<radicle_script_struct_member*>(
      std::calloc(count, sizeof(radicle_script_struct_member)));
  if (!copy) return nullptr;
  for (size_t index = 0; index < count; ++index) {
    copy[index] = source[index];
    copy[index].name = copy_c_string(source[index].name);
    copy[index].value.object.plugin = copy_c_string(source[index].value.object.plugin);
    copy[index].value.text = copy_c_string(source[index].value.text);
    if ((source[index].name && !copy[index].name) ||
        (source[index].value.object.plugin && !copy[index].value.object.plugin) ||
        (source[index].value.text && !copy[index].value.text)) {
      free_members(copy, count);
      return nullptr;
    }
  }
  return copy;
}

radicle_script_struct* copy_structs(const radicle_script_struct* source, size_t count) {
  if (count == 0) return nullptr;
  auto* copy = static_cast<radicle_script_struct*>(
      std::calloc(count, sizeof(radicle_script_struct)));
  if (!copy) return nullptr;
  for (size_t index = 0; index < count; ++index) {
    copy[index] = source[index];
    copy[index].members = copy_members(source[index].members, source[index].member_count);
    if (source[index].member_count && !copy[index].members) {
      free_structs(copy, count);
      return nullptr;
    }
  }
  return copy;
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
    result[i].elements = copy_elements(values[i].elements, values[i].element_value_count);
    result[i].structs = copy_structs(values[i].structs, values[i].struct_count);
    const bool failed = (values[i].script && !result[i].script) ||
                        (values[i].name && !result[i].name) ||
                        (values[i].text && !result[i].text) ||
                        (values[i].element_value_count && !result[i].elements) ||
                        (values[i].struct_count && !result[i].structs);
    if (failed) {
      free_owned(result[i]);
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
    ElementStore store{fo4::plugin_form_id_context(plugin->value), {}, {}, {}, {}};
    const std::string no_script;
    for (const auto& script : data.scripts) {
      append_properties(properties, script.properties, RADICLE_QUEST_PROPERTY_SCRIPT, -1,
                        script.name, store);
    }
    append_properties(properties, data.fragment_properties, RADICLE_QUEST_PROPERTY_FRAGMENT, -1,
                      no_script, store);
    for (const auto& alias : data.alias_scripts) {
      for (const auto& script : alias.scripts) {
        append_properties(properties, script.properties, RADICLE_QUEST_PROPERTY_ALIAS_SCRIPT,
                          alias.alias_id, script.name, store);
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
    free_owned(properties[i]);
  }
  std::free(properties);
}

}
