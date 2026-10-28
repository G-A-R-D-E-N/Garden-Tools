#pragma once

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed movie. A string it hands back must stay valid until radicle_swf_destroy or the next
   accepted plan, and the caller must not free one; every getter must stay a pure function of the
   tags in force, because a plan must rewrite them. */
typedef struct radicle_swf radicle_swf;

#define RADICLE_SWF_NO_INDEX ((size_t)-1)

typedef struct radicle_swf_info {
  uint8_t format;              /* 0 SWF, 1 GFx */
  uint8_t compression;         /* 0 none, 1 zlib, 2 lzma */
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

typedef struct radicle_swf_rect {
  int32_t x_min;
  int32_t x_max;
  int32_t y_min;
  int32_t y_max;
} radicle_swf_rect;

typedef struct radicle_swf_shape {
  uint16_t character_id;
  uint8_t version;             /* 1..4 */
  uint8_t has_edge_bounds;
  uint8_t flags;               /* DefineShape4 flags byte; 0 for the earlier tags */
  radicle_swf_rect bounds;
  radicle_swf_rect edge_bounds;
  size_t fill_style_count;
  size_t line_style_count;
  uint32_t edge_count;
  uint32_t curved_edge_count;
  uint32_t style_change_count;
  size_t record_count;
} radicle_swf_shape;

typedef enum radicle_swf_record_kind {
  RADICLE_SWF_RECORD_MOVE = 0,
  RADICLE_SWF_RECORD_STYLE_CHANGE = 1,
  RADICLE_SWF_RECORD_STRAIGHT_EDGE = 2,
  RADICLE_SWF_RECORD_CURVED_EDGE = 3,
} radicle_swf_record_kind;

typedef struct radicle_swf_shape_record {
  radicle_swf_record_kind kind;
  int32_t x;
  int32_t y;
  int32_t control_x;
  int32_t control_y;
  uint16_t fill0;
  uint16_t fill1;
  uint16_t line;
} radicle_swf_shape_record;

typedef struct radicle_swf_fill_style {
  uint8_t kind;
  uint32_t color;              /* 0xAARRGGBB; an RGB solid fill carries 0xFF for alpha */
  uint16_t bitmap_id;
  uint8_t spread;
  uint8_t interpolation;
  int16_t focal_point;         /* 8.8 fixed, meaningful only for kind 0x13 */
  int32_t has_matrix;
  int32_t matrix[6];           /* scale_x, scale_y, rotate_skew0, rotate_skew1, translate_x, translate_y */
  size_t stop_count;
} radicle_swf_fill_style;

typedef struct radicle_swf_gradient_stop {
  uint8_t ratio;
  uint32_t color;              /* 0xAARRGGBB */
} radicle_swf_gradient_stop;

typedef struct radicle_swf_line_style {
  uint16_t width;
  uint16_t flags;
  uint32_t color;              /* 0xAARRGGBB, used when the style carries no fill */
  int32_t has_fill;
  int32_t has_miter_limit;
  uint16_t miter_limit;        /* 8.8 fixed */
} radicle_swf_line_style;

typedef struct radicle_swf_morph_shape {
  uint16_t character_id;
  uint8_t version;             /* 1 DefineMorphShape, 2 DefineMorphShape2 */
  uint8_t has_edge_bounds;
  uint8_t flags;
  radicle_swf_rect start_bounds;
  radicle_swf_rect end_bounds;
  radicle_swf_rect start_edge_bounds;
  radicle_swf_rect end_edge_bounds;
  uint32_t fill_style_count;
  uint32_t line_style_count;
  uint32_t start_edge_count;
  uint32_t end_edge_count;
  size_t start_record_count;
  size_t end_record_count;
} radicle_swf_morph_shape;

typedef struct radicle_swf_morph_gradient_stop {
  uint8_t start_ratio;
  uint8_t end_ratio;
  uint32_t start_color;
  uint32_t end_color;
} radicle_swf_morph_gradient_stop;

typedef struct radicle_swf_morph_fill_style {
  uint8_t kind;
  uint32_t start_color;
  uint32_t end_color;
  uint16_t bitmap_id;
  uint8_t spread;
  uint8_t interpolation;
  int32_t has_start_focal;
  int16_t start_focal;
  int32_t has_end_focal;
  int16_t end_focal;
  int32_t has_start_matrix;
  int32_t start_matrix[6];
  int32_t has_end_matrix;
  int32_t end_matrix[6];
  size_t stop_count;
} radicle_swf_morph_fill_style;

typedef struct radicle_swf_morph_line_style {
  uint16_t start_width;
  uint16_t end_width;
  uint16_t flags;
  uint32_t start_color;
  uint32_t end_color;
  int32_t has_fill;
  int32_t has_start_miter;
  uint16_t start_miter;
  int32_t has_end_miter;
  uint16_t end_miter;
} radicle_swf_morph_line_style;

typedef struct radicle_swf_bitmap_info {
  uint16_t character_id;
  uint8_t format;               /* 3 colour map, 4 RGB15, 5 RGB32 */
  uint16_t width;
  uint16_t height;
  int32_t has_color_table;
  uint8_t color_table_size;
  size_t data_size;
} radicle_swf_bitmap_info;

typedef struct radicle_swf_jpeg_info {
  uint16_t character_id;
  uint16_t deblock;             /* 8.8 fixed, only DefineBitsJPEG4 carries one */
  size_t image_size;
  int32_t has_alpha;
  size_t alpha_size;
} radicle_swf_jpeg_info;

typedef struct radicle_swf_jpeg_tables_info {
  size_t size;
} radicle_swf_jpeg_tables_info;

/* `parsed` is 0 when the block did not parse; the other fields must still be valid then. */
typedef struct radicle_swf_abc_info {
  const char* name;            /* must be the DoABC2 (82) tag-header name; a DoABC (72) block has none, so it is "" */
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

/* Invariant: name must be non-null; *out_text is malloc'd with a trailing NUL, release with radicle_free_bytes. */
RADICLE_API radicle_status radicle_swf_inspect_text(const radicle_swf* swf, const char* name,
                                                    uint8_t** out_text, size_t* out_size);
/* Invariant: index is a flat radicle_swf_tag_at index of a DoAction or DoInitAction tag; *out_text is malloc'd with a trailing NUL, release with radicle_free_bytes. */
RADICLE_API radicle_status radicle_swf_avm1_disassemble(const radicle_swf* swf, size_t index,
                                                        uint8_t** out_text, size_t* out_size);

/* SymbolClass, ExportAssets and FrameLabel records; ordering must stay file order. */
RADICLE_API size_t radicle_swf_symbol_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_symbol_at(const radicle_swf* swf, size_t index, radicle_swf_symbol* out_symbol);
RADICLE_API size_t radicle_swf_export_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_export_at(const radicle_swf* swf, size_t index, radicle_swf_symbol* out_export);
RADICLE_API size_t radicle_swf_frame_label_count(const radicle_swf* swf);
RADICLE_API radicle_status radicle_swf_frame_label_at(const radicle_swf* swf, size_t index,
                                                      radicle_swf_frame_label* out_label);

/* The tag index must be one `radicle_swf_tag_at` accepts. */
RADICLE_API radicle_status radicle_swf_shape_at(const radicle_swf* swf, size_t index, radicle_swf_shape* out_shape);
RADICLE_API radicle_status radicle_swf_shape_fill_at(const radicle_swf* swf, size_t index, size_t fill,
                                                     radicle_swf_fill_style* out_style);
RADICLE_API radicle_status radicle_swf_shape_fill_stop_at(const radicle_swf* swf, size_t index, size_t fill,
                                                          size_t stop, radicle_swf_gradient_stop* out_stop);
RADICLE_API radicle_status radicle_swf_shape_line_at(const radicle_swf* swf, size_t index, size_t line,
                                                     radicle_swf_line_style* out_style);
RADICLE_API radicle_status radicle_swf_shape_record_at(const radicle_swf* swf, size_t index, size_t record,
                                                       radicle_swf_shape_record* out_record);
RADICLE_API radicle_status radicle_swf_shape_line_fill_at(const radicle_swf* swf, size_t index, size_t line,
                                                          radicle_swf_fill_style* out_fill);
RADICLE_API radicle_status radicle_swf_shape_line_fill_stop_at(const radicle_swf* swf, size_t index, size_t line,
                                                               size_t stop, radicle_swf_gradient_stop* out_stop);
RADICLE_API radicle_status radicle_swf_morph_at(const radicle_swf* swf, size_t index,
                                                radicle_swf_morph_shape* out_morph);
RADICLE_API radicle_status radicle_swf_morph_start_record_at(const radicle_swf* swf, size_t index,
                                                             size_t record,
                                                             radicle_swf_shape_record* out_record);
RADICLE_API radicle_status radicle_swf_morph_end_record_at(const radicle_swf* swf, size_t index,
                                                           size_t record,
                                                           radicle_swf_shape_record* out_record);
RADICLE_API radicle_status radicle_swf_morph_frame_count(const radicle_swf* swf, size_t index,
                                                         double ratio, size_t* out_count);
RADICLE_API radicle_status radicle_swf_morph_frame_record_at(const radicle_swf* swf, size_t index,
                                                             double ratio, size_t record,
                                                             radicle_swf_shape_record* out_record);
RADICLE_API radicle_status radicle_swf_morph_fill_at(const radicle_swf* swf, size_t index, size_t fill,
                                                     radicle_swf_morph_fill_style* out_fill);
RADICLE_API radicle_status radicle_swf_morph_fill_stop_at(const radicle_swf* swf, size_t index,
                                                          size_t fill, size_t stop,
                                                          radicle_swf_morph_gradient_stop* out_stop);
RADICLE_API radicle_status radicle_swf_morph_line_at(const radicle_swf* swf, size_t index, size_t line,
                                                     radicle_swf_morph_line_style* out_line);
RADICLE_API radicle_status radicle_swf_morph_line_fill_at(const radicle_swf* swf, size_t index,
                                                          size_t line,
                                                          radicle_swf_morph_fill_style* out_fill);
RADICLE_API radicle_status radicle_swf_morph_line_fill_stop_at(const radicle_swf* swf, size_t index,
                                                               size_t line, size_t stop,
                                                               radicle_swf_morph_gradient_stop* out_stop);
RADICLE_API radicle_status radicle_swf_bitmap_at(const radicle_swf* swf, size_t index,
                                                 radicle_swf_bitmap_info* out_bitmap);
RADICLE_API radicle_status radicle_swf_jpeg_at(const radicle_swf* swf, size_t index,
                                               radicle_swf_jpeg_info* out_jpeg);
RADICLE_API radicle_status radicle_swf_jpeg_tables_at(const radicle_swf* swf, size_t index,
                                                      radicle_swf_jpeg_tables_info* out_tables);

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

/* The whole-block text form must be NUL-terminated one byte past *out_size. */
RADICLE_API radicle_status radicle_swf_abc_text(const radicle_swf* swf, size_t abc, uint8_t** out_text,
                                                size_t* out_size);

/* Assembles text produced by radicle_swf_abc_text back into ABC bytes; the malloc'd buffer must carry
   a trailing NUL and the caller must release it with radicle_free_bytes. */
RADICLE_API radicle_status radicle_swf_abc_assemble(const radicle_swf* swf, size_t abc, const uint8_t* text,
                                                    size_t size, uint8_t** out_bytes, size_t* out_size);

/* Assembles the text and must rewrite that DoABC/DoABC2 body all-or-nothing. */
RADICLE_API radicle_status radicle_swf_abc_apply_text(radicle_swf* swf, size_t abc, const uint8_t* text,
                                                      size_t size);

/* Requires a branch-free instruction text and an instruction boundary; a refusal must leave the handle unchanged. */
RADICLE_API radicle_status radicle_swf_abc_insert_instruction(radicle_swf* swf, size_t abc, uint32_t body,
                                                              size_t offset, const uint8_t* text, size_t size);
/* Requires the same arguments as the insert, and replaces the instruction that starts at offset. */
RADICLE_API radicle_status radicle_swf_abc_replace_instruction(radicle_swf* swf, size_t abc, uint32_t body,
                                                               size_t offset, const uint8_t* text, size_t size);

typedef struct radicle_swf_frames radicle_swf_frames;

typedef struct radicle_swf_transform {
  double scale_x;
  double rotate_skew0;
  double rotate_skew1;
  double scale_y;
  double translate_x;          /* must be in points */
  double translate_y;          /* must be in points */
} radicle_swf_transform;

typedef struct radicle_swf_bounds {
  double x;
  double y;
  double width;
  double height;
} radicle_swf_bounds;

typedef struct radicle_swf_frames_instance {
  uint32_t instance_id;        /* must stay with an instance across a move and must change when a placement carries a character */
  uint32_t timeline_frame;     /* must be the 0-based frame of timeline_id this expansion used */
  uint16_t depth;
  uint16_t character_id;
  uint16_t timeline_id;        /* must be 0 for the root timeline, else the DefineSprite character that owns the instance */
  uint16_t clip_depth;         /* must be read only when has_clip_depth is 1 */
  uint8_t is_sprite;
  uint8_t has_bounds;          /* must be 0 for a character without shape bounds */
  uint8_t has_clip_depth;
  radicle_swf_transform transform;        /* must be local to timeline_id */
  radicle_swf_transform world_transform;  /* must be composed from the root */
  radicle_swf_bounds bounds;              /* must be in world points */
  const char* name;            /* must never be null; "" when the instance is unnamed */
  const char* path;            /* must never be null; instance path from the root, e.g. "Art/Cell" */
} radicle_swf_frames_instance;

/* A per-frame display list; every string it hands back must stay valid until radicle_swf_frames_destroy, so
   the handle may outlive the movie. Every accessor on it must refuse once a plan has moved the movie's
   revision, because the list must keep reporting the frames it copied at open. */
RADICLE_API radicle_status radicle_swf_frames_open(const radicle_swf* swf, radicle_swf_frames** out_frames);
RADICLE_API void radicle_swf_frames_destroy(radicle_swf_frames* frames);
RADICLE_API size_t radicle_swf_frames_timeline_count(const radicle_swf_frames* frames);
RADICLE_API radicle_status radicle_swf_frames_timeline_at(const radicle_swf_frames* frames, size_t index,
                                                          uint16_t* out_character_id, size_t* out_frame_count);
RADICLE_API radicle_status radicle_swf_frames_instance_count(const radicle_swf_frames* frames, size_t timeline,
                                                            size_t frame, size_t* out_count);
RADICLE_API radicle_status radicle_swf_frames_instance_at(const radicle_swf_frames* frames, size_t timeline,
                                                         size_t frame, size_t index,
                                                         radicle_swf_frames_instance* out_instance);

/* One tag body a plan replaces: `tag_index` must address a top-level tag that radicle_swf_tag_at reports,
   because a sprite body must be edited by replacing its DefineSprite tag instead. */
typedef struct radicle_swf_tag_replacement {
  size_t tag_index;
  const uint8_t* payload;      /* must decode as the tag's own code, or the whole plan is refused */
  size_t size;
} radicle_swf_tag_replacement;

/* One tag a plan inserts: `before_tag_index` must address a top-level tag, and naming the terminal End
   tag must insert the new tag ahead of it. */
typedef struct radicle_swf_tag_insertion {
  size_t before_tag_index;
  uint16_t code;               /* must lie in 1..0x3FF; the End code 0 must be refused */
  const uint8_t* payload;
  size_t size;
} radicle_swf_tag_insertion;

/* Constraint: a removal names one tag of the stream the plan was built against; the End tag must be refused. */
typedef struct radicle_swf_tag_removal {
  size_t tag_index;
} radicle_swf_tag_removal;

/* A plan must apply all-or-nothing: a refusal must leave every tag body, the revision and the bytes
   that radicle_swf_write_bytes must return exactly as they were before the call. */
typedef struct radicle_swf_tag_plan {
  const radicle_swf_tag_replacement* replacements;
  size_t replacement_count;
  const radicle_swf_tag_insertion* insertions;
  size_t insertion_count;
  const radicle_swf_tag_removal* removals;
  size_t removal_count;
} radicle_swf_tag_plan;

typedef struct radicle_swf_plan_result {
  uint8_t changed;             /* must be 0 when the plan matched the bytes in force, so the revision must not move */
  uint64_t revision;           /* must be the handle's revision after the call, advanced only when changed is 1 */
} radicle_swf_plan_result;

/* Applies a plan and must rebuild every cached view before any getter sees the new tags. */
RADICLE_API radicle_status radicle_swf_apply_plan(radicle_swf* swf, const radicle_swf_tag_plan* plan,
                                                  radicle_swf_plan_result* out_result);

typedef struct radicle_swf_glyph {
  uint8_t fill_bits;           /* NumFillBits of the glyph SHAPE header, 0..15 */
  uint8_t line_bits;           /* NumLineBits of the glyph SHAPE header, 0..15 */
  size_t record_count;
} radicle_swf_glyph;

/* The tag index must be one radicle_swf_tag_at accepts and name a decoded DefineFont, DefineFont2 or DefineFont3. */
RADICLE_API radicle_status radicle_swf_font_glyph_count(const radicle_swf* swf, size_t index, size_t* out_count);
RADICLE_API radicle_status radicle_swf_font_glyph_at(const radicle_swf* swf, size_t index, size_t glyph,
                                                     radicle_swf_glyph* out_glyph);
RADICLE_API radicle_status radicle_swf_font_glyph_record_at(const radicle_swf* swf, size_t index, size_t glyph,
                                                            size_t record, radicle_swf_shape_record* out_record);
/* Requires records to hold record_count entries; the revision advances only when the glyph shape changed. */
RADICLE_API radicle_status radicle_swf_font_replace_glyph(radicle_swf* swf, size_t index, size_t glyph,
                                                          uint8_t fill_bits, uint8_t line_bits,
                                                          const radicle_swf_shape_record* records,
                                                          size_t record_count,
                                                          radicle_swf_plan_result* out_result);
/* The revision must start at 0, advance only for a plan that changed the movie, and never move for a refusal. */
RADICLE_API uint64_t radicle_swf_revision(const radicle_swf* swf);
/* Returns malloc'd bytes the caller must release with radicle_free_bytes; an unedited handle must
   reproduce the bytes it was opened from, and a no-op plan must leave that output unchanged. */
RADICLE_API radicle_status radicle_swf_write_bytes(const radicle_swf* swf, uint8_t** out_bytes,
                                                   size_t* out_size);

typedef enum radicle_swf_character_kind {
  RADICLE_SWF_CHARACTER_SHAPE = 0,
  RADICLE_SWF_CHARACTER_MORPH = 1,
  RADICLE_SWF_CHARACTER_BITMAP = 2,
  RADICLE_SWF_CHARACTER_TEXT = 3,
  RADICLE_SWF_CHARACTER_EDIT_TEXT = 4,
  RADICLE_SWF_CHARACTER_FONT = 5,
  RADICLE_SWF_CHARACTER_SPRITE = 6,
  RADICLE_SWF_CHARACTER_BUTTON = 7,
  RADICLE_SWF_CHARACTER_SOUND = 8,
  RADICLE_SWF_CHARACTER_VIDEO = 9,
  RADICLE_SWF_CHARACTER_IMPORTED = 10,
} radicle_swf_character_kind;

typedef struct radicle_swf_character {
  uint16_t character_id;
  uint8_t kind;                /* radicle_swf_character_kind */
  uint8_t has_bounds;
  uint8_t has_bitmap_size;
  uint8_t reserved;
  size_t tag_index;            /* flat index radicle_swf_tag_at reports for the defining tag; the ImportAssets tag when imported */
  radicle_swf_rect bounds;     /* twips; zero unless has_bounds is 1 */
  uint16_t bitmap_width;       /* pixels; zero unless has_bitmap_size is 1 */
  uint16_t bitmap_height;
  const char* name;            /* SymbolClass or ExportAssets name; "" when unnamed, never null */
  const char* imported_from;   /* ImportAssets source movie; "" unless the character is imported */
  const char* imported_name;   /* imported symbol name; "" unless the character is imported */
} radicle_swf_character;

/* Must build once per revision and report every defining tag once, sorted by character_id. */
RADICLE_API radicle_status radicle_swf_character_count(const radicle_swf* swf, size_t* out_count);
/* Must refuse an out-of-range index with INVALID_ARGUMENT. */
RADICLE_API radicle_status radicle_swf_character_at(const radicle_swf* swf, size_t index,
                                                    radicle_swf_character* out_character);
/* Must refuse an unknown id with NOT_FOUND; a refused model must fail every character getter. */
RADICLE_API radicle_status radicle_swf_character_by_id(const radicle_swf* swf, uint16_t character_id,
                                                       radicle_swf_character* out_character);
/* Must refuse a character that is not a decoded shape with NOT_FOUND. Returns malloc'd UTF-8 SVG text the caller must release with radicle_free_bytes; the buffer is NUL-terminated one byte past *out_size. */
RADICLE_API radicle_status radicle_swf_shape_svg(const radicle_swf* swf, uint16_t character_id,
                                                 uint8_t** out_text, size_t* out_size);

typedef struct radicle_swf_history radicle_swf_history;

/* Must start from the bytes the handle currently serializes, so an unedited handle starts at its opened bytes. */
RADICLE_API radicle_status radicle_swf_history_create(const radicle_swf* swf,
                                                      radicle_swf_history** out_history);
RADICLE_API void radicle_swf_history_destroy(radicle_swf_history* history);
/* Must append one command only when the plan changes the movie; a refusal must move no history state. */
RADICLE_API radicle_status radicle_swf_history_apply(radicle_swf_history* history,
                                                     const radicle_swf_tag_plan* plan,
                                                     radicle_swf_plan_result* out_result);
/* Must reopen the original bytes and replay the remaining prefix, so undo equals the forward sequence. */
RADICLE_API radicle_status radicle_swf_history_undo(radicle_swf_history* history,
                                                    radicle_swf_plan_result* out_result);
RADICLE_API radicle_status radicle_swf_history_redo(radicle_swf_history* history,
                                                    radicle_swf_plan_result* out_result);
/* Must advance only when a call changes the movie, and never for a refusal. */
RADICLE_API uint64_t radicle_swf_history_revision(const radicle_swf_history* history);
RADICLE_API radicle_status radicle_swf_history_undo_count(const radicle_swf_history* history,
                                                          size_t* out_count);
RADICLE_API radicle_status radicle_swf_history_redo_count(const radicle_swf_history* history,
                                                          size_t* out_count);
RADICLE_API radicle_status radicle_swf_history_write_bytes(const radicle_swf_history* history,
                                                           uint8_t** out_bytes, size_t* out_size);

#ifdef __cplusplus
}
#endif
