#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::avm2 {

struct FontGlyph {
  std::uint16_t glyph_id{0};
  std::uint32_t code{0};
  /* Invariant: advance is an em fraction, so advance * font size is the pixel advance. */
  double advance{0.5};
};

struct FontResource {
  std::uint16_t character_id{0};
  std::string name;
  std::uint32_t units_per_em{1024};
  bool has_layout{false};
  std::vector<FontGlyph> glyphs;

  /* Requires glyphs in glyph order; returns the index of the glyph for a code or npos. */
  std::size_t glyph_index_for_code(std::uint32_t code) const;
};

class FontRegistry {
 public:
  void add(FontResource font);
  const FontResource* find(std::uint16_t character_id) const;
  const FontResource* find_by_name(std::string_view name) const;
  const FontResource* first_with_layout() const;
  std::size_t size() const noexcept { return fonts_.size(); }
  const std::vector<FontResource>& fonts() const noexcept { return fonts_; }

 private:
  std::vector<FontResource> fonts_;
};

}  // namespace fo4::avm2
