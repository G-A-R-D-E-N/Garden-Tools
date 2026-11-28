#pragma once

#include "fo4/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RADICLE_RUNTIME_HANDLES
#define RADICLE_RUNTIME_HANDLES
typedef struct radicle_movie radicle_movie;
typedef struct radicle_runtime_snapshot radicle_runtime_snapshot;
#endif
/* An art table must own every array it hands out, so the pointers stay valid until radicle_art_destroy. */
typedef struct radicle_art radicle_art;

typedef struct radicle_art_point {
  double x;
  double y;
} radicle_art_point;

/* A closed contour's last point must not repeat its first point; the renderer closes it. */
typedef struct radicle_art_contour {
  size_t point_count;
  const radicle_art_point* points;
  uint8_t closed;
} radicle_art_contour;

typedef struct radicle_art_gradient_stop {
  uint8_t ratio;
  uint32_t color;
} radicle_art_gradient_stop;

/* kind must be the SWF fill type; the contours are filled with the nonzero rule. */
typedef struct radicle_art_fill {
  uint8_t kind;
  uint32_t color;
  uint16_t bitmap_id;
  uint8_t spread;
  uint8_t interpolation;
  double focal_point;
  double matrix[6];
  size_t stop_count;
  const radicle_art_gradient_stop* stops;
  size_t contour_count;
  const radicle_art_contour* contours;
  /* bitmap_missing must be 1 for a bitmap fill whose bitmap the table does not hold, such as a JPEG. */
  uint8_t bitmap_missing;
} radicle_art_fill;

/* A line's width must be in pixels, where 0 is a hairline. */
typedef struct radicle_art_line {
  double width;
  uint32_t color;
  uint8_t caps;
  uint8_t joins;
  double miter_limit;
  size_t contour_count;
  const radicle_art_contour* contours;
} radicle_art_line;

typedef struct radicle_art_shape {
  uint16_t character_id;
  size_t fill_count;
  const radicle_art_fill* fills;
  size_t line_count;
  const radicle_art_line* lines;
} radicle_art_shape;

typedef struct radicle_art_bitmap {
  uint16_t character_id;
  uint16_t width;
  uint16_t height;
  size_t png_size;
  const uint8_t* png;
} radicle_art_bitmap;

/* glyphs[i] must be glyph id i, a shape in em units. */
typedef struct radicle_art_font {
  uint16_t font_id;
  size_t glyph_count;
  const radicle_art_shape* glyphs;
} radicle_art_font;

/* kind must be the SWF filter id; placed filters keep the PlaceObject3 list, not the AS3 filters array. */
typedef struct radicle_snapshot_filter {
  uint8_t kind;
  uint32_t color;
  uint32_t highlight_color;
  double blur_x;
  double blur_y;
  double angle;
  double distance;
  double strength;
  uint8_t inner;
  uint8_t knockout;
  uint8_t composite_source;
  uint8_t on_top;
  uint8_t passes;
  uint8_t matrix_columns;
  uint8_t matrix_rows;
  float divisor;
  float bias;
  uint32_t default_color;
  uint8_t clamp;
  uint8_t preserve_alpha;
  size_t gradient_count;
  const uint32_t* gradient_colors;
  const uint8_t* gradient_ratios;
  size_t matrix_count;
  const float* matrix;
} radicle_snapshot_filter;

/* drawn_revision must advance on every Graphics command and clear; drawn_key must be 0 for an entry without Graphics. */
typedef struct radicle_snapshot_entry_extra {
  uint64_t drawn_key;
  uint64_t drawn_revision;
  size_t placed_filter_count;
  const radicle_snapshot_filter* placed_filters;
} radicle_snapshot_entry_extra;

/* Requires a movie; the table must be built once from the movie's file and stay independent of the movie afterwards. */
RADICLE_API radicle_status radicle_movie_art_open(radicle_movie* movie, radicle_art** out_art);
RADICLE_API void radicle_art_destroy(radicle_art* art);
RADICLE_API size_t radicle_art_shape_count(const radicle_art* art);
RADICLE_API radicle_status radicle_art_shape_at(const radicle_art* art, size_t index,
                                                radicle_art_shape* out_shape);
RADICLE_API size_t radicle_art_bitmap_count(const radicle_art* art);
RADICLE_API radicle_status radicle_art_bitmap_at(const radicle_art* art, size_t index,
                                                 radicle_art_bitmap* out_bitmap);
RADICLE_API size_t radicle_art_font_count(const radicle_art* art);
RADICLE_API radicle_status radicle_art_font_at(const radicle_art* art, size_t index,
                                               radicle_art_font* out_font);

/* Requires an index below the entry count; the arrays stay valid until radicle_runtime_snapshot_destroy. */
RADICLE_API radicle_status radicle_runtime_snapshot_entry_extra_at(
    const radicle_runtime_snapshot* snapshot, size_t index, radicle_snapshot_entry_extra* out_extra);
/* Requires one thread per snapshot, since the first call for an entry writes that entry's cache. */
/* Requires an index below the entry count; returns NOT_FOUND for an entry without Graphics, and the shape stays valid until radicle_runtime_snapshot_destroy. */
RADICLE_API radicle_status radicle_runtime_snapshot_drawn_at(const radicle_runtime_snapshot* snapshot,
                                                             size_t index, radicle_art_shape* out_shape);

#ifdef __cplusplus
}
#endif
