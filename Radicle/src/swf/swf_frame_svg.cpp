#include "fo4/swf/swf_frame_svg.hpp"

#include "fo4/swf/swf_svg.hpp"
#include "fo4/swf/swf_svg_filters.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fo4::swf {

namespace {

constexpr double kTwipsPerPixel = 20.0;
constexpr double kMultiplyOne = 256.0;
constexpr double kAddOne = 255.0;
constexpr double kRatioLimit = 65535.0;
constexpr double kFontUnits = 1024.0;
constexpr double kFont3Units = 20480.0;
constexpr std::uint8_t kSolidFill = 0x00;
constexpr std::uint32_t kDefaultTextColor = 0xFF000000u;

struct Mask {
  std::size_t level{0};
  std::uint16_t depth{0};
  std::uint16_t end{0};
  std::string id;
  bool inside{false};
};

using ShapeIndex = std::map<std::uint16_t, const SwfShape*>;
using MorphIndex = std::map<std::uint16_t, const SwfMorphShape*>;
using TextIndex = std::map<std::uint16_t, const SwfText*>;
using FontIndex = std::map<std::uint16_t, const SwfFont*>;
using ButtonIndex = std::map<std::uint16_t, const SwfButton*>;

struct FrameLookup {
  ShapeIndex shapes;
  MorphIndex morphs;
  TextIndex texts;
  FontIndex fonts;
  ButtonIndex buttons;
  std::map<std::uint16_t, SwfRect> grids;
};

struct NineSliceAxis {
  std::array<double, 4> source;
  std::array<double, 4> destination;
};

std::string transform_text(const SwfTransform& transform) {
  return "matrix(" + svg_number(transform.scale_x) + " " + svg_number(transform.rotate_skew0) +
         " " + svg_number(transform.rotate_skew1) + " " + svg_number(transform.scale_y) + " " +
         svg_number(transform.translate_x) + " " + svg_number(transform.translate_y) + ")";
}

std::string color_matrix_text(const SwfColorTransform& color) {
  std::string values;
  for (std::size_t channel = 0; channel < 4; ++channel) {
    for (std::size_t source = 0; source < 4; ++source) {
      values += (source == channel ? svg_number(color.multiply[channel] / kMultiplyOne) : "0") + " ";
    }
    values += svg_number(color.add[channel] / kAddOne) + (channel == 3 ? "" : " ");
  }
  return values;
}

std::string stage_region_text(const SwfRect& stage) {
  const double width = (static_cast<double>(stage.x_max) - stage.x_min) / kTwipsPerPixel;
  const double height = (static_cast<double>(stage.y_max) - stage.y_min) / kTwipsPerPixel;
  return " filterUnits=\"userSpaceOnUse\" x=\"" + svg_number(stage.x_min / kTwipsPerPixel) +
         "\" y=\"" + svg_number(stage.y_min / kTwipsPerPixel) + "\" width=\"" + svg_number(width) +
         "\" height=\"" + svg_number(height) + "\"";
}

std::string filter_text(const std::string& id, const SwfColorTransform& color,
                        const SwfRect& stage) {
  return "<defs><filter id=\"" + id + "\"" + stage_region_text(stage) +
         " color-interpolation-filters=\"sRGB\">"
         "<feColorMatrix type=\"matrix\" values=\"" + color_matrix_text(color) +
         "\"/></filter></defs>\n";
}

std::size_t descendants_end(const SwfDisplayFrame& frame, std::size_t index) {
  const std::size_t level = frame.instances[index].nesting_level;
  std::size_t end = index + 1;
  while (end < frame.instances.size() && frame.instances[end].nesting_level > level) {
    ++end;
  }
  return end;
}

const SwfShape* character_shape(const FrameLookup& lookup, std::uint16_t character_id,
                                std::optional<std::uint16_t> ratio, std::optional<SwfShape>& morph) {
  const auto found = lookup.shapes.find(character_id);
  if (found != lookup.shapes.end()) {
    return found->second;
  }
  const auto morph_found = lookup.morphs.find(character_id);
  if (morph_found == lookup.morphs.end()) {
    return nullptr;
  }
  morph = interpolate_morph_shape(*morph_found->second, ratio.value_or(0) / kRatioLimit);
  return morph ? &*morph : nullptr;
}

std::string text_paths(const FontIndex& fonts, const SwfText& text,
                       const SwfTransform& outer = {}) {
  const SwfTransform text_transform = compose_transform(outer, transform_from_matrix(text.matrix));
  std::string paths;
  const SwfFont* font = nullptr;
  std::uint32_t color = kDefaultTextColor;
  double height = 0.0;
  std::int64_t x = 0;
  std::int64_t y = 0;
  for (const SwfTextRecord& record : text.records) {
    if (record.font_id) {
      const auto found = fonts.find(*record.font_id);
      font = found == fonts.end() ? nullptr : found->second;
    }
    if (record.color) {
      color = *record.color;
    }
    if (record.height) {
      height = *record.height;
    }
    if (record.x_offset) {
      x = *record.x_offset;
    }
    if (record.y_offset) {
      y = *record.y_offset;
    }
    for (const SwfTextGlyph& glyph : record.glyphs) {
      if (font && height > 0.0 && glyph.glyph_index < font->glyphs.size()) {
        SwfFillStyle fill;
        fill.type = kSolidFill;
        fill.color = color;
        SwfShape shape;
        shape.fill_styles.push_back(fill);
        shape.records = font->glyphs[glyph.glyph_index].records;
        SwfTransform placed;
        placed.scale_x = height / (font->define_font3 ? kFont3Units : kFontUnits);
        placed.scale_y = placed.scale_x;
        placed.translate_x = static_cast<double>(x) / kTwipsPerPixel;
        placed.translate_y = static_cast<double>(y) / kTwipsPerPixel;
        paths += shape_fill_paths_to_svg(shape, "text", {},
                                         transform_text(compose_transform(text_transform, placed)));
      }
      x += glyph.advance;
    }
  }
  return paths;
}

std::string sprite_clip_to_svg(const FrameLookup& lookup, const SvgBitmaps& bitmaps,
                               const SwfDisplayFrame& frame, std::size_t index) {
  const SwfFrameInstance& sprite = frame.instances[index];
  const std::size_t end = descendants_end(frame, index);
  std::string defs;
  std::string paths;
  for (std::size_t child = index + 1; child < end; ++child) {
    const SwfFrameInstance& row = frame.instances[child];
    std::optional<SwfShape> morph;
    const SwfShape* shape = character_shape(lookup, row.character_id, row.ratio, morph);
    if (row.world_visible != sprite.world_visible) {
      continue;
    }
    if (!shape) {
      const auto text = lookup.texts.find(row.character_id);
      if (text != lookup.texts.end()) {
        paths += text_paths(lookup.fonts, *text->second, row.world_transform);
      }
      continue;
    }
    const SvgImages images = shape_images(*shape, bitmaps);
    const std::string id = "r" + std::to_string(child);
    defs += shape_defs_to_svg(*shape, id + "f", images);
    paths += shape_fill_paths_to_svg(*shape, id + "f", images,
                                     transform_text(row.world_transform));
  }
  return defs + "<clipPath id=\"r" + std::to_string(index) + "clip\">\n" + paths + "</clipPath>\n";
}

std::string shape_group(const SvgBitmaps& bitmaps, const SwfShape& shape, const std::string& id,
                        const SwfTransform& transform) {
  const SvgImages images = shape_images(shape, bitmaps);
  return "<g transform=\"" + transform_text(transform) + "\">\n" +
         shape_defs_to_svg(shape, id + "f", images) + shape_paths_to_svg(shape, id + "f", images) +
         "</g>\n";
}

std::string text_group(const FontIndex& fonts, const SwfText& text, const SwfTransform& transform) {
  const std::string paths = text_paths(fonts, text);
  if (paths.empty()) {
    return {};
  }
  return "<g transform=\"" + transform_text(transform) + "\">\n" + paths + "</g>\n";
}

std::string character_group(const FrameLookup& lookup, const SvgBitmaps& bitmaps,
                            std::uint16_t character_id, const std::string& id,
                            const SwfTransform& transform) {
  std::optional<SwfShape> morph;
  if (const SwfShape* shape = character_shape(lookup, character_id, {}, morph)) {
    return shape_group(bitmaps, *shape, id, transform);
  }
  const auto text = lookup.texts.find(character_id);
  return text == lookup.texts.end() ? std::string{}
                                    : text_group(lookup.fonts, *text->second, transform);
}

std::string button_group(const FrameLookup& lookup, const SvgBitmaps& bitmaps,
                         const SwfButton& button, const std::string& id,
                         const SwfTransform& transform) {
  std::vector<SwfButtonRecord> up;
  for (const SwfButtonRecord& record : button.records) {
    if (record.up) {
      up.push_back(record);
    }
  }
  std::stable_sort(up.begin(), up.end(),
                   [](const SwfButtonRecord& left, const SwfButtonRecord& right) {
                     return left.depth < right.depth;
                   });
  std::string content;
  for (std::size_t index = 0; index < up.size(); ++index) {
    const auto record = compose_transform(transform, transform_from_matrix(up[index].matrix));
    content += character_group(lookup, bitmaps, up[index].character_id,
                               id + "u" + std::to_string(index), record);
  }
  return content;
}

NineSliceAxis nine_slice_axis(double low, double high, double scale, double offset,
                              double grid_low, double grid_high) {
  const double first = std::clamp(scale * grid_low + offset, low, high);
  const double second = std::clamp(scale * grid_high + offset, first, high);
  const double fixed_first = std::min(low + (first - low) / scale, high);
  const double fixed_second = std::clamp(high - (high - second) / scale, fixed_first, high);
  return {{low, first, second, high}, {low, fixed_first, fixed_second, high}};
}

SwfBounds cell_box(const std::array<double, 4>& x, const std::array<double, 4>& y,
                   std::size_t column, std::size_t row) {
  return {x[column], y[row], x[column + 1] - x[column], y[row + 1] - y[row]};
}

SwfTransform stretch_transform(const SwfBounds& source, const SwfBounds& destination) {
  SwfTransform stretch;
  stretch.scale_x = source.width > 0.0 ? destination.width / source.width : 1.0;
  stretch.scale_y = source.height > 0.0 ? destination.height / source.height : 1.0;
  stretch.translate_x = destination.x - stretch.scale_x * source.x;
  stretch.translate_y = destination.y - stretch.scale_y * source.y;
  return stretch;
}

std::string nine_slice_group(const std::array<NineSliceCell, 9>& cells, const std::string& id,
                             const std::string& content) {
  std::string clips;
  std::string groups;
  for (std::size_t index = 0; index < cells.size(); ++index) {
    const NineSliceCell& cell = cells[index];
    const std::string cell_id = id + "s" + std::to_string(index);
    clips += "<clipPath id=\"" + cell_id + "clip\"><rect x=\"" + svg_number(cell.source.x) +
             "\" y=\"" + svg_number(cell.source.y) + "\" width=\"" +
             svg_number(cell.source.width) + "\" height=\"" + svg_number(cell.source.height) +
             "\"/></clipPath>\n";
    groups += "<g transform=\"" +
              transform_text(stretch_transform(cell.source, cell.destination)) +
              "\">\n<g clip-path=\"url(#" + cell_id + "clip)\">\n<use href=\"#" + id +
              "sg\"/>\n</g>\n</g>\n";
  }
  return "<defs>\n<g id=\"" + id + "sg\">\n" + content + "</g>\n" + clips + "</defs>\n" + groups;
}

std::optional<SwfBounds> sprite_bounds(const SwfDisplayFrame& frame, std::size_t index) {
  const std::size_t end = descendants_end(frame, index);
  std::optional<SwfBounds> box;
  for (std::size_t child = index + 1; child < end; ++child) {
    const SwfFrameInstance& row = frame.instances[child];
    if (!row.has_bounds || !row.world_visible || row.clip_depth) {
      continue;
    }
    if (!box) {
      box = row.bounds;
      continue;
    }
    const double x = std::min(box->x, row.bounds.x);
    const double y = std::min(box->y, row.bounds.y);
    const double right = std::max(box->x + box->width, row.bounds.x + row.bounds.width);
    const double bottom = std::max(box->y + box->height, row.bounds.y + row.bounds.height);
    box = SwfBounds{x, y, right - x, bottom - y};
  }
  return box;
}

std::optional<std::array<NineSliceCell, 9>> nine_slice_cells(const FrameLookup& lookup,
                                                             const SwfDisplayFrame& frame,
                                                             std::size_t index) {
  const SwfFrameInstance& row = frame.instances[index];
  const SwfTransform& placement = row.world_transform;
  const auto grid = lookup.grids.find(row.character_id);
  if (grid == lookup.grids.end() || placement.rotate_skew0 != 0.0 ||
      placement.rotate_skew1 != 0.0 || placement.scale_x <= 0.0 || placement.scale_y <= 0.0 ||
      (placement.scale_x == 1.0 && placement.scale_y == 1.0)) {
    return std::nullopt;
  }
  std::optional<SwfBounds> placed;
  if (row.is_sprite) {
    placed = sprite_bounds(frame, index);
  } else if (lookup.shapes.count(row.character_id) != 0 && row.has_bounds) {
    placed = row.bounds;
  }
  if (!placed || placed->width <= 0.0 || placed->height <= 0.0) {
    return std::nullopt;
  }
  return nine_slice_regions(grid->second, *placed, placement);
}

std::string sprite_content(const FrameLookup& lookup, const SvgBitmaps& bitmaps,
                           const SwfDisplayFrame& frame, std::size_t index, const std::string& id) {
  std::string content;
  const std::size_t end = descendants_end(frame, index);
  for (std::size_t child = index + 1; child < end; ++child) {
    const SwfFrameInstance& row = frame.instances[child];
    if (!row.world_visible || row.clip_depth) {
      continue;
    }
    std::optional<SwfShape> morph;
    const std::string child_id = id + "c" + std::to_string(child);
    if (const SwfShape* shape = character_shape(lookup, row.character_id, row.ratio, morph)) {
      content += shape_group(bitmaps, *shape, child_id, row.world_transform);
      continue;
    }
    const auto text = lookup.texts.find(row.character_id);
    if (text != lookup.texts.end()) {
      content += text_group(lookup.fonts, *text->second, row.world_transform);
    }
  }
  return content;
}

std::string frame_body(const FrameLookup& lookup, const SvgBitmaps& bitmaps,
                       const SwfDisplayFrame& frame, const SwfRect& stage) {
  std::string body;
  std::vector<Mask> masks;
  std::size_t mask_end = 0;
  for (std::size_t index = 0; index < frame.instances.size(); ++index) {
    if (index < mask_end) {
      continue;
    }
    const SwfFrameInstance& row = frame.instances[index];
    const std::size_t level = row.nesting_level;
    std::erase_if(masks, [&](const Mask& mask) {
      return level < mask.level || (level == mask.level && row.depth > mask.end);
    });
    for (Mask& mask : masks) {
      if (level == mask.level) {
        mask.inside = row.depth > mask.depth;
      }
    }
    std::optional<SwfShape> morph;
    const SwfShape* shape = character_shape(lookup, row.character_id, row.ratio, morph);
    const std::string id = "r" + std::to_string(index);
    const auto text = lookup.texts.find(row.character_id);
    const bool has_text = text != lookup.texts.end();
    if (row.clip_depth && (row.is_sprite || shape || has_text)) {
      masks.push_back(Mask{level, row.depth, *row.clip_depth, id + "clip", false});
      if (row.is_sprite) {
        mask_end = descendants_end(frame, index);
        body += sprite_clip_to_svg(lookup, bitmaps, frame, index);
      } else if (shape) {
        const SvgImages images = shape_images(*shape, bitmaps);
        body += shape_defs_to_svg(*shape, id + "f", images) + "<clipPath id=\"" + id +
                "clip\" transform=\"" + transform_text(row.world_transform) + "\">\n" +
                shape_fill_paths_to_svg(*shape, id + "f", images) + "</clipPath>\n";
      } else {
        body += "<clipPath id=\"" + id + "clip\" transform=\"" +
                transform_text(row.world_transform) + "\">\n" +
                text_paths(lookup.fonts, *text->second) + "</clipPath>\n";
      }
      continue;
    }
    if (!row.world_visible) {
      continue;
    }
    std::string content;
    const auto button = lookup.buttons.find(row.character_id);
    if (shape) {
      content = shape_group(bitmaps, *shape, id, row.world_transform);
    } else if (button != lookup.buttons.end() && !row.clip_depth) {
      content = button_group(lookup, bitmaps, *button->second, id, row.world_transform);
    } else if (has_text) {
      content = text_group(lookup.fonts, *text->second, row.world_transform);
    }
    const auto cells = nine_slice_cells(lookup, frame, index);
    if (cells && row.is_sprite) {
      mask_end = descendants_end(frame, index);
      content = sprite_content(lookup, bitmaps, frame, index, id);
    }
    if (cells && !content.empty()) {
      content = nine_slice_group(*cells, id, content);
    }
    if (content.empty()) {
      continue;
    }
    if (row.world_color_transform) {
      content = filter_text(id + "color", *row.world_color_transform, stage) +
                "<g filter=\"url(#" + id + "color)\">\n" + content + "</g>\n";
    }
    content = filtered_group(row.filters, row.blend_mode, id, stage, content);
    for (const Mask& mask : masks) {
      if (mask.inside) {
        content = "<g clip-path=\"url(#" + mask.id + ")\">\n" + content + "</g>\n";
      }
    }
    body += content;
  }
  return body;
}

}  // namespace

std::array<NineSliceCell, 9> nine_slice_regions(const SwfRect& splitter, const SwfBounds& placed,
                                                const SwfTransform& placement) {
  const NineSliceAxis x =
      nine_slice_axis(placed.x, placed.x + placed.width, placement.scale_x, placement.translate_x,
                      splitter.x_min / kTwipsPerPixel, splitter.x_max / kTwipsPerPixel);
  const NineSliceAxis y =
      nine_slice_axis(placed.y, placed.y + placed.height, placement.scale_y, placement.translate_y,
                      splitter.y_min / kTwipsPerPixel, splitter.y_max / kTwipsPerPixel);
  std::array<NineSliceCell, 9> cells{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      NineSliceCell& cell = cells[row * 3 + column];
      cell.source = cell_box(x.source, y.source, column, row);
      cell.destination = cell_box(x.destination, y.destination, column, row);
    }
  }
  return cells;
}

std::string frame_to_svg(const SwfFile& file, const SwfDisplayList& display, std::uint32_t frame) {
  FrameLookup lookup;
  for (const SwfTag& tag : file.tags) {
    if (tag.shape) {
      lookup.shapes.emplace(tag.shape->shape_id, &*tag.shape);
    }
    if (tag.morph_shape) {
      lookup.morphs.emplace(tag.morph_shape->character_id, &*tag.morph_shape);
    }
    if (tag.text) {
      lookup.texts.emplace(tag.text->character_id, &*tag.text);
    }
    if (tag.font) {
      lookup.fonts.emplace(tag.font->font_id, &*tag.font);
    }
    if (tag.button) {
      lookup.buttons.emplace(tag.button->character_id, &*tag.button);
    }
    if (tag.scaling_grid) {
      lookup.grids.insert_or_assign(tag.scaling_grid->character_id, tag.scaling_grid->splitter);
    }
  }
  const SvgBitmaps bitmaps = index_bitmaps(file);
  std::string body;
  if (!display.timelines.empty() && frame < display.timelines.front().frames.size()) {
    body = frame_body(lookup, bitmaps, display.timelines.front().frames[frame],
                      file.header.frame_size);
  }
  return svg_open(file.header.frame_size) + body + "</svg>\n";
}

}  // namespace fo4::swf
