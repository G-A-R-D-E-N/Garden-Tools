#include "fo4/c_api_material_file.h"

#include "c_api_internal.hpp"
#include "fo4/error.hpp"
#include "fo4/material/material_file.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

struct radicle_material_file {
  fo4::material::MaterialFile value;
};

namespace {

using namespace radicle_c_detail;

template <typename Body>
radicle_status guarded(const char* what, Body body) {
  try {
    return body();
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const fo4::parse_error& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (const std::invalid_argument& error) {
    return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, what);
  }
}

radicle_status copy_bytes(std::string_view value, uint8_t** out_bytes, size_t* out_size) {
  if (value.empty()) {
    return RADICLE_STATUS_OK;
  }
  auto* buffer = static_cast<uint8_t*>(std::malloc(value.size()));
  if (!buffer) {
    return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate material bytes");
  }
  std::memcpy(buffer, value.data(), value.size());
  *out_bytes = buffer;
  *out_size = value.size();
  return RADICLE_STATUS_OK;
}

radicle_material_field_type c_field_type(fo4::material::FieldType type) {
  using fo4::material::FieldType;
  switch (type) {
    case FieldType::boolean:
      return RADICLE_MATERIAL_FIELD_BOOL;
    case FieldType::u8:
      return RADICLE_MATERIAL_FIELD_U8;
    case FieldType::u32:
      return RADICLE_MATERIAL_FIELD_U32;
    case FieldType::f32:
      return RADICLE_MATERIAL_FIELD_F32;
    case FieldType::color3:
      return RADICLE_MATERIAL_FIELD_COLOR3;
    case FieldType::string:
      break;
  }
  return RADICLE_MATERIAL_FIELD_STRING;
}

template <typename T, typename Out>
radicle_status get_scalar(const radicle_material_file* file, const char* name, Out* out_value) {
  clear_error();
  if (!file || !name || !out_value) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file, name and out_value are required");
  }
  if (!fo4::material::find_field(file->value, name)) {
    return fail(RADICLE_STATUS_NOT_FOUND, "no material field has that name");
  }
  const auto value = fo4::material::get_field<T>(file->value, name);
  if (!value) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the material field has a different type");
  }
  *out_value = static_cast<Out>(*value);
  return RADICLE_STATUS_OK;
}

template <typename T>
radicle_status set_scalar(radicle_material_file* file, const char* name, T value) {
  clear_error();
  if (!file || !name) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file and name are required");
  }
  if (!fo4::material::find_field(file->value, name)) {
    return fail(RADICLE_STATUS_NOT_FOUND, "no material field has that name");
  }
  return guarded("unknown exception while setting a material field", [&] {
    if (!fo4::material::set_field(file->value, name, value)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the material field has a different type");
    }
    return RADICLE_STATUS_OK;
  });
}

}  // namespace

extern "C" {

radicle_status radicle_material_file_read(const uint8_t* bytes, size_t size, radicle_material_file** out_file) {
  clear_error();
  if ((!bytes && size != 0) || !out_file) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes and out_file are required");
  }

  *out_file = nullptr;
  return guarded("unknown exception while reading a material file", [&] {
    auto handle = std::make_unique<radicle_material_file>();
    handle->value = fo4::material::read_material_file(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes), size));
    *out_file = handle.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_material_file_destroy(radicle_material_file* file) {
  clear_error();
  delete file;
}

radicle_status radicle_material_file_write(const radicle_material_file* file,
                                           uint8_t** out_bytes,
                                           size_t* out_size) {
  clear_error();
  if (!file || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file, out_bytes and out_size are required");
  }

  *out_bytes = nullptr;
  *out_size = 0;
  return guarded("unknown exception while writing a material file", [&] {
    const auto bytes = fo4::material::write_material_file(file->value);
    if (bytes.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (!buffer) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate serialized material bytes");
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_bytes = buffer;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  });
}

radicle_status radicle_material_file_format(const radicle_material_file* file, radicle_material_format* out_format) {
  clear_error();
  if (!file || !out_format) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file and out_format are required");
  }
  *out_format = file->value.format == fo4::material::MaterialFormat::bgem ? RADICLE_MATERIAL_FORMAT_BGEM
                                                                          : RADICLE_MATERIAL_FORMAT_BGSM;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_material_file_version(const radicle_material_file* file, uint32_t* out_version) {
  clear_error();
  if (!file || !out_version) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file and out_version are required");
  }
  *out_version = file->value.version();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_material_file_field_count(const radicle_material_file* file, size_t* out_count) {
  clear_error();
  if (!file || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file and out_count are required");
  }
  *out_count = file->value.fields.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_material_file_field_name(const radicle_material_file* file,
                                                size_t index,
                                                uint8_t** out_bytes,
                                                size_t* out_size) {
  clear_error();
  if (!file || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (index >= file->value.fields.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "field index is out of range");
  }
  return guarded("unknown exception while reading a material field name", [&] {
    return copy_bytes(file->value.fields[index].name, out_bytes, out_size);
  });
}

radicle_status radicle_material_file_field_type(const radicle_material_file* file,
                                                size_t index,
                                                radicle_material_field_type* out_type) {
  clear_error();
  if (!file || !out_type) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file and out_type are required");
  }
  if (index >= file->value.fields.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "field index is out of range");
  }
  *out_type = c_field_type(file->value.fields[index].type());
  return RADICLE_STATUS_OK;
}

radicle_status radicle_material_file_get_bool(const radicle_material_file* file, const char* name, uint8_t* out_value) {
  return get_scalar<bool>(file, name, out_value);
}

radicle_status radicle_material_file_get_u8(const radicle_material_file* file, const char* name, uint8_t* out_value) {
  return get_scalar<uint8_t>(file, name, out_value);
}

radicle_status radicle_material_file_get_u32(const radicle_material_file* file,
                                             const char* name,
                                             uint32_t* out_value) {
  return get_scalar<uint32_t>(file, name, out_value);
}

radicle_status radicle_material_file_get_f32(const radicle_material_file* file, const char* name, float* out_value) {
  return get_scalar<float>(file, name, out_value);
}

radicle_status radicle_material_file_get_color3(const radicle_material_file* file,
                                                const char* name,
                                                radicle_material_color3* out_value) {
  clear_error();
  if (!file || !name || !out_value) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file, name and out_value are required");
  }
  if (!fo4::material::find_field(file->value, name)) {
    return fail(RADICLE_STATUS_NOT_FOUND, "no material field has that name");
  }
  const auto value = fo4::material::get_field<fo4::material::Color3>(file->value, name);
  if (!value) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the material field has a different type");
  }
  *out_value = radicle_material_color3{value->r, value->g, value->b};
  return RADICLE_STATUS_OK;
}

radicle_status radicle_material_file_get_string(const radicle_material_file* file,
                                                const char* name,
                                                uint8_t** out_bytes,
                                                size_t* out_size) {
  clear_error();
  if (!file || !name || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file, name, out_bytes and out_size are required");
  }
  *out_bytes = nullptr;
  *out_size = 0;
  if (!fo4::material::find_field(file->value, name)) {
    return fail(RADICLE_STATUS_NOT_FOUND, "no material field has that name");
  }
  return guarded("unknown exception while reading a material string", [&] {
    const auto value = fo4::material::get_field<std::string>(file->value, name);
    if (!value) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the material field has a different type");
    }
    return copy_bytes(fo4::material::text(*value), out_bytes, out_size);
  });
}

radicle_status radicle_material_file_set_bool(radicle_material_file* file, const char* name, uint8_t value) {
  return set_scalar(file, name, value != 0);
}

radicle_status radicle_material_file_set_u8(radicle_material_file* file, const char* name, uint8_t value) {
  return set_scalar(file, name, value);
}

radicle_status radicle_material_file_set_u32(radicle_material_file* file, const char* name, uint32_t value) {
  return set_scalar(file, name, value);
}

radicle_status radicle_material_file_set_f32(radicle_material_file* file, const char* name, float value) {
  return set_scalar(file, name, value);
}

radicle_status radicle_material_file_set_color3(radicle_material_file* file,
                                                const char* name,
                                                const radicle_material_color3* value) {
  clear_error();
  if (!value) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "value is required");
  }
  return set_scalar(file, name, fo4::material::Color3{value->r, value->g, value->b});
}

radicle_status radicle_material_file_set_string(radicle_material_file* file,
                                                const char* name,
                                                const uint8_t* bytes,
                                                size_t size) {
  clear_error();
  if (!file || !name || (!bytes && size != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "file, name and bytes are required");
  }
  if (!fo4::material::find_field(file->value, name)) {
    return fail(RADICLE_STATUS_NOT_FOUND, "no material field has that name");
  }
  return guarded("unknown exception while setting a material string", [&] {
    const std::string_view text(reinterpret_cast<const char*>(bytes), size);
    if (!fo4::material::set_text(file->value, name, text)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the material field has a different type");
    }
    return RADICLE_STATUS_OK;
  });
}

}
