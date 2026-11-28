#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct radicle_swf_as3 radicle_swf_as3;

typedef enum radicle_swf_as3_language {
  RADICLE_SWF_AS3_LANGUAGE_AS3 = 0,
  RADICLE_SWF_AS3_LANGUAGE_CSHARP = 1,
} radicle_swf_as3_language;

typedef enum radicle_swf_as3_scope {
  RADICLE_SWF_AS3_SCOPE_FILE = 0,
  RADICLE_SWF_AS3_SCOPE_CLASS = 1,
  RADICLE_SWF_AS3_SCOPE_METHOD = 2,
} radicle_swf_as3_scope;

typedef struct radicle_swf_as3_info {
  uint64_t hash;
  size_t text_size;
  size_t class_count;
  size_t function_count;
  size_t refused_count;
} radicle_swf_as3_info;

/* Invariant: method, body and offset are 0xFFFFFFFF when the refusal has none. */
typedef struct radicle_swf_as3_issue {
  uint8_t status;
  uint32_t method;
  uint32_t body;
  uint32_t offset;
  const char* status_name;
  const char* message;
} radicle_swf_as3_issue;

/* Requires index below the block's instance count for CLASS and its method count for METHOD; FILE ignores index. */
RADICLE_API radicle_status radicle_swf_abc_decompile_ex(const radicle_swf* swf, size_t abc,
                                                        radicle_swf_as3_language language,
                                                        radicle_swf_as3_scope scope, uint32_t index,
                                                        radicle_swf_as3** out_as3);
RADICLE_API void radicle_swf_as3_destroy(radicle_swf_as3* as3);
RADICLE_API radicle_status radicle_swf_as3_get_info(const radicle_swf_as3* as3,
                                                    radicle_swf_as3_info* out_info);
/* Invariant: the text is NUL-terminated one byte past *out_size; the caller must not free it. */
RADICLE_API radicle_status radicle_swf_as3_text(const radicle_swf_as3* as3, const char** out_text,
                                                size_t* out_size);
RADICLE_API radicle_status radicle_swf_as3_issue_at(const radicle_swf_as3* as3, size_t index,
                                                    radicle_swf_as3_issue* out_issue);

#ifdef __cplusplus
}
#endif
