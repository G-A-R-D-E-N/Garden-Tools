#pragma once

#include "fo4/swf/swf.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace fo4::swf {

struct ArtPoint {
  double x{0.0};
  double y{0.0};
};

struct ArtContour {
  std::vector<ArtPoint> points;
  bool closed{false};
};

struct ArtGradientStop {
  std::uint8_t ratio{0};
  std::uint32_t color{0};
};

struct ArtFill {
  std::uint8_t kind{0};
  std::uint32_t color{0};
  std::uint16_t bitmap_id{0};
  std::uint8_t spread{0};
  std::uint8_t interpolation{0};
  double focal_point{0.0};
  std::array<double, 6> matrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
  std::vector<ArtGradientStop> stops;
  std::vector<ArtContour> contours;
  bool bitmap_missing{false};
};

struct ArtLine {
  double width{0.0};
  std::uint32_t color{0};
  std::uint8_t caps{0};
  std::uint8_t joins{0};
  double miter_limit{0.0};
  std::vector<ArtContour> contours;
};

struct ArtShape {
  std::uint16_t character_id{0};
  std::vector<ArtFill> fills;
  std::vector<ArtLine> lines;
};

struct ArtBitmap {
  std::uint16_t character_id{0};
  std::uint16_t width{0};
  std::uint16_t height{0};
  std::vector<std::uint8_t> png;
};

struct ArtFont {
  std::uint16_t font_id{0};
  std::vector<ArtShape> glyphs;
};

struct MovieArt {
  std::vector<ArtShape> shapes;
  std::vector<ArtBitmap> bitmaps;
  std::vector<ArtFont> fonts;
};

/* Requires records in twips scaled by `scale` into output units; fill matrix translations are always twips. */
ArtShape shape_art(const SwfShape& shape, double scale);

/* Requires a parsed file; the first definition of an id wins, and a bitmap without a PNG export is left out. */
MovieArt movie_art(const SwfFile& file);

}  // namespace fo4::swf
