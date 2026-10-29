#include "fo4/swf/swf_svg_filters.hpp"

#include "fo4/swf/swf_svg.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fo4::swf {

namespace {

constexpr double kTwipsPerPixel = 20.0;
constexpr double kAlphaOne = 255.0;
constexpr double kOffsetOne = 255.0;
constexpr std::size_t kColorMatrixCells = 20;
constexpr std::uint8_t kDropShadowId = 0;
constexpr std::uint8_t kBlurId = 1;
constexpr std::uint8_t kGlowId = 2;
constexpr std::uint8_t kColorMatrixId = 6;

std::string hex_byte(std::uint32_t value) {
  constexpr char kDigits[] = "0123456789abcdef";
  const std::uint32_t byte = value & 0xFFu;
  return std::string{kDigits[byte >> 4], kDigits[byte & 0xFu]};
}

std::string rgb_text(std::uint32_t argb) {
  return "#" + hex_byte(argb >> 16) + hex_byte(argb >> 8) + hex_byte(argb);
}

std::string opacity_text(std::uint32_t argb, double strength) {
  return svg_number(std::min(1.0, (argb >> 24) / kAlphaOne * strength));
}

std::string region_text(const SwfRect& stage) {
  const double width = (static_cast<double>(stage.x_max) - stage.x_min) / kTwipsPerPixel;
  const double height = (static_cast<double>(stage.y_max) - stage.y_min) / kTwipsPerPixel;
  return " filterUnits=\"userSpaceOnUse\" x=\"" + svg_number(stage.x_min / kTwipsPerPixel) +
         "\" y=\"" + svg_number(stage.y_min / kTwipsPerPixel) + "\" width=\"" + svg_number(width) +
         "\" height=\"" + svg_number(height) + "\"";
}

std::string deviation_text(const SwfFilter& filter) {
  return svg_number(filter.blur_x / 2.0) + " " + svg_number(filter.blur_y / 2.0);
}

std::string shadow_body(const SwfFilter& filter, double dx, double dy) {
  std::string body;
  std::string input = "SourceAlpha";
  if (filter.inner) {
    body = "<feComponentTransfer in=\"SourceAlpha\" result=\"h\"><feFuncA type=\"table\" "
           "tableValues=\"1 0\"/></feComponentTransfer>";
    input = "h";
  }
  body += "<feOffset in=\"" + input + "\" dx=\"" + svg_number(dx) + "\" dy=\"" + svg_number(dy) +
          "\" result=\"o\"/><feGaussianBlur in=\"o\" stdDeviation=\"" + deviation_text(filter) +
          "\" result=\"b\"/><feFlood flood-color=\"" + rgb_text(filter.color) +
          "\" flood-opacity=\"" + opacity_text(filter.color, filter.strength) +
          "\" result=\"f\"/><feComposite in=\"f\" in2=\"b\" operator=\"in\" result=\"s\"/>";
  std::string last = "s";
  if (filter.inner) {
    body += "<feComposite in=\"s\" in2=\"SourceAlpha\" operator=\"in\" result=\"i\"/>";
    last = "i";
  } else if (filter.knockout) {
    body += "<feComposite in=\"s\" in2=\"SourceAlpha\" operator=\"out\" result=\"k\"/>";
    last = "k";
  }
  const std::string shadow = "<feMergeNode in=\"" + last + "\"/>";
  const std::string source = "<feMergeNode in=\"SourceGraphic\"/>";
  std::string nodes = shadow;
  if (filter.composite_source && !filter.knockout) {
    nodes = filter.inner ? source + shadow : shadow + source;
  }
  return body + "<feMerge>" + nodes + "</feMerge>";
}

std::string color_matrix_body(const SwfFilter& filter) {
  std::string values;
  for (std::size_t index = 0; index < kColorMatrixCells; ++index) {
    double value = filter.matrix[index];
    if (index % 5 == 4) {
      value /= kOffsetOne;
    }
    if (index > 0) {
      values += " ";
    }
    values += svg_number(value);
  }
  return "<feColorMatrix in=\"SourceGraphic\" type=\"matrix\" values=\"" + values + "\"/>";
}

std::optional<std::string> filter_body(const SwfFilter& filter) {
  switch (filter.id) {
    case kDropShadowId:
      return shadow_body(filter, filter.distance * std::cos(filter.angle),
                         filter.distance * std::sin(filter.angle));
    case kGlowId:
      return shadow_body(filter, 0.0, 0.0);
    case kBlurId:
      return "<feGaussianBlur in=\"SourceGraphic\" stdDeviation=\"" + deviation_text(filter) +
             "\"/>";
    case kColorMatrixId:
      if (filter.matrix.size() == kColorMatrixCells) {
        return color_matrix_body(filter);
      }
      return std::nullopt;
    default:
      return std::nullopt;
  }
}

}  // namespace

SvgFilterDefs filter_defs(const std::vector<SwfFilter>& filters, const std::string& id,
                          const SwfRect& stage) {
  SvgFilterDefs out;
  std::string elements;
  for (std::size_t index = 0; index < filters.size(); ++index) {
    const std::optional<std::string> body = filter_body(filters[index]);
    if (!body) {
      out.skipped.push_back(filters[index].id);
      continue;
    }
    const std::string filter_id = id + "fl" + std::to_string(index);
    elements += "<filter id=\"" + filter_id + "\"" + region_text(stage) +
                " color-interpolation-filters=\"sRGB\">" + *body + "</filter>";
    out.ids.push_back(filter_id);
  }
  if (!out.ids.empty()) {
    out.defs = "<defs>" + elements + "</defs>\n";
  }
  return out;
}

std::string blend_mode_style(std::uint8_t blend_mode) {
  switch (blend_mode) {
    case 3:
      return "mix-blend-mode:multiply";
    case 4:
      return "mix-blend-mode:screen";
    case 5:
      return "mix-blend-mode:lighten";
    case 6:
      return "mix-blend-mode:darken";
    case 7:
      return "mix-blend-mode:difference";
    case 8:
      return "mix-blend-mode:plus-lighter";
    case 13:
      return "mix-blend-mode:overlay";
    case 14:
      return "mix-blend-mode:hard-light";
    default:
      return {};
  }
}

std::string filtered_group(const std::vector<SwfFilter>& filters,
                           std::optional<std::uint8_t> blend_mode, const std::string& id,
                           const SwfRect& stage, const std::string& content) {
  const SvgFilterDefs defs = filter_defs(filters, id, stage);
  std::string out = content;
  for (const std::string& filter_id : defs.ids) {
    out = "<g filter=\"url(#" + filter_id + ")\">\n" + out + "</g>\n";
  }
  out = defs.defs + out;
  const std::string style = blend_mode ? blend_mode_style(*blend_mode) : std::string{};
  if (!style.empty()) {
    out = "<g style=\"" + style + "\">\n" + out + "</g>\n";
  }
  return out;
}

}  // namespace fo4::swf
