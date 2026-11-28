#include "fo4/swf/swf_font_ttf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

constexpr std::size_t kMaxGlyphs = 65534;
constexpr std::size_t kMaxKernPairs = (65535 - 14) / 6;
constexpr int kCurveSteps = 8;

class Be {
public:
  void u8(std::uint32_t value) { bytes.push_back(static_cast<std::uint8_t>(value)); }
  void u16(std::uint32_t value) {
    u8(value >> 8);
    u8(value);
  }
  void i16(int value) { u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value))); }
  void u32(std::uint32_t value) {
    u16(value >> 16);
    u16(value);
  }
  void tag(const char (&name)[5]) {
    for (int index = 0; index < 4; ++index) {
      u8(static_cast<unsigned char>(name[index]));
    }
  }
  void append(const std::vector<std::uint8_t>& other) {
    bytes.insert(bytes.end(), other.begin(), other.end());
  }
  void pad4() {
    while (bytes.size() % 4 != 0) {
      u8(0);
    }
  }
  std::vector<std::uint8_t> bytes;
};

struct RawPoint {
  std::int64_t x{0};
  std::int64_t y{0};
  bool on{true};
};

struct Point {
  int x{0};
  int y{0};
  bool on{true};
};

using RawContour = std::vector<RawPoint>;
using Contour = std::vector<Point>;

struct Glyph {
  std::vector<Contour> contours;
  int x_min{0};
  int y_min{0};
  int x_max{0};
  int y_max{0};
  std::vector<std::uint8_t> data;
};

std::vector<RawContour> trace_contours(const SwfGlyph& glyph) {
  std::vector<RawContour> done;
  RawContour current;
  RawPoint pen;
  const auto finish = [&] {
    if (current.size() >= 3) {
      done.push_back(std::move(current));
    }
    current.clear();
  };
  const auto append = [&](std::int64_t x, std::int64_t y, bool on) {
    if (current.empty()) {
      current.push_back(RawPoint{pen.x, pen.y, true});
    }
    current.push_back(RawPoint{x, y, on});
  };
  const auto close_if_returned = [&] {
    if (current.size() > 2 && current.front().x == pen.x && current.front().y == pen.y) {
      current.pop_back();
      finish();
    }
  };
  for (const SwfShapeRecord& record : glyph.records) {
    switch (record.kind) {
      case SwfShapeRecordKind::move:
        finish();
        pen = RawPoint{record.x, record.y, true};
        break;
      case SwfShapeRecordKind::style_change:
        break;
      case SwfShapeRecordKind::straight_edge:
        if (record.x == 0 && record.y == 0) {
          break;
        }
        append(pen.x + record.x, pen.y + record.y, true);
        pen.x += record.x;
        pen.y += record.y;
        close_if_returned();
        break;
      case SwfShapeRecordKind::curved_edge: {
        const std::int64_t control_x = pen.x + record.control_x;
        const std::int64_t control_y = pen.y + record.control_y;
        append(control_x, control_y, false);
        pen.x = control_x + record.x;
        pen.y = control_y + record.y;
        append(pen.x, pen.y, true);
        close_if_returned();
        break;
      }
    }
  }
  finish();
  return done;
}

bool scale_coordinate(std::int64_t value, int scale, int& out) {
  const std::int64_t magnitude = value < 0 ? -value : value;
  const std::int64_t scaled = (magnitude + scale / 2) / scale;
  const std::int64_t signed_value = value < 0 ? -scaled : scaled;
  if (signed_value < std::numeric_limits<std::int16_t>::min() ||
      signed_value > std::numeric_limits<std::int16_t>::max()) {
    return false;
  }
  out = static_cast<int>(signed_value);
  return true;
}

void rotate_to_on_curve(Contour& contour) {
  const auto first = std::find_if(contour.begin(), contour.end(), [](const Point& p) { return p.on; });
  std::rotate(contour.begin(), first, contour.end());
}

using Polygon = std::vector<std::pair<double, double>>;

Polygon flatten(const Contour& contour) {
  Polygon polygon;
  const std::size_t count = contour.size();
  for (std::size_t index = 0; index < count;) {
    const Point& from = contour[index];
    polygon.emplace_back(from.x, from.y);
    const std::size_t next = (index + 1) % count;
    if (contour[next].on) {
      ++index;
      continue;
    }
    const Point& control = contour[next];
    const Point& to = contour[(index + 2) % count];
    for (int step = 1; step < kCurveSteps; ++step) {
      const double t = static_cast<double>(step) / kCurveSteps;
      const double u = 1.0 - t;
      polygon.emplace_back(u * u * from.x + 2 * u * t * control.x + t * t * to.x,
                           u * u * from.y + 2 * u * t * control.y + t * t * to.y);
    }
    index += 2;
  }
  return polygon;
}

double signed_area(const Polygon& polygon) {
  double area = 0;
  for (std::size_t index = 0; index < polygon.size(); ++index) {
    const auto& a = polygon[index];
    const auto& b = polygon[(index + 1) % polygon.size()];
    area += a.first * b.second - b.first * a.second;
  }
  return area / 2;
}

bool contains(const Polygon& polygon, double x, double y) {
  bool inside = false;
  for (std::size_t index = 0, last = polygon.size() - 1; index < polygon.size(); last = index++) {
    const auto& a = polygon[index];
    const auto& b = polygon[last];
    if ((a.second > y) != (b.second > y) &&
        x < (b.first - a.first) * (y - a.second) / (b.second - a.second) + a.first) {
      inside = !inside;
    }
  }
  return inside;
}

void orient_for_nonzero(std::vector<Contour>& contours) {
  std::vector<Polygon> polygons;
  polygons.reserve(contours.size());
  for (const Contour& contour : contours) {
    polygons.push_back(flatten(contour));
  }
  for (std::size_t index = 0; index < contours.size(); ++index) {
    int depth = 0;
    const Point& probe = contours[index].front();
    for (std::size_t other = 0; other < contours.size(); ++other) {
      if (other != index && contains(polygons[other], probe.x, probe.y)) {
        ++depth;
      }
    }
    const bool clockwise = signed_area(polygons[index]) < 0;
    if (clockwise != (depth % 2 == 0)) {
      std::reverse(contours[index].begin(), contours[index].end());
      rotate_to_on_curve(contours[index]);
    }
  }
}

bool fits_int16(int value) {
  return value >= std::numeric_limits<std::int16_t>::min() && value <= std::numeric_limits<std::int16_t>::max();
}

bool build_glyph(const SwfGlyph& swf_glyph, int scale, Glyph& out, std::string& message) {
  std::vector<RawContour> raw = trace_contours(swf_glyph);
  std::size_t total = 0;
  for (const RawContour& source : raw) {
    Contour contour;
    contour.reserve(source.size());
    for (const RawPoint& point : source) {
      Point scaled;
      scaled.on = point.on;
      if (!scale_coordinate(point.x, scale, scaled.x) || !scale_coordinate(-point.y, scale, scaled.y)) {
        message = "a glyph coordinate does not fit a TrueType glyph";
        return false;
      }
      contour.push_back(scaled);
    }
    total += contour.size();
    out.contours.push_back(std::move(contour));
  }
  if (total > std::numeric_limits<std::uint16_t>::max()) {
    message = "a glyph has more points than a TrueType glyph can hold";
    return false;
  }
  if (out.contours.empty()) {
    return true;
  }
  orient_for_nonzero(out.contours);
  out.x_min = out.y_min = std::numeric_limits<int>::max();
  out.x_max = out.y_max = std::numeric_limits<int>::min();
  for (const Contour& contour : out.contours) {
    for (const Point& point : contour) {
      out.x_min = std::min(out.x_min, point.x);
      out.x_max = std::max(out.x_max, point.x);
      out.y_min = std::min(out.y_min, point.y);
      out.y_max = std::max(out.y_max, point.y);
    }
  }
  Be glyf;
  glyf.i16(static_cast<int>(out.contours.size()));
  glyf.i16(out.x_min);
  glyf.i16(out.y_min);
  glyf.i16(out.x_max);
  glyf.i16(out.y_max);
  std::size_t end = 0;
  for (const Contour& contour : out.contours) {
    end += contour.size();
    glyf.u16(static_cast<std::uint32_t>(end - 1));
  }
  glyf.u16(0);
  for (const Contour& contour : out.contours) {
    for (const Point& point : contour) {
      glyf.u8(point.on ? 1u : 0u);
    }
  }
  int previous_x = 0;
  for (const Contour& contour : out.contours) {
    for (const Point& point : contour) {
      if (!fits_int16(point.x - previous_x)) {
        message = "a glyph coordinate does not fit a TrueType glyph";
        return false;
      }
      glyf.i16(point.x - previous_x);
      previous_x = point.x;
    }
  }
  int previous_y = 0;
  for (const Contour& contour : out.contours) {
    for (const Point& point : contour) {
      if (!fits_int16(point.y - previous_y)) {
        message = "a glyph coordinate does not fit a TrueType glyph";
        return false;
      }
      glyf.i16(point.y - previous_y);
      previous_y = point.y;
    }
  }
  glyf.pad4();
  out.data = std::move(glyf.bytes);
  return true;
}

std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
  std::uint32_t sum = 0;
  for (std::size_t index = 0; index < bytes.size(); index += 4) {
    std::uint32_t word = 0;
    for (std::size_t part = 0; part < 4; ++part) {
      word = (word << 8) | (index + part < bytes.size() ? bytes[index + part] : 0u);
    }
    sum += word;
  }
  return sum;
}

unsigned log2_floor(std::size_t value) {
  unsigned result = 0;
  while ((static_cast<std::size_t>(2) << result) <= value) {
    ++result;
  }
  return result;
}

std::string ascii(const std::string& text) {
  std::string out;
  for (const char ch : text) {
    out.push_back(static_cast<unsigned char>(ch) >= 0x20 && static_cast<unsigned char>(ch) < 0x7F ? ch : '_');
  }
  return out;
}

std::vector<std::uint8_t> name_table(const std::string& family, const std::string& style) {
  std::string postscript;
  for (const char ch : family + "-" + style) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '-') {
      postscript.push_back(ch);
    }
  }
  const std::array<std::pair<unsigned, std::string>, 4> strings{{
      {1, family},
      {2, style},
      {4, style == "Regular" ? family : family + " " + style},
      {6, postscript},
  }};
  Be table;
  table.u16(0);
  table.u16(static_cast<std::uint32_t>(strings.size()));
  table.u16(static_cast<std::uint32_t>(6 + 12 * strings.size()));
  std::size_t offset = 0;
  for (const auto& [id, text] : strings) {
    table.u16(3);
    table.u16(1);
    table.u16(0x409);
    table.u16(id);
    table.u16(static_cast<std::uint32_t>(text.size() * 2));
    table.u16(static_cast<std::uint32_t>(offset));
    offset += text.size() * 2;
  }
  for (const auto& entry : strings) {
    for (const char ch : entry.second) {
      table.u16(static_cast<unsigned char>(ch));
    }
  }
  return table.bytes;
}

}  // namespace

bool build_font_ttf(const SwfFont& font, std::vector<std::uint8_t>& out, std::string& message) {
  out.clear();
  message.clear();
  const std::size_t count = font.glyph_count;
  if (count == 0 || font.glyphs.size() != count) {
    message = "the font carries no glyph outlines";
    return false;
  }
  if (font.glyph_codes.size() != count || font.advances.size() != count) {
    message = "the font has no code table or no layout (DefineFont 1 and fonts without an advance table cannot be exported)";
    return false;
  }
  if ((font.flags & 0x40u) != 0) {
    message = "a ShiftJIS code table is not Unicode and is not exported";
    return false;
  }
  if (count > kMaxGlyphs) {
    message = "the font has too many glyphs for a TrueType font";
    return false;
  }
  const int scale = font.define_font3 ? 10 : 1;
  const int units_per_em = static_cast<int>(font.units_per_em) / scale;
  const std::size_t glyph_total = count + 1;

  std::vector<Glyph> glyphs(glyph_total);
  std::vector<int> advances(glyph_total, units_per_em / 2);
  for (std::size_t index = 0; index < count; ++index) {
    if (!build_glyph(font.glyphs[index], scale, glyphs[index + 1], message)) {
      return false;
    }
    const double raw = std::max(0.0, std::round(font.advances[index] * static_cast<double>(font.units_per_em)));
    int advance = 0;
    if (!scale_coordinate(static_cast<std::int64_t>(raw), scale, advance)) {
      message = "a glyph advance does not fit a TrueType font";
      return false;
    }
    advances[index + 1] = advance;
  }

  int font_x_min = 0;
  int font_y_min = 0;
  int font_x_max = 0;
  int font_y_max = 0;
  int advance_max = 0;
  int min_lsb = 0;
  int min_rsb = 0;
  int x_extent = 0;
  std::uint32_t advance_sum = 0;
  std::size_t max_points = 0;
  std::size_t max_contours = 0;
  bool first_box = true;
  Be glyf;
  Be loca;
  Be hmtx;
  for (std::size_t index = 0; index < glyph_total; ++index) {
    const Glyph& glyph = glyphs[index];
    loca.u32(static_cast<std::uint32_t>(glyf.bytes.size()));
    glyf.append(glyph.data);
    const int lsb = glyph.contours.empty() ? 0 : glyph.x_min;
    hmtx.u16(static_cast<std::uint32_t>(advances[index]));
    hmtx.i16(lsb);
    advance_max = std::max(advance_max, advances[index]);
    advance_sum += static_cast<std::uint32_t>(advances[index]);
    if (glyph.contours.empty()) {
      continue;
    }
    const int rsb = advances[index] - glyph.x_max;
    if (!fits_int16(rsb) || !fits_int16(lsb + (glyph.x_max - glyph.x_min))) {
      message = "a glyph extent does not fit a TrueType font";
      return false;
    }
    if (first_box) {
      font_x_min = glyph.x_min;
      font_y_min = glyph.y_min;
      font_x_max = glyph.x_max;
      font_y_max = glyph.y_max;
      min_lsb = lsb;
      min_rsb = rsb;
      x_extent = lsb + (glyph.x_max - glyph.x_min);
      first_box = false;
    } else {
      font_x_min = std::min(font_x_min, glyph.x_min);
      font_y_min = std::min(font_y_min, glyph.y_min);
      font_x_max = std::max(font_x_max, glyph.x_max);
      font_y_max = std::max(font_y_max, glyph.y_max);
      min_lsb = std::min(min_lsb, lsb);
      min_rsb = std::min(min_rsb, rsb);
      x_extent = std::max(x_extent, lsb + (glyph.x_max - glyph.x_min));
    }
    std::size_t points = 0;
    for (const Contour& contour : glyph.contours) {
      points += contour.size();
    }
    max_points = std::max(max_points, points);
    max_contours = std::max(max_contours, glyph.contours.size());
  }
  loca.u32(static_cast<std::uint32_t>(glyf.bytes.size()));

  std::map<std::uint16_t, std::uint16_t> glyph_of_code;
  for (std::size_t index = 0; index < count; ++index) {
    if (font.glyph_codes[index] != 0xFFFF) {
      glyph_of_code.emplace(font.glyph_codes[index], static_cast<std::uint16_t>(index + 1));
    }
  }

  struct Segment {
    std::uint16_t start;
    std::uint16_t end;
    std::uint16_t first_glyph;
  };
  std::vector<Segment> segments;
  for (const auto& [code, glyph] : glyph_of_code) {
    if (!segments.empty() && segments.back().end + 1 == code &&
        segments.back().first_glyph + (code - segments.back().start) == glyph) {
      segments.back().end = code;
    } else {
      segments.push_back(Segment{code, code, glyph});
    }
  }
  segments.push_back(Segment{0xFFFF, 0xFFFF, 0});
  if (16 + 8 * segments.size() > 0xFFFF) {
    message = "the code table needs more cmap segments than a format 4 subtable holds";
    return false;
  }
  const std::size_t segment_count = segments.size();
  const unsigned segment_log = log2_floor(segment_count);
  Be cmap;
  cmap.u16(0);
  cmap.u16(2);
  cmap.u16(0);
  cmap.u16(3);
  cmap.u32(20);
  cmap.u16(3);
  cmap.u16(1);
  cmap.u32(20);
  cmap.u16(4);
  cmap.u16(static_cast<std::uint32_t>(16 + 8 * segment_count));
  cmap.u16(0);
  cmap.u16(static_cast<std::uint32_t>(segment_count * 2));
  cmap.u16(2u << segment_log);
  cmap.u16(segment_log);
  cmap.u16(static_cast<std::uint32_t>(segment_count * 2 - (2u << segment_log)));
  for (const Segment& segment : segments) {
    cmap.u16(segment.end);
  }
  cmap.u16(0);
  for (const Segment& segment : segments) {
    cmap.u16(segment.start);
  }
  for (std::size_t index = 0; index < segment_count; ++index) {
    cmap.u16(index + 1 == segment_count ? 1u : static_cast<std::uint16_t>(segments[index].first_glyph - segments[index].start));
  }
  for (std::size_t index = 0; index < segment_count; ++index) {
    cmap.u16(0);
  }

  std::map<std::uint32_t, int> kern_pairs;
  for (const SwfKerning& pair : font.kerning) {
    const auto left = glyph_of_code.find(pair.code1);
    const auto right = glyph_of_code.find(pair.code2);
    int value = 0;
    if (left == glyph_of_code.end() || right == glyph_of_code.end() ||
        !scale_coordinate(pair.adjustment, scale, value) || value == 0) {
      continue;
    }
    kern_pairs.emplace((static_cast<std::uint32_t>(left->second) << 16) | right->second, value);
  }
  while (kern_pairs.size() > kMaxKernPairs) {
    kern_pairs.erase(std::prev(kern_pairs.end()));
  }
  Be kern;
  if (!kern_pairs.empty()) {
    const unsigned pair_log = log2_floor(kern_pairs.size());
    kern.u16(0);
    kern.u16(1);
    kern.u16(0);
    kern.u16(static_cast<std::uint32_t>(14 + 6 * kern_pairs.size()));
    kern.u16(1);
    kern.u16(static_cast<std::uint32_t>(kern_pairs.size()));
    kern.u16(6u << pair_log);
    kern.u16(pair_log);
    kern.u16(static_cast<std::uint32_t>(6 * kern_pairs.size() - (6u << pair_log)));
    for (const auto& [key, value] : kern_pairs) {
      kern.u16(key >> 16);
      kern.u16(key & 0xFFFFu);
      kern.i16(value);
    }
  }

  const bool bold = (font.flags & 0x01u) != 0;
  const bool italic = (font.flags & 0x02u) != 0;
  const std::string style = bold ? (italic ? "Bold Italic" : "Bold") : (italic ? "Italic" : "Regular");
  std::string trimmed_name = font.name;
  while (!trimmed_name.empty() && trimmed_name.back() == '\0') trimmed_name.pop_back();
  const std::string family = trimmed_name.empty() ? "SWF Font " + std::to_string(font.font_id) : ascii(trimmed_name);
  int ascent = 0;
  int descent = 0;
  int leading = 0;
  if (!scale_coordinate(font.ascent, scale, ascent) || !scale_coordinate(font.descent, scale, descent) ||
      !scale_coordinate(std::max<int>(font.leading, 0), scale, leading)) {
    message = "a font metric does not fit a TrueType font";
    return false;
  }
  const std::uint16_t first_code = glyph_of_code.empty() ? 0 : glyph_of_code.begin()->first;
  const std::uint16_t last_code = glyph_of_code.empty() ? 0 : glyph_of_code.rbegin()->first;

  Be head;
  head.u32(0x00010000);
  head.u32(0x00010000);
  head.u32(0);
  head.u32(0x5F0F3CF5);
  head.u16(1);
  head.u16(static_cast<std::uint32_t>(units_per_em));
  head.u32(0);
  head.u32(0);
  head.u32(0);
  head.u32(0);
  head.i16(font_x_min);
  head.i16(font_y_min);
  head.i16(font_x_max);
  head.i16(font_y_max);
  head.u16((bold ? 1u : 0u) | (italic ? 2u : 0u));
  head.u16(8);
  head.i16(2);
  head.i16(1);
  head.i16(0);

  Be hhea;
  hhea.u32(0x00010000);
  hhea.i16(ascent);
  hhea.i16(-descent);
  hhea.i16(leading);
  hhea.u16(static_cast<std::uint32_t>(advance_max));
  hhea.i16(min_lsb);
  hhea.i16(min_rsb);
  hhea.i16(x_extent);
  hhea.i16(1);
  hhea.i16(0);
  hhea.i16(0);
  for (int index = 0; index < 4; ++index) {
    hhea.i16(0);
  }
  hhea.i16(0);
  hhea.u16(static_cast<std::uint32_t>(glyph_total));

  Be maxp;
  maxp.u32(0x00010000);
  maxp.u16(static_cast<std::uint32_t>(glyph_total));
  maxp.u16(static_cast<std::uint32_t>(max_points));
  maxp.u16(static_cast<std::uint32_t>(max_contours));
  maxp.u16(0);
  maxp.u16(0);
  maxp.u16(1);
  for (int index = 0; index < 8; ++index) {
    maxp.u16(0);
  }

  Be os2;
  os2.u16(4);
  os2.i16(static_cast<int>(advance_sum / static_cast<std::uint32_t>(glyph_total)));
  os2.u16(bold ? 700 : 400);
  os2.u16(5);
  os2.u16(0);
  const int em = units_per_em;
  for (const int value : {em * 65 / 100, em * 70 / 100, 0, em * 14 / 100, em * 65 / 100, em * 70 / 100, 0,
                          em * 48 / 100, em * 5 / 100, em * 26 / 100}) {
    os2.i16(value);
  }
  os2.i16(0);
  for (int index = 0; index < 10; ++index) {
    os2.u8(0);
  }
  os2.u32(1);
  os2.u32(0);
  os2.u32(0);
  os2.u32(0);
  os2.tag("SWF ");
  os2.u16(0x80u | (italic ? 1u : 0u) | (bold ? 0x20u : 0u) | (!bold && !italic ? 0x40u : 0u));
  os2.u16(first_code);
  os2.u16(last_code);
  os2.i16(ascent);
  os2.i16(-descent);
  os2.i16(leading);
  os2.u16(static_cast<std::uint32_t>(std::max(ascent, font_y_max)));
  os2.u16(static_cast<std::uint32_t>(std::max(descent, -font_y_min)));
  os2.u32(1);
  os2.u32(0);
  os2.i16(0);
  os2.i16(0);
  os2.u16(0);
  os2.u16(0x20);
  os2.u16(kern_pairs.empty() ? 1 : 2);

  Be post;
  post.u32(0x00030000);
  post.u32(0);
  post.i16(-em * 10 / 100);
  post.i16(em * 5 / 100);
  post.u32(0);
  post.u32(0);
  post.u32(0);
  post.u32(0);
  post.u32(0);

  std::vector<std::pair<std::array<char, 4>, std::vector<std::uint8_t>>> tables;
  const auto add = [&](const char (&name)[5], const std::vector<std::uint8_t>& bytes) {
    tables.push_back({{name[0], name[1], name[2], name[3]}, bytes});
  };
  add("OS/2", os2.bytes);
  add("cmap", cmap.bytes);
  add("glyf", glyf.bytes);
  add("head", head.bytes);
  add("hhea", hhea.bytes);
  add("hmtx", hmtx.bytes);
  if (!kern.bytes.empty()) {
    add("kern", kern.bytes);
  }
  add("loca", loca.bytes);
  add("maxp", maxp.bytes);
  add("name", name_table(family, style));
  add("post", post.bytes);

  const std::size_t table_count = tables.size();
  const unsigned table_log = log2_floor(table_count);
  Be file;
  file.u32(0x00010000);
  file.u16(static_cast<std::uint32_t>(table_count));
  file.u16(16u << table_log);
  file.u16(table_log);
  file.u16(static_cast<std::uint32_t>(table_count * 16 - (16u << table_log)));
  std::size_t offset = 12 + 16 * table_count;
  std::size_t head_offset = 0;
  for (const auto& [name, bytes] : tables) {
    for (const char ch : name) {
      file.u8(static_cast<unsigned char>(ch));
    }
    file.u32(checksum(bytes));
    file.u32(static_cast<std::uint32_t>(offset));
    file.u32(static_cast<std::uint32_t>(bytes.size()));
    if (std::string(name.data(), 4) == "head") {
      head_offset = offset;
    }
    offset += (bytes.size() + 3) / 4 * 4;
  }
  for (const auto& entry : tables) {
    file.append(entry.second);
    file.pad4();
  }
  const std::uint32_t adjustment = 0xB1B0AFBAu - checksum(file.bytes);
  for (int index = 0; index < 4; ++index) {
    file.bytes[head_offset + 8 + static_cast<std::size_t>(index)] =
        static_cast<std::uint8_t>(adjustment >> (24 - 8 * index));
  }
  out = std::move(file.bytes);
  return true;
}

}  // namespace fo4::swf
