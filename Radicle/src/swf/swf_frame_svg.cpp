#include "fo4/swf/swf_frame_svg.hpp"

#include "fo4/swf/swf_svg.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fo4::swf {

namespace {

constexpr double kTwipsPerPixel = 20.0;
constexpr double kMultiplyOne = 256.0;
constexpr double kAddOne = 255.0;

struct Mask {
  std::size_t level{0};
  std::uint16_t depth{0};
  std::uint16_t end{0};
  std::string id;
  bool inside{false};
};

using ShapeIndex = std::map<std::uint16_t, const SwfShape*>;

std::size_t level_of(const std::string& path) {
  return static_cast<std::size_t>(std::count(path.begin(), path.end(), '/'));
}

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

std::string frame_body(const ShapeIndex& shapes, const SvgBitmaps& bitmaps,
                       const SwfDisplayFrame& frame, const SwfRect& stage) {
  std::string body;
  std::vector<Mask> masks;
  for (std::size_t index = 0; index < frame.instances.size(); ++index) {
    const SwfFrameInstance& row = frame.instances[index];
    const std::size_t level = level_of(row.path);
    std::erase_if(masks, [&](const Mask& mask) {
      return level < mask.level || (level == mask.level && row.depth > mask.end);
    });
    for (Mask& mask : masks) {
      if (level == mask.level) {
        mask.inside = row.depth > mask.depth;
      }
    }
    const auto found = shapes.find(row.character_id);
    if (found == shapes.end()) {
      continue;
    }
    const SwfShape& shape = *found->second;
    const std::string id = "r" + std::to_string(index);
    if (row.clip_depth) {
      masks.push_back(Mask{level, row.depth, *row.clip_depth, id + "clip", false});
      body += shape_defs_to_svg(shape, id + "f", bitmaps) + "<clipPath id=\"" + id +
              "clip\" transform=\"" + transform_text(row.world_transform) + "\">\n" +
              shape_fill_paths_to_svg(shape, id + "f", bitmaps) + "</clipPath>\n";
      continue;
    }
    std::string content = "<g transform=\"" + transform_text(row.world_transform) + "\">\n" +
                          shape_defs_to_svg(shape, id + "f", bitmaps) +
                          shape_paths_to_svg(shape, id + "f", bitmaps) + "</g>\n";
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
  for (const SwfTag& tag : file.tags) {
    if (tag.shape) {
      shapes.emplace(tag.shape->shape_id, &*tag.shape);
    }
  }
  const SvgBitmaps bitmaps = index_bitmaps(file);
  std::string body;
  if (!display.timelines.empty() && frame < display.timelines.front().frames.size()) {
    body = frame_body(shapes, bitmaps, display.timelines.front().frames[frame],
                      file.header.frame_size);
  }
  return svg_open(file.header.frame_size) + body + "</svg>\n";
}

}  // namespace fo4::swf
