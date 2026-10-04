#include "fo4/c_api_pex.h"

#include "c_api_internal.hpp"
#include "fo4/pex/pex.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

struct radicle_pex {
  fo4::pex::PexFile value;
};

namespace {

using namespace radicle_c_detail;

radicle_status copy_bytes(const std::string& value, uint8_t** out_bytes, size_t* out_size) {
  if (value.empty()) {
    return RADICLE_STATUS_OK;
  }
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate string bytes");
  }
  std::memcpy(buffer, value.data(), value.size());
  *out_bytes = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

radicle_status entry_at(const std::vector<std::string>& table,
                        size_t index,
                        const char* what,
                        uint8_t** out_bytes,
                        size_t* out_size) {
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= table.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, what);
  }
  try {
    return copy_bytes(table[index], out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading .pex data");
  }
}

}  // namespace

extern "C" {

radicle_status radicle_pex_parse(const uint8_t* bytes, size_t size, radicle_pex** out_pex) {
  clear_error();
  if ((!bytes && size != 0) || !out_pex) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes and out_pex are required");
  }

  *out_pex = nullptr;
  try {
    std::vector<std::uint8_t> input;
    if (size != 0) {
      input.assign(bytes, bytes + size);
    }
    auto handle = std::make_unique<radicle_pex>();
    handle->value = fo4::pex::PexFile::fromBytes(input);
    *out_pex = handle.release();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while parsing .pex data");
  }
}

radicle_status radicle_pex_read_file(const char* path, radicle_pex** out_pex) {
  clear_error();
  if (!path || !out_pex) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path and out_pex are required");
  }

  *out_pex = nullptr;
  try {
    std::ifstream stream(utf8_path(path), std::ios::binary);
    if (!stream) {
      return fail(RADICLE_STATUS_IO_ERROR, "failed to open the .pex file");
    }

    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (stream.bad()) {
      return fail(RADICLE_STATUS_IO_ERROR, "failed to read the .pex file");
    }
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    return radicle_pex_parse(bytes.data(), bytes.size(), out_pex);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading .pex data");
  }
}

void radicle_pex_destroy(radicle_pex* pex) {
  clear_error();
  delete pex;
}

radicle_status radicle_pex_write_bytes(const radicle_pex* pex, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }

  *out_bytes = nullptr;
  *out_size = 0;
  try {
    const auto bytes = pex->value.toBytes();
    if (bytes.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (!buffer) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate serialized bytes");
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_bytes = buffer;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while serializing .pex data");
  }
}

radicle_status radicle_pex_get_header(const radicle_pex* pex, radicle_pex_header* out_header) {
  clear_error();
  if (!pex || !out_header) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex and out_header are required");
  }

  const auto& file = pex->value;
  radicle_pex_header header{};
  header.major_version = file.majorVersion;
  header.minor_version = file.minorVersion;
  header.game_id = file.gameId;
  header.compilation_time = file.compilationTime;
  header.string_count = file.stringTable.size();
  header.object_count = file.objects.size();
  *out_header = header;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_pex_object_get_counts(const radicle_pex* pex,
                                             size_t index,
                                             radicle_pex_object_counts* out_counts) {
  clear_error();
  if (!pex || !out_counts) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex and out_counts are required");
  }

  *out_counts = radicle_pex_object_counts{};
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }

  const auto& object = pex->value.objects[index];
  radicle_pex_object_counts counts{};
  counts.struct_count = object.structs.size();
  counts.variable_count = object.variables.size();
  counts.property_count = object.properties.size();
  counts.state_count = object.states.size();
  for (const auto& state : object.states) {
    counts.function_count += state.functions.size();
  }
  *out_counts = counts;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_pex_object_name(const radicle_pex* pex,
                                       size_t index,
                                       uint8_t** out_bytes,
                                       size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }
  try {
    return copy_bytes(pex->value.objects[index].name, out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading a .pex object name");
  }
}

radicle_status radicle_pex_object_parent_name(const radicle_pex* pex,
                                              size_t index,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= pex->value.objects.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "object index is out of range");
  }
  try {
    return copy_bytes(pex->value.objects[index].parentClassName, out_bytes, out_size);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading a parent class name");
  }
}

radicle_status radicle_pex_string_table_entry(const radicle_pex* pex,
                                              size_t index,
                                              uint8_t** out_bytes,
                                              size_t* out_size) {
  clear_error();
  if (!pex || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "pex, out_bytes and out_size are required");
  }
  return entry_at(pex->value.stringTable, index, "string table index is out of range", out_bytes, out_size);
}

}  // extern "C"
