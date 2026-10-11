#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_material_file radicle_material_file;

typedef enum radicle_material_format {
  RADICLE_MATERIAL_FORMAT_BGSM = 0,
  RADICLE_MATERIAL_FORMAT_BGEM = 1
} radicle_material_format;

typedef enum radicle_material_field_type {
  RADICLE_MATERIAL_FIELD_BOOL = 0,
  RADICLE_MATERIAL_FIELD_U8 = 1,
  RADICLE_MATERIAL_FIELD_U32 = 2,
  RADICLE_MATERIAL_FIELD_F32 = 3,
  RADICLE_MATERIAL_FIELD_COLOR3 = 4,
  RADICLE_MATERIAL_FIELD_STRING = 5
} radicle_material_field_type;

typedef struct radicle_material_color3 {
  float r;
  float g;
  float b;
} radicle_material_color3;

RADICLE_API radicle_status radicle_material_file_read(const uint8_t* bytes,
                                                      size_t size,
                                                      radicle_material_file** out_file);
RADICLE_API void radicle_material_file_destroy(radicle_material_file* file);
RADICLE_API radicle_status radicle_material_file_write(const radicle_material_file* file,
                                                       uint8_t** out_bytes,
                                                       size_t* out_size);
RADICLE_API radicle_status radicle_material_file_format(const radicle_material_file* file,
                                                        radicle_material_format* out_format);
RADICLE_API radicle_status radicle_material_file_version(const radicle_material_file* file,
                                                         uint32_t* out_version);

RADICLE_API radicle_status radicle_material_file_field_count(const radicle_material_file* file,
                                                             size_t* out_count);
RADICLE_API radicle_status radicle_material_file_field_name(const radicle_material_file* file,
                                                            size_t index,
                                                            uint8_t** out_bytes,
                                                            size_t* out_size);
RADICLE_API radicle_status radicle_material_file_field_type(const radicle_material_file* file,
                                                            size_t index,
                                                            radicle_material_field_type* out_type);

RADICLE_API radicle_status radicle_material_file_get_bool(const radicle_material_file* file,
                                                          const char* name,
                                                          uint8_t* out_value);
RADICLE_API radicle_status radicle_material_file_get_u8(const radicle_material_file* file,
                                                        const char* name,
                                                        uint8_t* out_value);
RADICLE_API radicle_status radicle_material_file_get_u32(const radicle_material_file* file,
                                                         const char* name,
                                                         uint32_t* out_value);
RADICLE_API radicle_status radicle_material_file_get_f32(const radicle_material_file* file,
                                                         const char* name,
                                                         float* out_value);
RADICLE_API radicle_status radicle_material_file_get_color3(const radicle_material_file* file,
                                                            const char* name,
                                                            radicle_material_color3* out_value);
RADICLE_API radicle_status radicle_material_file_get_string(const radicle_material_file* file,
                                                            const char* name,
                                                            uint8_t** out_bytes,
                                                            size_t* out_size);

RADICLE_API radicle_status radicle_material_file_set_bool(radicle_material_file* file,
                                                          const char* name,
                                                          uint8_t value);
RADICLE_API radicle_status radicle_material_file_set_u8(radicle_material_file* file,
                                                        const char* name,
                                                        uint8_t value);
RADICLE_API radicle_status radicle_material_file_set_u32(radicle_material_file* file,
                                                         const char* name,
                                                         uint32_t value);
RADICLE_API radicle_status radicle_material_file_set_f32(radicle_material_file* file,
                                                         const char* name,
                                                         float value);
RADICLE_API radicle_status radicle_material_file_set_color3(radicle_material_file* file,
                                                            const char* name,
                                                            const radicle_material_color3* value);
RADICLE_API radicle_status radicle_material_file_set_string(radicle_material_file* file,
                                                            const char* name,
                                                            const uint8_t* bytes,
                                                            size_t size);

#ifdef __cplusplus
}
#endif
