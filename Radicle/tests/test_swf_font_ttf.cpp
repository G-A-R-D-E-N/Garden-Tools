#include "fo4/c_api.h"
#include "fo4/c_api_swf.h"
#include "swf_movie_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct SwfDeleter {
  void operator()(radicle_swf* swf) const { radicle_swf_destroy(swf); }
};

using SwfHandle = std::unique_ptr<radicle_swf, SwfDeleter>;

struct Move {
  int x;
  int y;
};
struct Line {
  int dx;
  int dy;
};
struct Curve {
  int cx;
  int cy;
  int ax;
  int ay;
};

constexpr unsigned kBits = 14;

void signed_pair(swf_test::SwfBuilder& b, int x, int y) {
  b.push_bits(static_cast<std::uint32_t>(x), kBits);
  b.push_bits(static_cast<std::uint32_t>(y), kBits);
}

void glyph_start(swf_test::SwfBuilder& b) { b.u8(0x10); }

void glyph_move(swf_test::SwfBuilder& b, Move move, bool first) {
  b.push_bits(0, 1);
  b.push_bits(first ? 0x05 : 0x01, 5);
  b.push_bits(kBits, 5);
  signed_pair(b, move.x, move.y);
  if (first) {
    b.push_bits(1, 1);
  }
}

void glyph_line(swf_test::SwfBuilder& b, Line line) {
  b.push_bits(3, 2);
  b.push_bits(kBits - 2, 4);
  b.push_bits(1, 1);
  signed_pair(b, line.dx, line.dy);
}

void glyph_curve(swf_test::SwfBuilder& b, Curve curve) {
  b.push_bits(2, 2);
  b.push_bits(kBits - 2, 4);
  signed_pair(b, curve.cx, curve.cy);
  signed_pair(b, curve.ax, curve.ay);
}

std::vector<std::byte> glyph_end(swf_test::SwfBuilder& b) {
  b.push_bits(0, 6);
  b.align();
  return b.body;
}

std::vector<std::byte> square_with_hole() {
  swf_test::SwfBuilder b;
  glyph_start(b);
  glyph_move(b, {1000, 1000}, true);
  glyph_line(b, {4000, 0});
  glyph_line(b, {0, 4000});
  glyph_line(b, {-4000, 0});
  glyph_line(b, {0, -4000});
  glyph_move(b, {2000, 2000}, false);
  glyph_line(b, {2000, 0});
  glyph_line(b, {0, 2000});
  glyph_line(b, {-2000, 0});
  glyph_line(b, {0, -2000});
  return glyph_end(b);
}

std::vector<std::byte> blank() {
  swf_test::SwfBuilder b;
  glyph_start(b);
  return glyph_end(b);
}

std::vector<std::byte> arch() {
  swf_test::SwfBuilder b;
  glyph_start(b);
  glyph_move(b, {0, 0}, true);
  glyph_curve(b, {1000, -1000, 1000, 1000});
  glyph_line(b, {-2000, 0});
  return glyph_end(b);
}

constexpr std::uint8_t kLayout = 0x80;
constexpr std::uint8_t kWideCodes = 0x04;
constexpr std::uint8_t kBold = 0x01;
constexpr std::uint8_t kShiftJis = 0x40;

std::vector<std::byte> font_movie(std::uint16_t tag, std::uint8_t flags, const std::vector<std::vector<std::byte>>& glyphs,
                                  const std::vector<std::uint32_t>& codes, const std::vector<std::uint32_t>& advances) {
  swf_test::SwfBuilder p;
  p.u16(5);
  if (tag != 10) {
    p.u8(flags);
    p.u8(1);
    p.sized_text(std::string_view("TestFont\0", 9));
    p.u16(static_cast<std::uint32_t>(glyphs.size()));
  }
  std::uint32_t offset = 2 * static_cast<std::uint32_t>(glyphs.size()) + (tag != 10 ? 2 : 0);
  for (const auto& glyph : glyphs) {
    p.u16(offset);
    offset += static_cast<std::uint32_t>(glyph.size());
  }
  if (tag != 10) {
    p.u16(offset);
  }
  for (const auto& glyph : glyphs) {
    p.bytes(glyph);
  }
  if (tag != 10) {
    for (const std::uint32_t code : codes) {
      p.u16(code);
    }
  }
  if (tag != 10 && (flags & kLayout) != 0) {
    p.u16(16000);
    p.u16(4000);
    p.u16(200);
    for (const std::uint32_t advance : advances) {
      p.u16(advance);
    }
    for (std::size_t glyph = 0; glyph < glyphs.size(); ++glyph) {
      p.rect(0, 0, 0, 0, 0);
    }
    p.u16(1);
    p.u16(0x41);
    p.u16(0x42);
    p.u16(static_cast<std::uint32_t>(-205) & 0xFFFFu);
  }
  swf_test::SwfBuilder movie;
  movie.rect(0, 11000, 0, 8000, 15);
  movie.u16(0x0C00);
  movie.u16(1);
  movie.tag(tag, p.body, true);
  movie.tag(0);
  return swf_test::wrap_body(movie.body, 10, false, false);
}

std::vector<std::byte> font3_movie(std::uint8_t flags) {
  return font_movie(75, flags, {square_with_hole(), blank(), arch()}, {0x41, 0x20, 0x42}, {3000, 1500, 2501});
}

std::vector<std::byte> two_wide_triangles(int first_x, int second_x) {
  constexpr unsigned wide = 17;
  swf_test::SwfBuilder b;
  glyph_start(b);
  for (const int x : {first_x, second_x}) {
    b.push_bits(0, 1);
    b.push_bits(x == first_x ? 0x05 : 0x01, 5);
    b.push_bits(wide, 5);
    b.push_bits(static_cast<std::uint32_t>(x), wide);
    b.push_bits(0, wide);
    if (x == first_x) {
      b.push_bits(1, 1);
    }
    b.push_bits(3, 2);
    b.push_bits(wide - 2, 4);
    b.push_bits(1, 1);
    b.push_bits(1000, wide);
    b.push_bits(0, wide);
    b.push_bits(3, 2);
    b.push_bits(wide - 2, 4);
    b.push_bits(1, 1);
    b.push_bits(0, wide);
    b.push_bits(1000, wide);
  }
  return glyph_end(b);
}

std::vector<std::byte> wide_triangle(int x, int dx) {
  constexpr unsigned wide = 17;
  swf_test::SwfBuilder b;
  glyph_start(b);
  b.push_bits(0, 1);
  b.push_bits(0x05, 5);
  b.push_bits(wide, 5);
  b.push_bits(static_cast<std::uint32_t>(x), wide);
  b.push_bits(0, wide);
  b.push_bits(1, 1);
  b.push_bits(3, 2);
  b.push_bits(wide - 2, 4);
  b.push_bits(1, 1);
  b.push_bits(static_cast<std::uint32_t>(dx), wide);
  b.push_bits(0, wide);
  b.push_bits(3, 2);
  b.push_bits(wide - 2, 4);
  b.push_bits(1, 1);
  b.push_bits(0, wide);
  b.push_bits(1000, wide);
  return glyph_end(b);
}

SwfHandle open(const std::vector<std::byte>& bytes) {
  radicle_swf* raw = nullptr;
  EXPECT_EQ(radicle_swf_open_bytes(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &raw),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  return SwfHandle(raw);
}

std::vector<std::uint8_t> export_ttf(const radicle_swf* swf, size_t tag) {
  std::uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_swf_font_ttf(swf, tag, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::uint8_t> out(bytes, bytes + size);
  radicle_free_bytes(bytes);
  return out;
}

struct Outline {
  struct Point {
    int x;
    int y;
    bool on;
  };
  std::vector<std::vector<Point>> contours;
  int x_min{0};
  int y_min{0};
  int x_max{0};
  int y_max{0};
};

class Sfnt {
public:
  explicit Sfnt(std::vector<std::uint8_t> bytes) : data_(std::move(bytes)) {
    const unsigned count = u16(4);
    for (unsigned index = 0; index < count; ++index) {
      const std::size_t entry = 12 + 16 * index;
      const std::string tag(reinterpret_cast<const char*>(&data_[entry]), 4);
      tables_[tag] = {u32(entry + 8), u32(entry + 12)};
      declared_[tag] = u32(entry + 4);
    }
  }

  bool checksums_valid() const {
    if (sum(0, data_.size(), false) != 0xB1B0AFBAu) {
      return false;
    }
    for (const auto& [tag, span] : tables_) {
      if (span.first + span.second > data_.size() ||
          sum(span.first, span.second, tag == "head") != declared_.at(tag)) {
        return false;
      }
    }
    return true;
  }

  bool has(const std::string& tag) const { return tables_.count(tag) != 0; }
  std::size_t at(const std::string& tag) const { return tables_.at(tag).first; }
  std::size_t length(const std::string& tag) const { return tables_.at(tag).second; }

  unsigned u16(std::size_t offset) const { return (data_.at(offset) << 8) | data_.at(offset + 1); }
  int i16(std::size_t offset) const { return static_cast<std::int16_t>(u16(offset)); }
  std::uint32_t u32(std::size_t offset) const { return (u16(offset) << 16) | u16(offset + 2); }

  unsigned glyph_total() const { return u16(at("maxp") + 4); }

  unsigned glyph_of(unsigned code) const {
    const std::size_t cmap = at("cmap");
    std::size_t sub = 0;
    for (unsigned index = 0; index < u16(cmap + 2); ++index) {
      if (u16(cmap + 4 + 8 * index) == 3 && u16(cmap + 6 + 8 * index) == 1) {
        sub = cmap + u32(cmap + 8 + 8 * index);
      }
    }
    EXPECT_NE(sub, 0u);
    const unsigned segments = u16(sub + 6) / 2;
    const std::size_t ends = sub + 14;
    const std::size_t starts = ends + 2 * segments + 2;
    const std::size_t deltas = starts + 2 * segments;
    const std::size_t ranges = deltas + 2 * segments;
    for (unsigned index = 0; index < segments; ++index) {
      if (code > u16(ends + 2 * index) || code < u16(starts + 2 * index)) {
        continue;
      }
      EXPECT_EQ(u16(ranges + 2 * index), 0u);
      return (code + u16(deltas + 2 * index)) & 0xFFFFu;
    }
    return 0;
  }

  unsigned advance(unsigned glyph) const { return u16(at("hmtx") + 4 * glyph); }
  int bearing(unsigned glyph) const { return i16(at("hmtx") + 4 * glyph + 2); }

  int kerning(unsigned left, unsigned right) const {
    const std::size_t sub = at("kern") + 4;
    const unsigned pairs = u16(sub + 6);
    for (unsigned index = 0; index < pairs; ++index) {
      const std::size_t entry = sub + 14 + 6 * index;
      if (u16(entry) == left && u16(entry + 2) == right) {
        return i16(entry + 4);
      }
    }
    return 0;
  }

  std::string name(unsigned id) const {
    const std::size_t table = at("name");
    for (unsigned index = 0; index < u16(table + 2); ++index) {
      const std::size_t record = table + 6 + 12 * index;
      if (u16(record + 6) != id) {
        continue;
      }
      std::string text;
      for (unsigned part = 0; part < u16(record + 8); part += 2) {
        text.push_back(static_cast<char>(u16(table + u16(table + 4) + u16(record + 10) + part)));
      }
      return text;
    }
    return {};
  }

  Outline outline(unsigned glyph) const {
    Outline out;
    const bool long_loca = i16(at("head") + 50) == 1;
    const std::size_t first = long_loca ? u32(at("loca") + 4 * glyph) : 2u * u16(at("loca") + 2 * glyph);
    const std::size_t next =
        long_loca ? u32(at("loca") + 4 * glyph + 4) : 2u * u16(at("loca") + 2 * glyph + 2);
    if (next == first) {
      return out;
    }
    std::size_t cursor = at("glyf") + first;
    const int contours = i16(cursor);
    out.x_min = i16(cursor + 2);
    out.y_min = i16(cursor + 4);
    out.x_max = i16(cursor + 6);
    out.y_max = i16(cursor + 8);
    cursor += 10;
    std::vector<unsigned> ends;
    for (int index = 0; index < contours; ++index) {
      ends.push_back(u16(cursor));
      cursor += 2;
    }
    cursor += 2 + u16(cursor);
    const unsigned points = ends.empty() ? 0 : ends.back() + 1;
    std::vector<unsigned> flags;
    while (flags.size() < points) {
      const unsigned flag = data_.at(cursor++);
      flags.push_back(flag);
      if ((flag & 8u) != 0) {
        for (unsigned repeat = data_.at(cursor++); repeat != 0; --repeat) {
          flags.push_back(flag);
        }
      }
    }
    std::vector<int> xs(points);
    std::vector<int> ys(points);
    for (const bool horizontal : {true, false}) {
      const unsigned short_bit = horizontal ? 2u : 4u;
      const unsigned same_bit = horizontal ? 16u : 32u;
      int value = 0;
      for (unsigned index = 0; index < points; ++index) {
        if ((flags[index] & short_bit) != 0) {
          const int delta = data_.at(cursor++);
          value += (flags[index] & same_bit) != 0 ? delta : -delta;
        } else if ((flags[index] & same_bit) == 0) {
          value += i16(cursor);
          cursor += 2;
        }
        (horizontal ? xs : ys)[index] = value;
      }
    }
    unsigned begin = 0;
    for (const unsigned end : ends) {
      std::vector<Outline::Point> contour;
      for (unsigned index = begin; index <= end; ++index) {
        contour.push_back({xs[index], ys[index], (flags[index] & 1u) != 0});
      }
      out.contours.push_back(contour);
      begin = end + 1;
    }
    return out;
  }

private:
  std::uint32_t sum(std::size_t offset, std::size_t size, bool zero_adjustment) const {
    std::uint32_t total = 0;
    for (std::size_t index = 0; index < size; index += 4) {
      std::uint32_t word = 0;
      for (std::size_t part = 0; part < 4; ++part) {
        const std::size_t position = offset + index + part;
        const bool in_adjustment = zero_adjustment && index + part >= 8 && index + part < 12;
        word = (word << 8) | (position < data_.size() && !in_adjustment ? data_[position] : 0u);
      }
      total += word;
    }
    return total;
  }

  std::vector<std::uint8_t> data_;
  std::map<std::string, std::pair<std::size_t, std::size_t>> tables_;
  std::map<std::string, std::uint32_t> declared_;
};

double area(const std::vector<Outline::Point>& contour) {
  double total = 0;
  for (std::size_t index = 0; index < contour.size(); ++index) {
    const auto& a = contour[index];
    const auto& b = contour[(index + 1) % contour.size()];
    total += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
  }
  return total / 2;
}

}  // namespace

TEST(SwfFontTtf, MetricsGettersReadTheLayoutTable) {
  const SwfHandle swf = open(font3_movie(kLayout | kWideCodes | kBold));
  ASSERT_NE(swf, nullptr);

  radicle_swf_font_metrics metrics{};
  ASSERT_EQ(radicle_swf_font_metrics_at(swf.get(), 0, &metrics), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(metrics.units_per_em, 20480u);
  EXPECT_EQ(metrics.ascent, 16000);
  EXPECT_EQ(metrics.descent, 4000);
  EXPECT_EQ(metrics.leading, 200);
  EXPECT_EQ(metrics.has_layout, 1);
  EXPECT_EQ(metrics.glyph_count, 3u);
  EXPECT_EQ(metrics.kerning_count, 1u);

  const unsigned encoded[] = {3000, 1500, 2501};
  for (size_t glyph = 0; glyph < 3; ++glyph) {
    double advance = 0;
    ASSERT_EQ(radicle_swf_font_advance_at(swf.get(), 0, glyph, &advance), RADICLE_STATUS_OK);
    EXPECT_NEAR(advance * metrics.units_per_em, encoded[glyph], 1e-6);
  }
  double advance = 0;
  EXPECT_EQ(radicle_swf_font_advance_at(swf.get(), 0, 3, &advance), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_swf_font_kerning pair{};
  ASSERT_EQ(radicle_swf_font_kerning_at(swf.get(), 0, 0, &pair), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(pair.code1, 0x41);
  EXPECT_EQ(pair.code2, 0x42);
  EXPECT_EQ(pair.adjustment, -205);
  EXPECT_EQ(radicle_swf_font_kerning_at(swf.get(), 0, 1, &pair), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(SwfFontTtf, RefusesATagThatIsNotAFontAndNullArguments) {
  const SwfHandle swf = open(font3_movie(kLayout | kWideCodes));
  ASSERT_NE(swf, nullptr);

  radicle_swf_font_metrics metrics{};
  double advance = 0;
  radicle_swf_font_kerning pair{};
  std::uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_swf_font_metrics_at(swf.get(), 1, &metrics), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_font_advance_at(swf.get(), 1, 0, &advance), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_font_kerning_at(swf.get(), 1, 0, &pair), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_font_ttf(swf.get(), 1, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_swf_font_metrics_at(swf.get(), 9, &metrics), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_font_metrics_at(swf.get(), 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_font_ttf(swf.get(), 0, nullptr, &size), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_swf_font_ttf(nullptr, 0, &bytes, &size), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(SwfFontTtf, ASfntHasValidChecksumsTablesAndNames) {
  const SwfHandle swf = open(font3_movie(kLayout | kWideCodes | kBold));
  ASSERT_NE(swf, nullptr);
  const Sfnt font(export_ttf(swf.get(), 0));

  EXPECT_TRUE(font.checksums_valid());
  for (const char* tag : {"OS/2", "cmap", "glyf", "head", "hhea", "hmtx", "kern", "loca", "maxp", "name", "post"}) {
    EXPECT_TRUE(font.has(tag)) << tag;
  }
  EXPECT_EQ(font.u32(0), 0x00010000u);
  EXPECT_EQ(font.u32(font.at("head") + 12), 0x5F0F3CF5u);
  EXPECT_EQ(font.u16(font.at("head") + 18), 2048u);
  EXPECT_EQ(font.glyph_total(), 4u);
  EXPECT_EQ(font.name(1), "TestFont");
  EXPECT_EQ(font.name(2), "Bold");
  EXPECT_EQ(font.i16(font.at("hhea") + 4), 1600);
  EXPECT_EQ(font.i16(font.at("hhea") + 6), -400);
  EXPECT_EQ(font.i16(font.at("hhea") + 8), 20);
  EXPECT_EQ(font.u16(font.at("hhea") + 34), 4u);
  EXPECT_EQ(font.u16(font.at("OS/2") + 4), 700u);
}

TEST(SwfFontTtf, CmapAndHmtxMapEverySwfGlyphOneAfterTheNotdef) {
  const SwfHandle swf = open(font3_movie(kLayout | kWideCodes));
  ASSERT_NE(swf, nullptr);
  const Sfnt font(export_ttf(swf.get(), 0));

  EXPECT_EQ(font.glyph_of(0x41), 1u);
  EXPECT_EQ(font.glyph_of(0x20), 2u);
  EXPECT_EQ(font.glyph_of(0x42), 3u);
  EXPECT_EQ(font.glyph_of(0x43), 0u);
  EXPECT_EQ(font.glyph_of(0xFFFF), 0u);
  EXPECT_EQ(font.advance(1), 300u);
  EXPECT_EQ(font.advance(2), 150u);
  EXPECT_EQ(font.advance(3), 250u);
  EXPECT_EQ(font.bearing(1), 100);
  EXPECT_EQ(font.bearing(2), 0);
  EXPECT_EQ(font.kerning(1, 3), -21);
  EXPECT_EQ(font.kerning(3, 1), 0);
}

TEST(SwfFontTtf, OutlinesAreScaledFlippedAndWoundSoTheHoleStaysOpen) {
  const SwfHandle swf = open(font3_movie(kLayout | kWideCodes));
  ASSERT_NE(swf, nullptr);
  const Sfnt font(export_ttf(swf.get(), 0));

  const Outline notdef = font.outline(0);
  EXPECT_TRUE(notdef.contours.empty());

  const Outline box = font.outline(1);
  ASSERT_EQ(box.contours.size(), 2u);
  EXPECT_EQ(box.x_min, 100);
  EXPECT_EQ(box.x_max, 500);
  EXPECT_EQ(box.y_min, -500);
  EXPECT_EQ(box.y_max, -100);
  EXPECT_EQ(box.contours[0].size(), 4u);
  EXPECT_LT(area(box.contours[0]), 0);
  EXPECT_GT(area(box.contours[1]), 0);
  EXPECT_EQ(box.contours[0][0].x, 100);
  EXPECT_EQ(box.contours[0][0].y, -100);

  EXPECT_TRUE(font.outline(2).contours.empty());

  const Outline arch = font.outline(3);
  ASSERT_EQ(arch.contours.size(), 1u);
  ASSERT_EQ(arch.contours[0].size(), 3u);
  EXPECT_TRUE(arch.contours[0][0].on);
  EXPECT_FALSE(arch.contours[0][1].on);
  EXPECT_TRUE(arch.contours[0][2].on);
  EXPECT_EQ(arch.contours[0][1].x, 100);
  EXPECT_EQ(arch.contours[0][1].y, 100);
  EXPECT_EQ(arch.contours[0][2].x, 200);
  EXPECT_LT(area(arch.contours[0]), 0);
}

TEST(SwfFontTtf, RefusesFontsThatCannotBecomeATrueTypeFont) {
  std::uint8_t* bytes = nullptr;
  size_t size = 0;

  const SwfHandle no_layout = open(font3_movie(kWideCodes));
  ASSERT_NE(no_layout, nullptr);
  radicle_swf_font_metrics metrics{};
  ASSERT_EQ(radicle_swf_font_metrics_at(no_layout.get(), 0, &metrics), RADICLE_STATUS_OK);
  EXPECT_EQ(metrics.has_layout, 0);
  EXPECT_EQ(metrics.glyph_count, 0u);
  EXPECT_EQ(metrics.kerning_count, 0u);
  EXPECT_EQ(metrics.units_per_em, 20480u);
  EXPECT_EQ(radicle_swf_font_ttf(no_layout.get(), 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);

  const SwfHandle shift_jis = open(font3_movie(kLayout | kWideCodes | kShiftJis));
  ASSERT_NE(shift_jis, nullptr);
  EXPECT_EQ(radicle_swf_font_ttf(shift_jis.get(), 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
}

TEST(SwfFontTtf, ADefineFont2KeepsItsOwnEmSquareAndScale) {
  const SwfHandle swf = open(font_movie(48, kLayout | kWideCodes, {square_with_hole(), blank(), arch()},
                                         {0x41, 0x20, 0x42}, {600, 300, 500}));
  ASSERT_NE(swf, nullptr);
  radicle_swf_font_metrics metrics{};
  ASSERT_EQ(radicle_swf_font_metrics_at(swf.get(), 0, &metrics), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(metrics.units_per_em, 1024u);

  const Sfnt font(export_ttf(swf.get(), 0));
  EXPECT_EQ(font.u16(font.at("head") + 18), 1024u);
  EXPECT_EQ(font.advance(1), 600u);
  EXPECT_EQ(font.i16(font.at("hhea") + 4), 16000);
  EXPECT_EQ(font.kerning(1, 3), -205);
  const Outline box = font.outline(1);
  EXPECT_EQ(box.x_min, 1000);
  EXPECT_EQ(box.x_max, 5000);
  EXPECT_EQ(box.y_min, -5000);
  EXPECT_EQ(box.y_max, -1000);

  ASSERT_EQ(box.contours.size(), 2u);
  std::vector<std::pair<int, int>> hole;
  for (const Outline::Point& point : box.contours[1]) {
    hole.emplace_back(point.x, point.y);
  }
  std::sort(hole.begin(), hole.end());
  const std::vector<std::pair<int, int>> expected{{2000, -4000}, {2000, -2000}, {4000, -4000}, {4000, -2000}};
  EXPECT_EQ(hole, expected);
}

TEST(SwfFontTtf, ADefineFontTagWithoutCodesOrLayoutIsRefused) {
  const SwfHandle swf = open(font_movie(10, 0, {square_with_hole()}, {}, {}));
  ASSERT_NE(swf, nullptr);
  std::uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_swf_font_ttf(swf.get(), 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes, nullptr);
}

TEST(SwfFontTtf, ADuplicateCodeKeepsTheFirstGlyph) {
  const SwfHandle swf = open(font_movie(75, kLayout | kWideCodes, {square_with_hole(), blank(), arch()},
                                         {0x41, 0x41, 0x42}, {3000, 1500, 2501}));
  ASSERT_NE(swf, nullptr);
  const Sfnt font(export_ttf(swf.get(), 0));
  EXPECT_EQ(font.glyph_of(0x41), 1u);
  EXPECT_EQ(font.glyph_of(0x42), 3u);
  EXPECT_EQ(font.glyph_of(0x20), 0u);
}

TEST(SwfFontTtf, RefusesGlyphsWhoseDeltasOrBearingsLeaveInt16) {
  std::uint8_t* bytes = nullptr;
  size_t size = 0;

  const SwfHandle wide_delta = open(font_movie(48, kLayout | kWideCodes, {wide_triangle(-30000, 60000)}, {0x41}, {600}));
  ASSERT_NE(wide_delta, nullptr);
  EXPECT_EQ(radicle_swf_font_ttf(wide_delta.get(), 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(std::string(radicle_last_error()).find("does not fit"), std::string::npos);

  const SwfHandle wide_bearing = open(font_movie(48, kLayout | kWideCodes, {wide_triangle(-30000, 1000)}, {0x41}, {30000}));
  ASSERT_NE(wide_bearing, nullptr);
  EXPECT_EQ(radicle_swf_font_ttf(wide_bearing.get(), 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(std::string(radicle_last_error()).find("does not fit"), std::string::npos);

  const SwfHandle cross_contour = open(font_movie(48, kLayout | kWideCodes, {two_wide_triangles(-30000, 29000)}, {0x41}, {600}));
  ASSERT_NE(cross_contour, nullptr);
  EXPECT_EQ(radicle_swf_font_ttf(cross_contour.get(), 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_NE(std::string(radicle_last_error()).find("does not fit"), std::string::npos);

  const SwfHandle fits = open(font_movie(48, kLayout | kWideCodes, {wide_triangle(-30000, 1000)}, {0x41}, {600}));
  ASSERT_NE(fits, nullptr);
  EXPECT_EQ(export_ttf(fits.get(), 0).empty(), false);
}
