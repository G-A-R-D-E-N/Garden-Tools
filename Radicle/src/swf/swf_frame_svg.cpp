#include "fo4/swf/swf_frame_svg.hpp"

#include "fo4/swf/swf_svg.hpp"

#include <algorithm>
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

const SwfShape* row_shape(const ShapeIndex& shapes, const MorphIndex& morphs,
                          const SwfFrameInstance& row, std::optional<SwfShape>& morph) {
  const auto found = shapes.find(row.character_id);
  if (found != shapes.end()) {
    return found->second;
  }
  const auto morph_found = morphs.find(row.character_id);
  if (morph_found == morphs.end()) {
    return nullptr;
  }
  morph = interpolate_morph_shape(*morph_found->second, row.ratio.value_or(0) / kRatioLimit);
  return morph ? &*morph : nullptr;
}

std::string text_paths(const FontIndex& fonts, const SwfText& text) {
  const SwfTransform text_transform = transform_from_matrix(text.matrix);
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

std::string sprite_clip_to_svg(const ShapeIndex& shapes, const MorphIndex& morphs,
                               const SvgBitmaps& bitmaps, const SwfDisplayFrame& frame,
                               std::size_t index) {
  const SwfFrameInstance& sprite = frame.instances[index];
  const std::size_t end = descendants_end(frame, index);
  std::string defs;
  std::string paths;
  for (std::size_t child = index + 1; child < end; ++child) {
    const SwfFrameInstance& row = frame.instances[child];
    std::optional<SwfShape> morph;
    const SwfShape* shape = row_shape(shapes, morphs, row, morph);
    if (!shape || row.world_visible != sprite.world_visible) {
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

std::string frame_body(const ShapeIndex& shapes, const MorphIndex& morphs, const TextIndex& texts,
                       const FontIndex& fonts, const SvgBitmaps& bitmaps,
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
    const SwfShape* shape = row_shape(shapes, morphs, row, morph);
    const std::string id = "r" + std::to_string(index);
    if (row.clip_depth && (row.is_sprite || shape)) {
      masks.push_back(Mask{level, row.depth, *row.clip_depth, id + "clip", false});
      if (row.is_sprite) {
        mask_end = descendants_end(frame, index);
        body += sprite_clip_to_svg(shapes, morphs, bitmaps, frame, index);
      } else {
        const SvgImages images = shape_images(*shape, bitmaps);
        body += shape_defs_to_svg(*shape, id + "f", images) + "<clipPath id=\"" + id +
                "clip\" transform=\"" + transform_text(row.world_transform) + "\">\n" +
                shape_fill_paths_to_svg(*shape, id + "f", images) + "</clipPath>\n";
      }
      continue;
    }
    if (!row.world_visible) {
      continue;
    }
    std::string content;
    if (shape) {
      const SvgImages images = shape_images(*shape, bitmaps);
      content = "<g transform=\"" + transform_text(row.world_transform) + "\">\n" +
                shape_defs_to_svg(*shape, id + "f", images) +
                shape_paths_to_svg(*shape, id + "f", images) + "</g>\n";
    } else {
      const auto text = texts.find(row.character_id);
      if (row.clip_depth || text == texts.end()) {
        continue;
      }
      const std::string paths = text_paths(fonts, *text->second);
      if (paths.empty()) {
        continue;
      }
      content = "<g transform=\"" + transform_text(row.world_transform) + "\">\n" + paths +
                "</g>\n";
    }
    if (row.world_color_transform) {
      content = filter_text(id + "color", *row.world_color_transform, stage) +
                "<g filter=\"url(#" + id + "color)\">\n" + content + "</g>\n";
    }
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

std::string frame_to_svg(const SwfFile& file, const SwfDisplayList& display, std::uint32_t frame) {
  ShapeIndex shapes;
  MorphIndex morphs;
  TextIndex texts;
  FontIndex fonts;
  for (const SwfTag& tag : file.tags) {
    if (tag.shape) {
      shapes.emplace(tag.shape->shape_id, &*tag.shape);
    }
    if (tag.morph_shape) {
      morphs.emplace(tag.morph_shape->character_id, &*tag.morph_shape);
    }
    if (tag.text) {
      texts.emplace(tag.text->character_id, &*tag.text);
    }
    if (tag.font) {
      fonts.emplace(tag.font->font_id, &*tag.font);
    }
  }
  const SvgBitmaps bitmaps = index_bitmaps(file);
  std::string body;
  if (!display.timelines.empty() && frame < display.timelines.front().frames.size()) {
    body = frame_body(shapes, morphs, texts, fonts, bitmaps,
                      display.timelines.front().frames[frame], file.header.frame_size);
  }
  return svg_open(file.header.frame_size) + body + "</svg>\n";
}

}  // namespace fo4::swf
