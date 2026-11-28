#include "fo4/c_api_runtime_art.h"

#include "c_api_env_internal.hpp"
#include "c_api_internal.hpp"
#include "c_api_runtime_internal.hpp"
#include "fo4/avm2/snapshot.hpp"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_art.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using radicle_c_detail::clear_error;
using radicle_c_detail::fail;
using radicle_c_env_detail::guarded;

namespace radicle_art_detail {

namespace {

std::uint8_t flag_of(bool value) { return static_cast<std::uint8_t>(value ? 1 : 0); }

void push_contour(const fo4::swf::ArtContour& contour, ShapeStore& store) {
  radicle_art_contour out{};
  out.point_count = contour.points.size();
  out.closed = flag_of(contour.closed);
  for (const fo4::swf::ArtPoint& point : contour.points) {
    store.points.push_back(radicle_art_point{point.x, point.y});
  }
  store.contours.push_back(out);
}

// Fill contours must be pushed before line contours, so one cursor per array walks them in step.
void link_pointers(ShapeStore& store) {
  std::size_t stop_at = 0;
  std::size_t contour_at = 0;
  std::size_t point_at = 0;
  const auto link_contours = [&](std::size_t count) {
    const radicle_art_contour* first = store.contours.data() + contour_at;
    for (std::size_t index = 0; index < count; ++index) {
      radicle_art_contour& contour = store.contours[contour_at++];
      contour.points = store.points.data() + point_at;
      point_at += contour.point_count;
    }
    return first;
  };
  for (radicle_art_fill& fill : store.fills) {
    fill.stops = store.stops.data() + stop_at;
    stop_at += fill.stop_count;
    fill.contours = link_contours(fill.contour_count);
  }
  for (radicle_art_line& line : store.lines) {
    line.contours = link_contours(line.contour_count);
  }
  store.shape.fills = store.fills.data();
  store.shape.lines = store.lines.data();
}

}  // namespace

std::unique_ptr<ShapeStore> store_shape(const fo4::swf::ArtShape& source,
                                        std::uint16_t character_id) {
  auto store = std::make_unique<ShapeStore>();
  for (const fo4::swf::ArtFill& fill : source.fills) {
    radicle_art_fill out{};
    out.kind = fill.kind;
    out.color = fill.color;
    out.bitmap_id = fill.bitmap_id;
    out.spread = fill.spread;
    out.interpolation = fill.interpolation;
    out.focal_point = fill.focal_point;
    for (std::size_t index = 0; index < 6; ++index) {
      out.matrix[index] = fill.matrix[index];
    }
    out.stop_count = fill.stops.size();
    out.contour_count = fill.contours.size();
    out.bitmap_missing = flag_of(fill.bitmap_missing);
    for (const fo4::swf::ArtGradientStop& stop : fill.stops) {
      store->stops.push_back(radicle_art_gradient_stop{stop.ratio, stop.color});
    }
    for (const fo4::swf::ArtContour& contour : fill.contours) {
      push_contour(contour, *store);
    }
    store->fills.push_back(out);
  }
  for (const fo4::swf::ArtLine& line : source.lines) {
    radicle_art_line out{};
    out.width = line.width;
    out.color = line.color;
    out.caps = line.caps;
    out.joins = line.joins;
    out.miter_limit = line.miter_limit;
    out.contour_count = line.contours.size();
    for (const fo4::swf::ArtContour& contour : line.contours) {
      push_contour(contour, *store);
    }
    store->lines.push_back(out);
  }
  link_pointers(*store);
  store->shape.character_id = character_id;
  store->shape.fill_count = store->fills.size();
  store->shape.line_count = store->lines.size();
  return store;
}

void fill_placed_filters(const fo4::avm2::SnapshotEntry& entry,
                         std::vector<radicle_snapshot_filter>& out) {
  out.clear();
  for (const fo4::swf::SwfFilter& filter : entry.placed_filters) {
    radicle_snapshot_filter item{};
    item.kind = filter.id;
    item.color = filter.color;
    item.highlight_color = filter.highlight_color;
    item.blur_x = filter.blur_x;
    item.blur_y = filter.blur_y;
    item.angle = filter.angle;
    item.distance = filter.distance;
    item.strength = filter.strength;
    item.inner = flag_of(filter.inner);
    item.knockout = flag_of(filter.knockout);
    item.composite_source = flag_of(filter.composite_source);
    item.on_top = flag_of(filter.on_top);
    item.passes = filter.passes;
    item.matrix_columns = filter.matrix_columns;
    item.matrix_rows = filter.matrix_rows;
    item.divisor = filter.divisor;
    item.bias = filter.bias;
    item.default_color = filter.default_color;
    item.clamp = flag_of(filter.clamp);
    item.preserve_alpha = flag_of(filter.preserve_alpha);
    item.gradient_count = filter.gradient_colors.size();
    item.gradient_colors = filter.gradient_colors.empty() ? nullptr : filter.gradient_colors.data();
    item.gradient_ratios = filter.gradient_ratios.empty() ? nullptr : filter.gradient_ratios.data();
    item.matrix_count = filter.matrix.size();
    item.matrix = filter.matrix.empty() ? nullptr : filter.matrix.data();
    out.push_back(item);
  }
}

}  // namespace radicle_art_detail

namespace radicle_art_detail {

struct BitmapStore {
  std::vector<std::uint8_t> png;
  radicle_art_bitmap bitmap{};
};

struct FontStore {
  std::vector<std::unique_ptr<ShapeStore>> glyphs;
  std::vector<radicle_art_shape> glyph_shapes;
  radicle_art_font font{};
};

}  // namespace radicle_art_detail

struct radicle_art {
  std::vector<std::unique_ptr<radicle_art_detail::ShapeStore>> shapes;
  std::vector<std::unique_ptr<radicle_art_detail::BitmapStore>> bitmaps;
  std::vector<std::unique_ptr<radicle_art_detail::FontStore>> fonts;
};

extern "C" {

radicle_status radicle_movie_art_open(radicle_movie* movie, radicle_art** out_art) {
  clear_error();
  if (out_art) *out_art = nullptr;
  if (movie == nullptr || movie->file == nullptr || out_art == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "movie and out_art are required");
  }
  return guarded("unknown exception while building the art table", [&] {
    const fo4::swf::MovieArt source = fo4::swf::movie_art(*movie->file);
    auto art = std::make_unique<radicle_art>();
    for (const fo4::swf::ArtShape& shape : source.shapes) {
      art->shapes.push_back(radicle_art_detail::store_shape(shape, shape.character_id));
    }
    for (const fo4::swf::ArtBitmap& bitmap : source.bitmaps) {
      auto stored = std::make_unique<radicle_art_detail::BitmapStore>();
      stored->png = bitmap.png;
      stored->bitmap = radicle_art_bitmap{bitmap.character_id, bitmap.width, bitmap.height,
                                          stored->png.size(), stored->png.data()};
      art->bitmaps.push_back(std::move(stored));
    }
    for (const fo4::swf::ArtFont& font : source.fonts) {
      auto stored = std::make_unique<radicle_art_detail::FontStore>();
      for (const fo4::swf::ArtShape& glyph : font.glyphs) {
        stored->glyphs.push_back(radicle_art_detail::store_shape(glyph, glyph.character_id));
      }
      for (const auto& glyph : stored->glyphs) {
        stored->glyph_shapes.push_back(glyph->shape);
      }
      stored->font = radicle_art_font{font.font_id, stored->glyph_shapes.size(),
                                      stored->glyph_shapes.data()};
      art->fonts.push_back(std::move(stored));
    }
    *out_art = art.release();
    return RADICLE_STATUS_OK;
  });
}

void radicle_art_destroy(radicle_art* art) { delete art; }

size_t radicle_art_shape_count(const radicle_art* art) {
  return art == nullptr ? 0 : art->shapes.size();
}

radicle_status radicle_art_shape_at(const radicle_art* art, size_t index,
                                    radicle_art_shape* out_shape) {
  clear_error();
  if (out_shape) *out_shape = {};
  if (art == nullptr || out_shape == nullptr || index >= art->shapes.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the art shape index is out of range");
  }
  *out_shape = art->shapes[index]->shape;
  return RADICLE_STATUS_OK;
}

size_t radicle_art_bitmap_count(const radicle_art* art) {
  return art == nullptr ? 0 : art->bitmaps.size();
}

radicle_status radicle_art_bitmap_at(const radicle_art* art, size_t index,
                                     radicle_art_bitmap* out_bitmap) {
  clear_error();
  if (out_bitmap) *out_bitmap = {};
  if (art == nullptr || out_bitmap == nullptr || index >= art->bitmaps.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the art bitmap index is out of range");
  }
  *out_bitmap = art->bitmaps[index]->bitmap;
  return RADICLE_STATUS_OK;
}

size_t radicle_art_font_count(const radicle_art* art) {
  return art == nullptr ? 0 : art->fonts.size();
}

radicle_status radicle_art_font_at(const radicle_art* art, size_t index, radicle_art_font* out_font) {
  clear_error();
  if (out_font) *out_font = {};
  if (art == nullptr || out_font == nullptr || index >= art->fonts.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the art font index is out of range");
  }
  *out_font = art->fonts[index]->font;
  return RADICLE_STATUS_OK;
}

radicle_status radicle_runtime_snapshot_entry_extra_at(const radicle_runtime_snapshot* snapshot,
                                                       size_t index,
                                                       radicle_snapshot_entry_extra* out_extra) {
  clear_error();
  if (out_extra) *out_extra = {};
  if (snapshot == nullptr || out_extra == nullptr || index >= snapshot->frame.entries.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the snapshot entry index is out of range");
  }
  const fo4::avm2::SnapshotEntry& entry = snapshot->frame.entries[index];
  const std::vector<radicle_snapshot_filter>& filters = snapshot->filters[index];
  out_extra->drawn_key = entry.drawn_key;
  out_extra->drawn_revision = entry.drawn_revision;
  out_extra->placed_filter_count = filters.size();
  out_extra->placed_filters = filters.empty() ? nullptr : filters.data();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_runtime_snapshot_drawn_at(const radicle_runtime_snapshot* snapshot,
                                                 size_t index, radicle_art_shape* out_shape) {
  clear_error();
  if (out_shape) *out_shape = {};
  if (snapshot == nullptr || out_shape == nullptr || index >= snapshot->frame.entries.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the snapshot entry index is out of range");
  }
  const fo4::avm2::SnapshotEntry& entry = snapshot->frame.entries[index];
  if (entry.drawn_art == nullptr) {
    return fail(RADICLE_STATUS_NOT_FOUND, "the snapshot entry has no drawn shape");
  }
  std::unique_ptr<radicle_art_detail::ShapeStore>& cached = snapshot->drawn[index];
  if (cached == nullptr) {
    cached = radicle_art_detail::store_shape(*entry.drawn_art, 0);
  }
  *out_shape = cached->shape;
  return RADICLE_STATUS_OK;
}

}  // extern "C"
