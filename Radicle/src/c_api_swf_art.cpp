#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"

#include "fo4/c_api_swf.h"
#include "fo4/swf/swf_glyph_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace {

const fo4::swf::SwfTag* decoded_tag(const radicle_swf* swf, std::size_t index) {
  if (swf == nullptr || index >= swf->tags.size()) {
    return nullptr;
  }
  return swf->tags[index].tag;
}

const std::vector<fo4::swf::SwfShapeRecord>& morph_frame(const radicle_swf* swf, std::size_t index,
                                                         double ratio) {
  if (swf->morph_frames.size() != swf->tags.size()) {
    swf->morph_frames.resize(swf->tags.size());
  }
  auto& frame = swf->morph_frames[index];
  const double clamped = std::clamp(ratio, 0.0, 1.0);
  if (!frame.filled || frame.ratio != clamped) {
    frame.commands =
        fo4::swf::interpolate_morph_commands(*swf->tags[index].tag->morph_shape, clamped);
    frame.ratio = clamped;
    frame.filled = true;
  }
  return frame.commands;
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

void fill_matrix(const fo4::swf::SwfMatrix& matrix, std::int32_t (&out)[6]) {
  out[0] = matrix.scale_x;
  out[1] = matrix.scale_y;
  out[2] = matrix.rotate_skew0;
  out[3] = matrix.rotate_skew1;
  out[4] = matrix.translate_x;
  out[5] = matrix.translate_y;
}

void fill_gradient_stop(const fo4::swf::SwfGradientStop& stop, radicle_swf_gradient_stop& out) {
  out = {};
  out.ratio = stop.ratio;
  out.color = stop.color;
}

void fill_gradient_stop(const fo4::swf::SwfMorphGradientStop& stop,
                        radicle_swf_morph_gradient_stop& out) {
  out = {};
  out.start_ratio = stop.start_ratio;
  out.end_ratio = stop.end_ratio;
  out.start_color = stop.start_color;
  out.end_color = stop.end_color;
}

void fill_morph_fill_style(const fo4::swf::SwfMorphFillStyle& style,
                           radicle_swf_morph_fill_style& out) {
  out = {};
  out.kind = style.type;
  out.start_color = style.start_color;
  out.end_color = style.end_color;
  out.bitmap_id = style.bitmap_id;
  out.spread = style.spread;
  out.interpolation = style.interpolation;
  out.stop_count = style.stops.size();
  out.has_start_focal = style.start_focal_point.has_value() ? 1 : 0;
  out.start_focal = style.start_focal_point.value_or(0);
  out.has_end_focal = style.end_focal_point.has_value() ? 1 : 0;
  out.end_focal = style.end_focal_point.value_or(0);
  if (style.start_matrix) {
    out.has_start_matrix = 1;
    fill_matrix(*style.start_matrix, out.start_matrix);
  }
  if (style.end_matrix) {
    out.has_end_matrix = 1;
    fill_matrix(*style.end_matrix, out.end_matrix);
  }
}

void fill_shape_record(const fo4::swf::SwfShapeRecord& source, radicle_swf_shape_record& out) {
  out = {};
  switch (source.kind) {
    case fo4::swf::SwfShapeRecordKind::move:
      out.kind = RADICLE_SWF_RECORD_MOVE;
      break;
    case fo4::swf::SwfShapeRecordKind::style_change:
      out.kind = RADICLE_SWF_RECORD_STYLE_CHANGE;
      break;
    case fo4::swf::SwfShapeRecordKind::straight_edge:
      out.kind = RADICLE_SWF_RECORD_STRAIGHT_EDGE;
      break;
    case fo4::swf::SwfShapeRecordKind::curved_edge:
      out.kind = RADICLE_SWF_RECORD_CURVED_EDGE;
      break;
  }
  out.x = source.x;
  out.y = source.y;
  out.control_x = source.control_x;
  out.control_y = source.control_y;
  out.fill0 = source.fill0;
  out.fill1 = source.fill1;
  out.line = source.line;
}

radicle_status morph_record_at(const radicle_swf* swf, size_t index, size_t record, bool end_stream,
                               radicle_swf_shape_record* out_record) {
  clear_error();
  if (out_record) *out_record = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_record and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& records = end_stream ? tag->morph_shape->end_records : tag->morph_shape->start_records;
  if (record >= records.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph record index is out of range");
  }
  fill_shape_record(records[record], *out_record);
  return RADICLE_STATUS_OK;
}

bool to_shape_record(const radicle_swf_shape_record& source, fo4::swf::SwfShapeRecord& out) {
  if (static_cast<int>(source.kind) < 0 || source.kind > RADICLE_SWF_RECORD_CURVED_EDGE) {
    return false;
  }
  out = {};
  out.kind = static_cast<fo4::swf::SwfShapeRecordKind>(source.kind);
  out.x = source.x;
  out.y = source.y;
  out.control_x = source.control_x;
  out.control_y = source.control_y;
  out.fill0 = source.fill0;
  out.fill1 = source.fill1;
  out.line = source.line;
  return true;
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
  out_shape->record_count = shape.records.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_shape_record_at(const radicle_swf* swf, size_t index, size_t record,
                                          radicle_swf_shape_record* out_record) {
  clear_error();
  if (out_record) *out_record = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_record and a tag index are required");
  }
  if (!tag->shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded shape");
  }
  if (record >= tag->shape->records.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the shape record index is out of range");
  }
  fill_shape_record(tag->shape->records[record], *out_record);
  return RADICLE_STATUS_OK;
}


radicle_status radicle_swf_morph_start_record_at(const radicle_swf* swf, size_t index, size_t record,
                                                 radicle_swf_shape_record* out_record) {
  return morph_record_at(swf, index, record, false, out_record);
}

radicle_status radicle_swf_morph_end_record_at(const radicle_swf* swf, size_t index, size_t record,
                                               radicle_swf_shape_record* out_record) {
  return morph_record_at(swf, index, record, true, out_record);
}

radicle_status radicle_swf_morph_frame_count(const radicle_swf* swf, size_t index, double ratio,
                                             size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_count and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  *out_count = morph_frame(swf, index, ratio).size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_frame_record_at(const radicle_swf* swf, size_t index, double ratio,
                                                 size_t record, radicle_swf_shape_record* out_record) {
  clear_error();
  if (out_record) *out_record = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_record and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& commands = morph_frame(swf, index, ratio);
  if (record >= commands.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph frame record index is out of range");
  }
  fill_shape_record(commands[record], *out_record);
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
  fill_gradient_stop(stops[stop], *out_stop);
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

radicle_status radicle_swf_shape_line_fill_at(const radicle_swf* swf, size_t index, size_t line,
                                              radicle_swf_fill_style* out_fill) {
  clear_error();
  if (out_fill) *out_fill = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_fill == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_fill and a tag index are required");
  }
  if (!tag->shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded shape");
  }
  if (line >= tag->shape->line_styles.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the line style index is out of range");
  }
  if (!tag->shape->line_styles[line].fill) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the line style carries no fill");
  }
  fill_fill_style(*tag->shape->line_styles[line].fill, *out_fill);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_shape_line_fill_stop_at(const radicle_swf* swf, size_t index, size_t line,
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
  if (line >= tag->shape->line_styles.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the line style index is out of range");
  }
  if (!tag->shape->line_styles[line].fill) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the line style carries no fill");
  }
  const auto& stops = tag->shape->line_styles[line].fill->stops;
  if (stop >= stops.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the gradient stop index is out of range");
  }
  fill_gradient_stop(stops[stop], *out_stop);
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
  out_morph->start_record_count = morph.start_records.size();
  out_morph->end_record_count = morph.end_records.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_fill_at(const radicle_swf* swf, size_t index, size_t fill,
                                         radicle_swf_morph_fill_style* out_fill) {
  clear_error();
  if (out_fill) *out_fill = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_fill == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_fill and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& fills = tag->morph_shape->fill_styles;
  if (fill >= fills.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph fill style index is out of range");
  }
  fill_morph_fill_style(fills[fill], *out_fill);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_fill_stop_at(const radicle_swf* swf, size_t index, size_t fill,
                                              size_t stop,
                                              radicle_swf_morph_gradient_stop* out_stop) {
  clear_error();
  if (out_stop) *out_stop = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_stop == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_stop and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& fills = tag->morph_shape->fill_styles;
  if (fill >= fills.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph fill style index is out of range");
  }
  const auto& stops = fills[fill].stops;
  if (stop >= stops.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the gradient stop index is out of range");
  }
  fill_gradient_stop(stops[stop], *out_stop);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_line_at(const radicle_swf* swf, size_t index, size_t line,
                                         radicle_swf_morph_line_style* out_line) {
  clear_error();
  if (out_line) *out_line = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_line == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_line and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& lines = tag->morph_shape->line_styles;
  if (line >= lines.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph line style index is out of range");
  }
  const auto& style = lines[line];
  out_line->start_width = style.start_width;
  out_line->end_width = style.end_width;
  out_line->flags = style.flags;
  out_line->start_color = style.start_color;
  out_line->end_color = style.end_color;
  out_line->has_fill = style.fill.has_value() ? 1 : 0;
  out_line->has_start_miter = style.start_miter_limit.has_value() ? 1 : 0;
  out_line->start_miter = style.start_miter_limit.value_or(0);
  out_line->has_end_miter = style.end_miter_limit.has_value() ? 1 : 0;
  out_line->end_miter = style.end_miter_limit.value_or(0);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_line_fill_at(const radicle_swf* swf, size_t index, size_t line,
                                              radicle_swf_morph_fill_style* out_fill) {
  clear_error();
  if (out_fill) *out_fill = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_fill == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_fill and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& lines = tag->morph_shape->line_styles;
  if (line >= lines.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph line style index is out of range");
  }
  if (!lines[line].fill) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the morph line style carries no fill");
  }
  fill_morph_fill_style(*lines[line].fill, *out_fill);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_morph_line_fill_stop_at(const radicle_swf* swf, size_t index, size_t line,
                                                   size_t stop,
                                                   radicle_swf_morph_gradient_stop* out_stop) {
  clear_error();
  if (out_stop) *out_stop = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_stop == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_stop and a tag index are required");
  }
  if (!tag->morph_shape) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded morph shape");
  }
  const auto& lines = tag->morph_shape->line_styles;
  if (line >= lines.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the morph line style index is out of range");
  }
  if (!lines[line].fill) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the morph line style carries no fill");
  }
  const auto& stops = lines[line].fill->stops;
  if (stop >= stops.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the gradient stop index is out of range");
  }
  fill_gradient_stop(stops[stop], *out_stop);
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

radicle_status radicle_swf_font_glyph_count(const radicle_swf* swf, size_t index, size_t* out_count) {
  clear_error();
  if (out_count) *out_count = 0;
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_count and a tag index are required");
  }
  if (!tag->font) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded font");
  }
  *out_count = tag->font->glyphs.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_font_glyph_at(const radicle_swf* swf, size_t index, size_t glyph,
                                         radicle_swf_glyph* out_glyph) {
  clear_error();
  if (out_glyph) *out_glyph = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_glyph == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_glyph and a tag index are required");
  }
  if (!tag->font) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded font");
  }
  if (glyph >= tag->font->glyphs.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the glyph index is out of range");
  }
  const auto& shape = tag->font->glyphs[glyph];
  out_glyph->fill_bits = shape.fill_bits;
  out_glyph->line_bits = shape.line_bits;
  out_glyph->record_count = shape.records.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_font_glyph_record_at(const radicle_swf* swf, size_t index, size_t glyph,
                                                size_t record, radicle_swf_shape_record* out_record) {
  clear_error();
  if (out_record) *out_record = {};
  const auto* tag = decoded_tag(swf, index);
  if (tag == nullptr || out_record == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_record and a tag index are required");
  }
  if (!tag->font) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the tag carries no decoded font");
  }
  if (glyph >= tag->font->glyphs.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the glyph index is out of range");
  }
  const auto& records = tag->font->glyphs[glyph].records;
  if (record >= records.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the shape record index is out of range");
  }
  fill_shape_record(records[record], *out_record);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_font_replace_glyph(radicle_swf* swf, size_t index, size_t glyph,
                                              uint8_t fill_bits, uint8_t line_bits,
                                              const radicle_swf_shape_record* records,
                                              size_t record_count,
                                              radicle_swf_plan_result* out_result) {
  clear_error();
  if (out_result) {
    out_result->changed = 0;
    out_result->revision = swf == nullptr || !swf->revision ? 0 : swf->revision->revision;
  }
  if (swf == nullptr || out_result == nullptr || (records == nullptr && record_count != 0)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf, out_result and the record array are required");
  }
  return guarded("unknown exception while replacing a glyph shape", [&] {
    const auto* tag = decoded_tag(swf, index);
    if (tag == nullptr) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a tag index is required");
    }
    fo4::swf::SwfGlyph shape;
    shape.fill_bits = fill_bits;
    shape.line_bits = line_bits;
    shape.records.resize(record_count);
    for (size_t position = 0; position < record_count; ++position) {
      if (!to_shape_record(records[position], shape.records[position])) {
        return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a shape record kind is out of range");
      }
    }
    std::vector<std::byte> payload;
    std::string message;
    if (!fo4::swf::replace_glyph_payload(*tag, glyph, shape, payload, message)) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, message.c_str());
    }
    const radicle_swf_tag_replacement replacement{
        index, reinterpret_cast<const uint8_t*>(payload.data()), payload.size()};
    const radicle_swf_tag_plan plan{&replacement, 1, nullptr, 0, nullptr, 0};
    return radicle_swf_apply_plan(swf, &plan, out_result);
  });
}

}  // extern "C"
