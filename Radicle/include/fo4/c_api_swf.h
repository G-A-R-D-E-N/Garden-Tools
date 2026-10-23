#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed movie. A string it hands back must stay valid until radicle_swf_destroy and the
   caller must not free one; opening parses once, so every getter is a pure function of the bytes. */
typedef struct radicle_swf radicle_swf;

#define RADICLE_SWF_NO_INDEX ((size_t)-1)

typedef struct radicle_swf_info {
  uint8_t format;              /* 0 SWF, 1 GFx */
  uint8_t compression;         /* 0 none, 1 zlib, 2 lzma; an LZMA movie must not open */
  uint8_t version;
  uint8_t action_script3;
  uint8_t has_metadata;
  uint8_t use_network;
  uint8_t use_gpu;
  uint8_t use_direct_blit;
  uint8_t declared_length_matches;
  uint16_t frame_count;
  uint32_t file_length;
  float frame_rate;            /* header frame rate, in frames per second */
  size_t tag_count;            /* flattened tag tree; must include sprite children */
  size_t shape_tags;
  size_t bitmap_tags;
  size_t font_tags;
  size_t text_tags;
  size_t sound_tags;
  size_t exporter_info_tags;
  size_t external_image_tags;
  size_t font_texture_tags;
  size_t symbol_count;
  size_t export_count;
  size_t frame_label_count;
  size_t abc_count;
} radicle_swf_info;

/* One tag of the flattened tree: `parent` must be the enclosing DefineSprite tag or RADICLE_SWF_NO_INDEX. */
typedef struct radicle_swf_tag {
  uint16_t code;
  uint32_t length;
  size_t offset;               /* must be the byte offset of the payload, past its header, inside the decompressed tag stream; not a file offset */
  size_t parent;
  uint16_t sprite_id;          /* must be 0 for a tag outside any sprite; a DefineSprite tag reports its own id */
  uint8_t inside_sprite;
  uint8_t long_header;
  size_t payload_size;
  const char* name;            /* tag_code_name; never null */
} radicle_swf_tag;

typedef struct radicle_swf_symbol {
  uint16_t tag_id;
  const char* name;            /* must be the SymbolClass class name or the ExportAssets name */
} radicle_swf_symbol;

typedef struct radicle_swf_frame_label {
  const char* name;
  size_t tag_index;
  uint16_t sprite_id;
  uint8_t inside_sprite;
} radicle_swf_frame_label;

/* `parsed` is 0 when the block did not parse; the other fields must still be valid then. */
typedef struct radicle_swf_abc_info {
  const char* name;            /* must be the tag-header name for DoABC2 (82); the C ABI decodes a DoABC (72) name from its own header */
  uint32_t flags;
  size_t tag_index;
  size_t payload_size;
  int32_t parsed;
} radicle_swf_abc_info;

/* A method as the ABC summary lists it; the strings must live as long as the movie. */
typedef struct radicle_swf_method {
  const char* name;
  const char* return_type;
  uint32_t flags;
  size_t instruction_count;
  size_t call_count;
  size_t multiname_count;
} radicle_swf_method;

typedef struct radicle_swf_class {
  const char* name;
  const char* super_name;
  size_t interface_count;
  size_t method_count;
} radicle_swf_class;

typedef struct radicle_swf_call {
  size_t offset;
  const char* opcode;
  const char* target;
  const char* receiver;
  uint32_t argument_count;
  const char* event_name;      /* must be empty when the call registers no event */
  const char* handler;         /* must be empty when the call registers no event */
  int32_t is_external_interface;
  int32_t is_event_registration;
} radicle_swf_call;

/* One entry of the ABC method pool: the view radicle_swf_abc_method_disassemble must decode. */
typedef struct radicle_swf_method_pool {
  const char* name;
  const char* return_type;
  uint32_t param_count;
  uint8_t flags;
  int32_t has_body;
  uint32_t max_stack;
  uint32_t local_count;
  uint32_t init_scope_depth;
  uint32_t max_scope_depth;
  uint32_t code_size;
  size_t exception_count;
} radicle_swf_method_pool;

RADICLE_API radicle_status radicle_swf_open_bytes(const uint8_t* bytes, size_t size, radicle_swf** out_swf);
RADICLE_API radicle_status radicle_swf_open_path(const char* path, radicle_swf** out_swf);
/* The archive handle must outlive the call; the entry bytes are copied. */
RADICLE_API radicle_status radicle_swf_open_ba2(const radicle_ba2* ba2, const char* path, radicle_swf** out_swf);
RADICLE_API void radicle_swf_destroy(radicle_swf* swf);

RADICLE_API radicle_status radicle_swf_get_info(const radicle_swf* swf, radicle_swf_info* out_info);

/* Tag tree; indexes must address what radicle_swf_tag_count reports. */
RADICLE_API size_t radicle_swf_tag_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_tag_at(const radicle_swf* swf, size_t index, radicle_swf_tag* out_tag);
RADICLE_API radicle_status radicle_swf_tag_payload(const radicle_swf* swf, size_t index,
                                                   uint8_t** out_bytes, size_t* out_size);

/* SymbolClass, ExportAssets and FrameLabel records; ordering must stay file order. */
RADICLE_API size_t radicle_swf_symbol_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_symbol_at(const radicle_swf* swf, size_t index, radicle_swf_symbol* out_symbol);
RADICLE_API size_t radicle_swf_export_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_export_at(const radicle_swf* swf, size_t index, radicle_swf_symbol* out_export);
RADICLE_API size_t radicle_swf_frame_label_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_frame_label_at(const radicle_swf* swf, size_t index,
                                                      radicle_swf_frame_label* out_label);

/* ABC blocks; indexes must address what radicle_swf_abc_count reports. */
RADICLE_API size_t radicle_swf_abc_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_abc_info_at(const radicle_swf* swf, size_t abc,
                                                   radicle_swf_abc_info* out_info);

/* Summary view: classes and their methods, then scripts and free functions; indexes must address these lists. */
RADICLE_API size_t radicle_swf_abc_class_count(const radicle_swf* swf, size_t abc);
RADICLE_API radicle_status radicle_swf_abc_class_at(const radicle_swf* swf, size_t abc, size_t index,
                                                    radicle_swf_class* out_class);
RADICLE_API radicle_status radicle_swf_abc_class_interface(const radicle_swf* swf, size_t abc, size_t index,
                                                           size_t interface_index, const char** out_name);
RADICLE_API radicle_status radicle_swf_abc_class_method(const radicle_swf* swf, size_t abc, size_t index,
                                                        size_t method_index, radicle_swf_method* out_method);
RADICLE_API size_t radicle_swf_abc_script_count(const radicle_swf* swf, size_t abc);
RADICLE_API radicle_status radicle_swf_abc_script_at(const radicle_swf* swf, size_t abc, size_t index,
                                                     radicle_swf_method* out_method);
RADICLE_API size_t radicle_swf_abc_function_count(const radicle_swf* swf, size_t abc);
RADICLE_API radicle_status radicle_swf_abc_function_at(const radicle_swf* swf, size_t abc, size_t index,
                                                       radicle_swf_method* out_method);

/* Which summary list a method came from; the caller must pass it back with the getter's indexes. */
#define RADICLE_SWF_OWNER_CLASS 0
#define RADICLE_SWF_OWNER_SCRIPT 1
#define RADICLE_SWF_OWNER_FUNCTION 2

RADICLE_API radicle_status radicle_swf_abc_call_at(const radicle_swf* swf, size_t abc,
                                                   size_t owner, size_t owner_index, size_t method_index,
                                                   size_t call_index, radicle_swf_call* out_call);
RADICLE_API radicle_status radicle_swf_abc_multiname_at(const radicle_swf* swf, size_t abc,
                                                        size_t owner, size_t owner_index, size_t method_index,
                                                        size_t index, const char** out_name);

/* Method pool view: what radicle_swf_abc_method_disassemble must decode. */
RADICLE_API size_t radicle_swf_abc_method_count(const radicle_swf* swf, size_t abc);
RADICLE_API radicle_status radicle_swf_abc_method_at(const radicle_swf* swf, size_t abc, size_t index,
                                                     radicle_swf_method_pool* out_method);
/* Returns malloc'd UTF-8 text the caller must release with radicle_free_bytes; the buffer is NUL-terminated one byte past *out_size and must stay non-null even when *out_size is 0. */
RADICLE_API radicle_status radicle_swf_abc_method_disassemble(const radicle_swf* swf, size_t abc, size_t index,
                                                              uint8_t** out_text, size_t* out_size);


#ifdef __cplusplus
}
#endif
