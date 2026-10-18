#include "fo4/c_api.h"

#include "c_api_internal.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/form_links.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


namespace radicle_c_detail {

thread_local std::string g_last_error;

void clear_error() {
  g_last_error.clear();
}

radicle_status fail(radicle_status status, const char* message) {
  try {
    g_last_error = message ? message : "Unknown Radicle error";
  } catch (...) {
    // Allocation failed while storing the message; the status code still
    // reports the failure. Never let an exception escape extern "C".
  }
  return status;
}

radicle_status fail_exception(radicle_status status, const std::exception& error) {
  try {
    g_last_error = error.what();
  } catch (const std::bad_alloc&) {
    // Out of memory on this very path: keep whatever previous text is
    // present rather than throwing through the C boundary. Callers must
    // treat the status code as authoritative regardless of last_error.
  } catch (...) {
    try {
      g_last_error.clear();
    } catch (...) {
    }
  }
  return status;
}

std::filesystem::path utf8_path(std::string_view value) {
  std::u8string utf8;
  utf8.reserve(value.size());
  for (const unsigned char ch : value) {
    utf8.push_back(static_cast<char8_t>(ch));
  }
  return std::filesystem::path(utf8);
}

radicle_status copy_strings(const std::vector<std::string>& values,
                            char*** out_strings,
                            size_t* out_count) {
  *out_strings = nullptr;
  *out_count = 0;

  if (values.empty()) {
    return RADICLE_STATUS_OK;
  }

  auto** result = static_cast<char**>(std::calloc(values.size(), sizeof(char*)));
  if (!result) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate string array");
  }

  for (size_t i = 0; i < values.size(); ++i) {
    result[i] = static_cast<char*>(std::malloc(values[i].size() + 1));
    if (!result[i]) {
      radicle_free_string_array(result, i);
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate string");
    }
    std::memcpy(result[i], values[i].c_str(), values[i].size() + 1);
  }

  *out_strings = result;
  *out_count = values.size();
  return RADICLE_STATUS_OK;
}

}  // namespace radicle_c_detail

namespace {

using namespace radicle_c_detail;

fo4::PluginKind to_plugin_kind(radicle_plugin_kind kind) {
  switch (kind) {
    case RADICLE_PLUGIN_FULL:
      return fo4::PluginKind::full;
    case RADICLE_PLUGIN_LIGHT:
      return fo4::PluginKind::light;
  }
  throw std::invalid_argument("invalid Radicle plugin kind");
}


}  // namespace

extern "C" {

radicle_status radicle_plugin_read_file(const char* path, radicle_plugin** out_plugin) {
  clear_error();
  if (!path || !out_plugin) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path and out_plugin are required");
  }

  *out_plugin = nullptr;
  try {
    auto plugin = fo4::read_plugin_file(utf8_path(path));
    *out_plugin = new radicle_plugin{std::move(plugin)};
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading plugin");
  }
}

void radicle_plugin_destroy(radicle_plugin* plugin) {
  clear_error();
  delete plugin;
}

radicle_status radicle_plugin_write_file(const radicle_plugin* plugin, const char* path) {
  clear_error();
  if (!plugin || !path) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and path are required");
  }

  try {
    fo4::write_plugin_file(plugin->value, utf8_path(path));
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while writing plugin");
  }
}

radicle_status radicle_plugin_write_bytes(const radicle_plugin* plugin,
                                           uint8_t** out_bytes,
                                           size_t* out_size) {
  clear_error();
  if (!plugin || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, out_bytes, and out_size are required");
  }

  *out_bytes = nullptr;
  *out_size = 0;
  try {
    const auto bytes = fo4::write_plugin(plugin->value);
    if (bytes.empty()) {
      return RADICLE_STATUS_OK;
    }

    auto* result = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (!result) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate plugin bytes");
    }
    std::memcpy(result, bytes.data(), bytes.size());
    *out_bytes = result;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while serializing plugin");
  }
}

void radicle_free_bytes(uint8_t* bytes) {
  clear_error();
  std::free(bytes);
}

radicle_status radicle_plugin_stamp_master_kinds(radicle_plugin* plugin,
                                                  const char* const* master_paths,
                                                  size_t master_count) {
  clear_error();
  if (!plugin || (master_count != 0 && !master_paths)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and master paths are required");
  }

  try {
    std::vector<fo4::Plugin> masters;
    masters.reserve(master_count);
    for (size_t i = 0; i < master_count; ++i) {
      if (!master_paths[i]) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "master path cannot be null");
      }
      masters.push_back(fo4::read_plugin_file(utf8_path(master_paths[i])));
    }
    fo4::stamp_master_kinds(plugin->value, masters);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while stamping masters");
  }
}

radicle_status radicle_plugin_remap_masters(radicle_plugin* plugin,
                                             const radicle_master_descriptor* masters,
                                             size_t master_count) {
  clear_error();
  if (!plugin || (master_count != 0 && !masters)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and master descriptors are required");
  }

  try {
    std::vector<fo4::MasterDescriptor> descriptors;
    descriptors.reserve(master_count);
    for (size_t i = 0; i < master_count; ++i) {
      if (!masters[i].filename) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "master filename cannot be null");
      }
      descriptors.push_back({fo4::PluginKey::from_filename(masters[i].filename),
                             to_plugin_kind(masters[i].kind)});
    }
    auto remapped = fo4::remap_plugin_masters(plugin->value, std::move(descriptors));
    plugin->value = std::move(remapped);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while remapping masters");
  }
}

radicle_status radicle_plugin_set_author(radicle_plugin* plugin, const char* author) {
  clear_error();
  if (!plugin || !author) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and author are required");
  }

  try {
    plugin->value = fo4::set_plugin_author(plugin->value, author);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while setting author");
  }
}

radicle_status radicle_plugin_set_description(radicle_plugin* plugin, const char* description) {
  clear_error();
  if (!plugin || !description) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and description are required");
  }

  try {
    plugin->value = fo4::set_plugin_description(plugin->value, description);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while setting description");
  }
}

radicle_status radicle_plugin_set_master_flag(radicle_plugin* plugin, int32_t master) {
  clear_error();
  if (!plugin) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  }

  try {
    plugin->value = fo4::set_plugin_master_flag(plugin->value, master != 0);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while setting the master flag");
  }
}

radicle_status radicle_plugin_set_localized_flag(radicle_plugin* plugin, int32_t localized) {
  clear_error();
  if (!plugin) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  }

  try {
    plugin->value = fo4::set_plugin_localized_flag(plugin->value, localized != 0);
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR,
                "unknown exception while setting the localized flag");
  }
}

radicle_status radicle_plugin_collect_form_keys(const radicle_plugin* plugin,
                                                 char*** out_strings,
                                                 size_t* out_count) {
  clear_error();
  if (!plugin || !out_strings || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, out_strings, and out_count are required");
  }

  try {
    std::vector<std::string> values;
    for (const auto* record : fo4::collect_records(plugin->value)) {
      if (!record->form_key.owner().empty()) {
        values.push_back(record->form_key.to_string());
      }
    }
    return copy_strings(values, out_strings, out_count);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while collecting FormKeys");
  }
}

radicle_status radicle_plugin_collect_form_links(const radicle_plugin* plugin,
                                                  char*** out_strings,
                                                  size_t* out_count) {
  clear_error();
  if (!plugin || !out_strings || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, out_strings, and out_count are required");
  }

  try {
    std::vector<std::string> values;
    for (const auto& hit : fo4::collect_form_links(plugin->value)) {
      values.push_back(hit.key.to_string());
    }
    return copy_strings(values, out_strings, out_count);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while collecting FormLinks");
  }
}

void radicle_free_string_array(char** strings, size_t count) {
  clear_error();
  if (!strings) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    std::free(strings[i]);
  }
  std::free(strings);
}

const char* radicle_last_error(void) {
  return g_last_error.c_str();
}

}  // extern "C"
