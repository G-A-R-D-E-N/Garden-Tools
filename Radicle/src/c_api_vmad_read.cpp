#include "c_api_env_internal.hpp"
#include "fo4/c_api_vmad.h"
#include "fo4/error.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/schema/vmad.hpp"
#include "fo4/signature.hpp"
#include "fo4/text/encoding.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct element_row {
  std::uint8_t kind{RADICLE_VMAD_NONE};
  std::string member_name;
  std::size_t struct_index{0};
  std::size_t index_in_property{0};
  std::uint8_t type{0};
  std::uint8_t flags{0};
  std::uint32_t object_form_id{0};
  std::int16_t alias{0};
  std::string text;
  std::int32_t integer{0};
  float real{0.0f};
  bool boolean{false};
};

struct property_row {
  std::string name;
  std::uint8_t type{0};
  std::uint8_t flags{0};
  std::uint8_t value_kind{RADICLE_VMAD_NONE};
  std::uint32_t object_form_id{0};
  std::int16_t alias{0};
  std::string text;
  std::int32_t integer{0};
  float real{0.0f};
  bool boolean{false};
  std::uint32_t declared_element_count{0};
  std::uint8_t element_kind{RADICLE_VMAD_NONE};
  std::size_t first_element{0};
  std::size_t element_count{0};
};

struct script_row {
  std::string name;
  std::uint8_t flags{0};
  std::size_t first_property{0};
  std::size_t property_count{0};
};

}  // namespace

struct radicle_vmad_scripts {
  std::int32_t unsupported{0};
  std::size_t trailing_bytes{0};
  std::vector<script_row> scripts;
  std::vector<property_row> properties;
  std::vector<element_row> elements;
};

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::belongs_to_env;
using radicle_c_env_detail::guarded;
using radicle_c_env_detail::to_ref;

namespace {

void fill_scalar(const fo4::VmadProperty& property, property_row& row) {
  if (property.object) {
    row.value_kind = RADICLE_VMAD_OBJECT;
    row.object_form_id = property.object->value();
    row.alias = property.alias.value_or(0);
    return;
  }
  if (property.text) {
    row.value_kind = RADICLE_VMAD_TEXT;
    row.text = *property.text;
    return;
  }
  if (property.integer) {
    row.value_kind = RADICLE_VMAD_INT;
    row.integer = *property.integer;
    return;
  }
  if (property.real) {
    row.value_kind = RADICLE_VMAD_REAL;
    row.real = *property.real;
    return;
  }
  if (property.boolean) {
    row.value_kind = RADICLE_VMAD_BOOL;
    row.boolean = *property.boolean;
  }
}

element_row object_element(const fo4::VmadObjectElement& element, std::size_t index) {
  element_row row;
  row.kind = RADICLE_VMAD_OBJECT;
  row.index_in_property = index;
  row.object_form_id = element.object.value();
  row.alias = element.alias;
  return row;
}

element_row member_element(const fo4::VmadStructMember& member, std::size_t struct_index, std::size_t index) {
  element_row row;
  row.member_name = fo4::text::windows1252_to_utf8(member.name);
  row.struct_index = struct_index;
  row.index_in_property = index;
  row.type = member.type;
  row.flags = member.flags;
  if (member.object) {
    row.kind = RADICLE_VMAD_OBJECT;
    row.object_form_id = member.object->object.value();
    row.alias = member.object->alias;
  } else if (member.text) {
    row.kind = RADICLE_VMAD_TEXT;
    row.text = *member.text;
  } else if (member.integer) {
    row.kind = RADICLE_VMAD_INT;
    row.integer = *member.integer;
  } else if (member.real) {
    row.kind = RADICLE_VMAD_REAL;
    row.real = *member.real;
  } else if (member.boolean) {
    row.kind = RADICLE_VMAD_BOOL;
    row.boolean = *member.boolean;
  }
  return row;
}

void fill_elements(const fo4::VmadProperty& property, property_row& row, radicle_vmad_scripts& out) {
  row.first_element = out.elements.size();
  if (property.struct_elements) {
    row.element_kind = RADICLE_VMAD_STRUCT_MEMBER;
    for (std::size_t s = 0; s < property.struct_elements->size(); ++s) {
      for (const auto& member : (*property.struct_elements)[s].members) {
        out.elements.push_back(member_element(member, s, out.elements.size() - row.first_element));
      }
    }
  } else if (property.object_elements) {
    row.element_kind = RADICLE_VMAD_OBJECT;
    for (const auto& element : *property.object_elements) {
      out.elements.push_back(object_element(element, out.elements.size() - row.first_element));
    }
  } else if (property.text_elements) {
    row.element_kind = RADICLE_VMAD_TEXT;
    for (const auto& value : *property.text_elements) {
      element_row element;
      element.kind = RADICLE_VMAD_TEXT;
      element.index_in_property = out.elements.size() - row.first_element;
      element.text = value;
      out.elements.push_back(std::move(element));
    }
  } else if (property.integer_elements) {
    row.element_kind = RADICLE_VMAD_INT;
    for (const auto value : *property.integer_elements) {
      element_row element;
      element.kind = RADICLE_VMAD_INT;
      element.index_in_property = out.elements.size() - row.first_element;
      element.integer = value;
      out.elements.push_back(std::move(element));
    }
  } else if (property.real_elements) {
    row.element_kind = RADICLE_VMAD_REAL;
    for (const auto value : *property.real_elements) {
      element_row element;
      element.kind = RADICLE_VMAD_REAL;
      element.index_in_property = out.elements.size() - row.first_element;
      element.real = value;
      out.elements.push_back(std::move(element));
    }
  } else if (property.boolean_elements) {
    row.element_kind = RADICLE_VMAD_BOOL;
    for (const auto value : *property.boolean_elements) {
      element_row element;
      element.kind = RADICLE_VMAD_BOOL;
      element.index_in_property = out.elements.size() - row.first_element;
      element.boolean = value;
      out.elements.push_back(std::move(element));
    }
  }
  row.element_count = out.elements.size() - row.first_element;
}

constexpr std::uint8_t k_struct_type = 7;
constexpr std::uint8_t k_struct_array_type = 17;

void fill_handle(const fo4::VmadScripts& decoded, radicle_vmad_scripts& handle) {
  handle.unsupported = decoded.unsupported ? 1 : 0;
  handle.trailing_bytes = decoded.trailing_bytes;
  for (const auto& script : decoded.scripts) {
    script_row row;
    row.name = fo4::text::windows1252_to_utf8(script.name);
    row.flags = script.flags;
    row.first_property = handle.properties.size();
    for (const auto& property : script.properties) {
      property_row entry;
      entry.name = fo4::text::windows1252_to_utf8(property.name);
      entry.type = property.type;
      entry.flags = property.flags;
      entry.declared_element_count = property.element_count.value_or(0);
      fill_scalar(property, entry);
      fill_elements(property, entry, handle);
      // Invariant: a struct whose members this reader cannot decode must report unsupported, because its rows are lost.
      if ((entry.type == k_struct_type || entry.type == k_struct_array_type) && !property.struct_elements) {
        handle.unsupported = 1;
      }
      handle.properties.push_back(std::move(entry));
    }
    row.property_count = handle.properties.size() - row.first_property;
    handle.scripts.push_back(std::move(row));
  }
}

const fo4::Subrecord* find_vmad(const fo4::Record& record) {
  constexpr fo4::Signature k_vmad = fo4::Signature::from_string("VMAD");
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == k_vmad) return &subrecord;
  }
  return nullptr;
}

}  // namespace

extern "C" {

radicle_status radicle_record_vmad_scripts(const radicle_env* env,
                                            const radicle_record* record,
                                            radicle_vmad_scripts** out_scripts) {
  clear_error();
  if (out_scripts != nullptr) *out_scripts = nullptr;
  if (env == nullptr || record == nullptr || out_scripts == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "env, record and out_scripts are required");
  }
  return guarded("unknown exception while decoding a record VMAD", [&] {
    const auto* ref = to_ref(record);
    if (!belongs_to_env(env, ref)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record does not belong to env");
    }
    // Invariant: an undecodable record reports a decode failure, never NOT_FOUND, because its payload is unknown.
    try {
      fo4::require_decodable(*ref->record);
    } catch (const std::invalid_argument& error) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.what());
    }
    const auto* vmad = find_vmad(*ref->record);
    if (vmad == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "record has no VMAD subrecord");
    fo4::VmadScripts decoded;
    try {
      decoded = fo4::read_vmad_scripts(vmad->data);
    } catch (const fo4::parse_error& error) {
      return fail(RADICLE_STATUS_PARSE_ERROR, error.what());
    }
    auto handle = std::make_unique<radicle_vmad_scripts>();
    fill_handle(decoded, *handle);
    *out_scripts = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_vmad_scripts_destroy(radicle_vmad_scripts* scripts) {
  clear_error();
  delete scripts;
}

size_t radicle_vmad_script_count(const radicle_vmad_scripts* scripts) {
  clear_error();
  return scripts == nullptr ? 0 : scripts->scripts.size();
}

radicle_status radicle_vmad_scripts_script(const radicle_vmad_scripts* scripts,
                                            size_t index,
                                            radicle_vmad_script* out_script) {
  clear_error();
  if (scripts == nullptr || out_script == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scripts and out_script are required");
  }
  *out_script = {};
  if (index >= scripts->scripts.size()) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "script index is out of range");
  const auto& script = scripts->scripts[index];
  out_script->name = script.name.c_str();
  out_script->flags = script.flags;
  out_script->property_count = script.property_count;
  out_script->trailing_bytes = scripts->trailing_bytes;
  out_script->unsupported = scripts->unsupported;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_vmad_scripts_property(const radicle_vmad_scripts* scripts,
                                              size_t script_index,
                                              size_t property_index,
                                              radicle_vmad_property* out_property) {
  clear_error();
  if (scripts == nullptr || out_property == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scripts and out_property are required");
  }
  *out_property = {};
  if (script_index >= scripts->scripts.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "script index is out of range");
  }
  const auto& script = scripts->scripts[script_index];
  if (property_index >= script.property_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "property index is out of range");
  }
  const auto& property = scripts->properties[script.first_property + property_index];
  out_property->name = property.name.c_str();
  out_property->type = property.type;
  out_property->flags = property.flags;
  out_property->value_kind = static_cast<radicle_vmad_value_kind>(property.value_kind);
  out_property->object_form_id = property.object_form_id;
  out_property->alias = property.alias;
  out_property->text = property.value_kind == RADICLE_VMAD_TEXT ? property.text.c_str() : nullptr;
  out_property->integer = property.integer;
  out_property->real = property.real;
  out_property->boolean = property.boolean ? 1 : 0;
  out_property->declared_element_count = property.declared_element_count;
  out_property->element_kind = static_cast<radicle_vmad_value_kind>(property.element_kind);
  out_property->element_total = property.element_count;
  return RADICLE_STATUS_OK;
}

size_t radicle_vmad_element_count(const radicle_vmad_scripts* scripts,
                                   size_t script_index,
                                   size_t property_index) {
  clear_error();
  if (scripts == nullptr || script_index >= scripts->scripts.size()) return 0;
  const auto& script = scripts->scripts[script_index];
  if (property_index >= script.property_count) return 0;
  return scripts->properties[script.first_property + property_index].element_count;
}

radicle_status radicle_vmad_scripts_element(const radicle_vmad_scripts* scripts,
                                             size_t script_index,
                                             size_t property_index,
                                             size_t element_index,
                                             radicle_vmad_element* out_element) {
  clear_error();
  if (scripts == nullptr || out_element == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "scripts and out_element are required");
  }
  *out_element = {};
  if (script_index >= scripts->scripts.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "script index is out of range");
  }
  const auto& script = scripts->scripts[script_index];
  if (property_index >= script.property_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "property index is out of range");
  }
  const auto& property = scripts->properties[script.first_property + property_index];
  if (element_index >= property.element_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "element index is out of range");
  }
  const auto& element = scripts->elements[property.first_element + element_index];
  out_element->kind = static_cast<radicle_vmad_value_kind>(element.kind);
  out_element->member_name = element.member_name.empty() ? nullptr : element.member_name.c_str();
  out_element->struct_index = element.struct_index;
  out_element->index_in_property = element.index_in_property;
  out_element->type = element.type;
  out_element->flags = element.flags;
  out_element->object_form_id = element.object_form_id;
  out_element->alias = element.alias;
  out_element->text = element.kind == RADICLE_VMAD_TEXT ? element.text.c_str() : nullptr;
  out_element->integer = element.integer;
  out_element->real = element.real;
  out_element->boolean = element.boolean ? 1 : 0;
  return RADICLE_STATUS_OK;
}

}  // extern "C"
