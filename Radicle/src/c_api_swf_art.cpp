#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"

#include "fo4/c_api_swf.h"

#include <cstddef>
#include <cstdint>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;

namespace {

const fo4::swf::SwfTag* decoded_tag(const radicle_swf* swf, std::size_t index) {
  if (swf == nullptr || index >= swf->tags.size()) {
    return nullptr;
  }
  return swf->tags[index].tag;
}

void fill_rect(const fo4::swf::SwfRect& rect, radicle_swf_rect& out) {
  out = {};
  out.x_min = rect.x_min;
  out.x_max = rect.x_max;
  out.y_min = rect.y_min;
  out.y_max = rect.y_max;
}

std::uint8_t shape_version(std::uint16_t code) {
  switch (code) {
    case fo4::swf::tag::kDefineShape:
      return 1;
    case fo4::swf::tag::kDefineShape2:
      return 2;
    case fo4::swf::tag::kDefineShape3:
      return 3;
    default:
      return 4;
  }
}

void fill_fill_style(const fo4::swf::SwfFillStyle& style, radicle_swf_fill_style& out) {
  out = {};
  out.kind = style.type;
  out.color = style.color;
  out.bitmap_id = style.bitmap_id;
  out.spread = style.spread;
  out.interpolation = style.interpolation;
  out.focal_point = style.focal_point.value_or(0);
  out.stop_count = style.stops.size();
  if (style.matrix) {
    out.has_matrix = 1;
    out.matrix[0] = style.matrix->scale_x;
    out.matrix[1] = style.matrix->scale_y;
    out.matrix[2] = style.matrix->rotate_skew0;
    out.matrix[3] = style.matrix->rotate_skew1;
    out.matrix[4] = style.matrix->translate_x;
    out.matrix[5] = style.matrix->translate_y;
  }
}

}  // namespace

extern "C" {

radicle_status radicle_swf_shape_at(const radicle_swf* swf, size_t index,
                                    radicle_swf_shape* out_shape) {
  clear_error();
  if (out_shape) *out_shape = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_shape == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_shape and a tag index are required");
  }
  if (!tag->shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded shape");
  }
  const auto& shape = *tag->shape;
  out_shape->character_id = shape.shape_id;
  out_shape->version = shape_version(tag->code);
  out_shape->has_edge_bounds = shape.has_edge_bounds ? 1 : 0;
  out_shape->flags = shape.flags;
  fill_rect(shape.bounds, out_shape->bounds);
  fill_rect(shape.edge_bounds, out_shape->edge_bounds);
  out_shape->fill_style_count = shape.fill_styles.size();
  out_shape->line_style_count = shape.line_styles.size();
  out_shape->edge_count = shape.edge_count;
  out_shape->curved_edge_count = shape.curved_edge_count;
  out_shape->style_change_count = shape.style_change_count;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_shape_fill_at(const radicle_swf* swf, size_t index, size_t fill,
                                         radicle_swf_fill_style* out_style) {
  clear_error();
  if (out_style) *out_style = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_style == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_style and a tag index are required");
  }
  if (!tag->shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded shape");
  }
  if (fill >= tag->shape->fill_styles.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the fill style index is out of range");
  }
  fill_fill_style(tag->shape->fill_styles[fill], *out_style);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_shape_fill_stop_at(const radicle_swf* swf, size_t index, size_t fill,
                                              size_t stop, radicle_swf_gradient_stop* out_stop) {
  clear_error();
  if (out_stop) *out_stop = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_stop == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_stop and a tag index are required");
  }
  if (!tag->shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded shape");
  }
  if (fill >= tag->shape->fill_styles.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the fill style index is out of range");
  }
  const auto& stops = tag->shape->fill_styles[fill].stops;
  if (stop >= stops.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the gradient stop index is out of range");
  }
  out_stop->ratio = stops[stop].ratio;
  out_stop->color = stops[stop].color;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_shape_line_at(const radicle_swf* swf, size_t index, size_t line,
                                         radicle_swf_line_style* out_style) {
  clear_error();
  if (out_style) *out_style = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_style == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_style and a tag index are required");
  }
  if (!tag->shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded shape");
  }
  if (line >= tag->shape->line_styles.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the line style index is out of range");
  }
  const auto& style = tag->shape->line_styles[line];
  out_style->width = style.width;
  out_style->flags = style.flags;
  out_style->color = style.color;
  out_style->has_fill = style.fill.has_value() ? 1 : 0;
  out_style->has_miter_limit = style.miter_limit.has_value() ? 1 : 0;
  out_style->miter_limit = style.miter_limit.value_or(0);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_at(const radicle_swf* swf, size_t index,
                                    radicle_swf_morph_shape* out_morph) {
  clear_error();
  if (out_morph) *out_morph = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_morph == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_morph and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& morph = *tag->morph_shape;
  out_morph->character_id = morph.character_id;
  out_morph->version = morph.version;
  out_morph->has_edge_bounds = morph.has_edge_bounds ? 1 : 0;
  out_morph->flags = morph.flags;
  fill_rect(morph.start_bounds, out_morph->start_bounds);
  fill_rect(morph.end_bounds, out_morph->end_bounds);
  fill_rect(morph.start_edge_bounds, out_morph->start_edge_bounds);
  fill_rect(morph.end_edge_bounds, out_morph->end_edge_bounds);
  out_morph->fill_style_count = morph.fill_style_count;
  out_morph->line_style_count = morph.line_style_count;
  out_morph->start_edge_count = morph.start_edge_count;
  out_morph->end_edge_count = morph.end_edge_count;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_bitmap_at(const radicle_swf* swf, size_t index,
                                     radicle_swf_bitmap_info* out_bitmap) {
  clear_error();
  if (out_bitmap) *out_bitmap = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_bitmap == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_bitmap and a tag index are required");
  }
  if (!tag->bitmap) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded bitmap");
  }
  const auto& bitmap = *tag->bitmap;
  out_bitmap->character_id = bitmap.character_id;
  out_bitmap->format = bitmap.format;
  out_bitmap->width = bitmap.width;
  out_bitmap->height = bitmap.height;
  out_bitmap->has_color_table = bitmap.color_table_size.has_value() ? 1 : 0;
  out_bitmap->color_table_size = bitmap.color_table_size.value_or(0);
  out_bitmap->data_size = bitmap.data_size;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_jpeg_at(const radicle_swf* swf, size_t index,
                                   radicle_swf_jpeg_info* out_jpeg) {
  clear_error();
  if (out_jpeg) *out_jpeg = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_jpeg == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_jpeg and a tag index are required");
  }
  if (!tag->jpeg) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded JPEG");
  }
  const auto& jpeg = *tag->jpeg;
  out_jpeg->character_id = jpeg.character_id;
  out_jpeg->deblock = jpeg.deblock;
  out_jpeg->image_size = jpeg.image_size;
  out_jpeg->has_alpha = jpeg.has_alpha ? 1 : 0;
  out_jpeg->alpha_size = jpeg.alpha_size;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_jpeg_tables_at(const radicle_swf* swf, size_t index,
                                          radicle_swf_jpeg_tables_info* out_tables) {
  clear_error();
  if (out_tables) *out_tables = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_tables == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_tables and a tag index are required");
  }
  if (!tag->jpeg_tables) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no JPEG tables");
  }
  out_tables->size = tag->jpeg_tables->size;
  return RADICLE_STATUS_OK;
}

}  // extern "C"
