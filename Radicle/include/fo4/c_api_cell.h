#pragma once

#include "fo4/c_api_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Exterior blocks require 32x32 cells and sub-blocks 8x8 cells, floored for negative coordinates. */
typedef struct radicle_exterior_cell_layout {
  int32_t cell_x;
  int32_t cell_y;
  int32_t block_x;
  int32_t block_y;
  int32_t subblock_x;
  int32_t subblock_y;
} radicle_exterior_cell_layout;

/* flags must keep bit 0 clear; it defaults to the shipped exterior value 0x0002. */
typedef struct radicle_exterior_cell_creation {
  radicle_form_key worldspace;
  int32_t x;
  int32_t y;
  const char* editor_id;
  uint16_t flags;
} radicle_exterior_cell_creation;

/* Created cells require an existing worldspace or an editor ID; the created key is returned in out_key. */
RADICLE_API radicle_status radicle_exterior_cell_layout_for(int32_t x,
                                                            int32_t y,
                                                            radicle_exterior_cell_layout* out_layout);
RADICLE_API radicle_status radicle_exterior_cell_coordinate_valid(int32_t coordinate,
                                                                  int32_t* out_valid);
RADICLE_API radicle_status radicle_plugin_create_exterior_cell(radicle_plugin* plugin,
                                                               const radicle_exterior_cell_creation* creation,
                                                               radicle_form_key* out_key);
RADICLE_API radicle_status radicle_plugin_create_interior_cell(radicle_plugin* plugin,
                                                               const char* editor_id,
                                                               radicle_form_key* out_key);
/* A search requires the worldspace plugin to be loaded; an absent cell reports RADICLE_STATUS_NOT_FOUND. */
RADICLE_API radicle_status radicle_env_find_exterior_cell(const radicle_env* env,
                                                          radicle_form_key worldspace,
                                                          int32_t x,
                                                          int32_t y,
                                                          const radicle_record** out_record);
RADICLE_API radicle_status radicle_env_find_persistent_exterior_cell(const radicle_env* env,
                                                                     radicle_form_key worldspace,
                                                                     const radicle_record** out_record);

#ifdef __cplusplus
}
#endif
