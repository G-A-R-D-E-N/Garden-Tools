#include "fo4/c_api_swf.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_swf_internal.hpp"
#include "fo4/swf/swf_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

struct radicle_swf_layout {
  fo4::swf::SwfLayout layout;
  std::shared_ptr<radicle_swf_detail::revision_state> revision;
  std::uint64_t built_revision{0};
};

namespace {

// Invariant: every accessor must fail once the movie revision this layout copied has moved.
bool stale(const radicle_swf_layout* layout) {
  return layout != nullptr && layout->revision &&
         layout->revision->revision != layout->built_revision;
}

radicle_status fail_stale() {
  return fail(RADICLE_STATUS_INVALID_ARGUMENT,
              "this layout was built before the movie was edited; reopen it");
}

void fill_bounds(const fo4::swf::SwfBounds& source, radicle_swf_bounds& out) {
  out.x = source.x;
  out.y = source.y;
  out.width = source.width;
  out.height = source.height;
}

void fill_transform(const fo4::swf::SwfTransform& source, radicle_swf_transform& out) {
  out.scale_x = source.scale_x;
  out.rotate_skew0 = source.rotate_skew0;
  out.rotate_skew1 = source.rotate_skew1;
  out.scale_y = source.scale_y;
  out.translate_x = source.translate_x;
  out.translate_y = source.translate_y;
}

void fill_text(const fo4::swf::SwfLayoutText& source, radicle_swf_layout_text& out) {
  if (source.font_id) {
    out.has_font_id = 1;
    out.font_id = *source.font_id;
  }
  if (source.font_height) {
    out.has_font_height = 1;
    out.font_height = *source.font_height;
  }
  if (source.color) {
    out.has_color = 1;
    out.color = *source.color;
  }
  if (source.has_layout) {
    out.has_layout = 1;
    out.align = source.align;
    out.left_margin = source.left_margin;
    out.right_margin = source.right_margin;
    out.indent = source.indent;
    out.leading = source.leading;
  }
  out.multiline = source.multiline ? 1 : 0;
  out.word_wrap = source.word_wrap ? 1 : 0;
  out.auto_size = source.auto_size ? 1 : 0;
  out.html = source.html ? 1 : 0;
  out.border = source.border ? 1 : 0;
  out.variable = source.variable.c_str();
  out.text = source.text.c_str();
  out.has_font_class = source.has_font_class ? 1 : 0;
  out.font_class = source.font_class.c_str();
}

void fill_row(const fo4::swf::SwfLayoutRow& source, radicle_swf_layout_row& out) {
  out.depth = source.depth;
  out.character_id = source.character_id;
  out.kind = static_cast<std::uint8_t>(source.kind);
  out.visible = source.visible ? 1 : 0;
  out.has_rect = source.has_rect ? 1 : 0;
  out.flags = source.flags;
  if (source.clip_depth) {
    out.has_clip_depth = 1;
    out.clip_depth = *source.clip_depth;
  }
  fill_transform(source.world_transform, out.world_transform);
  if (source.has_rect) {
    fill_bounds(source.rect, out.rect);
  }
  out.clip_frame_count = source.clip_frame_count;
  out.clip_first_frame = source.clip_first_frame;
  out.clip_last_frame = source.clip_last_frame;
  out.path = source.path.c_str();
  out.parent_path = source.parent_path.c_str();
  out.text.variable = "";
  out.text.text = "";
  if (source.text) {
    out.has_text = 1;
    fill_text(*source.text, out.text);
  }
}

}  // namespace

extern "C" {

radicle_status radicle_swf_layout_open(const radicle_swf* swf, radicle_swf_layout** out_layout) {
  clear_error();
  if (out_layout) *out_layout = nullptr;
  if (swf == nullptr || out_layout == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "swf and out_layout are required");
  }
  return guarded("unknown exception while building the layout", [&] {
    std::unique_ptr<radicle_swf_layout> built(new radicle_swf_layout());
    std::string message;
    if (!fo4::swf::build_layout(swf->file, built->layout, message)) {
      return fail(RADICLE_STATUS_PARSE_ERROR,
                  message.empty() ? "the layout did not build" : message.c_str());
    }
    built->revision = swf->revision;
    built->built_revision = swf->revision ? swf->revision->revision : 0;
    *out_layout = built.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_swf_layout_destroy(radicle_swf_layout* layout) { delete layout; }

radicle_status radicle_swf_layout_get_info(const radicle_swf_layout* layout,
                                           radicle_swf_layout_info* out_info) {
  clear_error();
  if (out_info) *out_info = {};
  if (stale(layout)) return fail_stale();
  if (layout == nullptr || out_info == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "layout and out_info are required");
  }
  fill_bounds(layout->layout.stage, out_info->stage);
  out_info->frame_rate = layout->layout.frame_rate;
  out_info->scripted = layout->layout.scripted ? 1 : 0;
  out_info->has_as3 = layout->layout.has_as3 ? 1 : 0;
  out_info->legacy_moves = layout->layout.legacy_moves ? 1 : 0;
  out_info->state_count = layout->layout.states.size();
  out_info->row_count = layout->layout.rows.size();
  out_info->font_count = layout->layout.fonts.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_layout_state_at(const radicle_swf_layout* layout, size_t index,
                                           radicle_swf_layout_state* out_state) {
  clear_error();
  if (out_state) *out_state = {};
  if (stale(layout)) return fail_stale();
  if (layout == nullptr || out_state == nullptr || index >= layout->layout.states.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "layout, an in-range state index and out_state are required");
  }
  const auto& state = layout->layout.states[index];
  out_state->label = state.label.c_str();
  out_state->timeline_id = state.timeline_id;
  out_state->frame = state.frame;
  out_state->first_row = state.first_row;
  out_state->row_count = state.row_count;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_layout_row_at(const radicle_swf_layout* layout, size_t index,
                                         radicle_swf_layout_row* out_row) {
  clear_error();
  if (out_row) *out_row = {};
  if (stale(layout)) return fail_stale();
  if (layout == nullptr || out_row == nullptr || index >= layout->layout.rows.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "layout, an in-range row index and out_row are required");
  }
  fill_row(layout->layout.rows[index], *out_row);
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_layout_font_at(const radicle_swf_layout* layout, size_t index,
                                          radicle_swf_layout_font* out_font) {
  clear_error();
  if (out_font) *out_font = {};
  if (stale(layout)) return fail_stale();
  if (layout == nullptr || out_font == nullptr || index >= layout->layout.fonts.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "layout, an in-range font index and out_font are required");
  }
  const auto& font = layout->layout.fonts[index];
  out_font->font_id = font.font_id;
  out_font->version = font.version;
  out_font->has_style = font.has_style ? 1 : 0;
  out_font->bold = font.bold ? 1 : 0;
  out_font->italic = font.italic ? 1 : 0;
  out_font->has_language = font.has_language ? 1 : 0;
  out_font->language = font.language;
  out_font->has_glyph_count = font.has_glyph_count ? 1 : 0;
  out_font->glyph_count = font.glyph_count;
  out_font->has_outlines = font.has_outlines ? 1 : 0;
  out_font->has_metrics = font.has_metrics ? 1 : 0;
  out_font->ascent = font.ascent;
  out_font->descent = font.descent;
  out_font->leading = font.leading;
  out_font->units_per_em = font.units_per_em;
  out_font->name = font.name.c_str();
  out_font->has_codes = font.has_codes ? 1 : 0;
  out_font->has_font_name = font.has_font_name ? 1 : 0;
  out_font->has_font_class = font.has_font_class ? 1 : 0;
  out_font->code_count = font.codes.size();
  out_font->font_name = font.font_name.c_str();
  out_font->font_copyright = font.font_copyright.c_str();
  out_font->font_class = font.font_class.c_str();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_swf_layout_font_code_at(const radicle_swf_layout* layout, size_t font_index,
                                               size_t code_index, uint16_t* out_code) {
  clear_error();
  if (out_code) *out_code = 0;
  if (stale(layout)) return fail_stale();
  if (layout == nullptr || out_code == nullptr || font_index >= layout->layout.fonts.size() ||
      code_index >= layout->layout.fonts[font_index].codes.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "layout, an in-range font index, an in-range code index and out_code are required");
  }
  *out_code = layout->layout.fonts[font_index].codes[code_index];
  return RADICLE_STATUS_OK;
}

}  // extern "C"
