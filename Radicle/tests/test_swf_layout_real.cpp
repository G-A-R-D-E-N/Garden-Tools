#include "fo4/archive/ba2.hpp"
#include "fo4/swf/swf.hpp"
#include "fo4/swf/swf_layout.hpp"
#include "real_data_build.hpp"
#include "real_data_movies.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

class Fnv {
 public:
  void bytes(const void* data, std::size_t size) {
    const auto* raw = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
      hash_ ^= raw[index];
      hash_ *= 1099511628211ull;
    }
  }

  void number(std::uint64_t value) { bytes(&value, sizeof(value)); }

  void real(double value) { number(std::bit_cast<std::uint64_t>(value)); }

  void text(std::string_view value) {
    number(value.size());
    bytes(value.data(), value.size());
  }

  std::uint64_t value() const { return hash_; }

 private:
  std::uint64_t hash_{1469598103934665603ull};
};

void fold_text(Fnv& hash, const std::optional<fo4::swf::SwfLayoutText>& text) {
  hash.number(text ? 1u : 0u);
  if (!text) {
    return;
  }
  hash.number(text->font_id ? *text->font_id + 1u : 0u);
  hash.number(text->has_font_class ? 1u : 0u);
  hash.text(text->font_class);
  hash.number(text->font_height ? 1u : 0u);
  if (text->font_height) {
    hash.real(*text->font_height);
  }
  hash.number(text->color ? *text->color + 1ull : 0ull);
  hash.number(text->has_layout ? 1u : 0u);
  hash.number(text->align);
  hash.real(text->left_margin);
  hash.real(text->right_margin);
  hash.real(text->indent);
  hash.real(text->leading);
  hash.number((text->multiline ? 1u : 0u) | (text->word_wrap ? 2u : 0u) |
              (text->auto_size ? 4u : 0u) | (text->html ? 8u : 0u) | (text->border ? 16u : 0u));
  hash.text(text->variable);
  hash.text(text->text);
}

void fold_font(Fnv& hash, const fo4::swf::SwfLayoutFont& font) {
  hash.number(font.font_id);
  hash.number(font.version);
  hash.text(font.name);
  hash.number((font.has_style ? 1u : 0u) | (font.bold ? 2u : 0u) | (font.italic ? 4u : 0u) |
              (font.has_language ? 8u : 0u) | (font.has_glyph_count ? 16u : 0u) |
              (font.has_outlines ? 32u : 0u) | (font.has_metrics ? 64u : 0u));
  hash.number(font.language);
  hash.number(font.glyph_count);
  hash.number(font.ascent);
  hash.number(font.descent);
  hash.number(static_cast<std::uint16_t>(font.leading));
  hash.number(font.units_per_em);
  hash.number((font.has_codes ? 1u : 0u) | (font.has_font_name ? 2u : 0u) |
              (font.has_font_class ? 4u : 0u));
  hash.number(font.codes.size());
  for (const std::uint16_t code : font.codes) {
    hash.number(code);
  }
  hash.text(font.font_name);
  hash.text(font.font_copyright);
  hash.text(font.font_class);
}

void fold(Fnv& hash, const fo4::swf::SwfLayout& layout) {
  hash.real(layout.stage.width);
  hash.real(layout.stage.height);
  hash.real(layout.frame_rate);
  hash.number(layout.scripted ? 1u : 0u);
  hash.number(layout.fonts.size());
  for (const auto& font : layout.fonts) {
    fold_font(hash, font);
  }
  for (const auto& state : layout.states) {
    hash.text(state.label);
    hash.number(state.timeline_id);
    hash.number(state.frame);
    hash.number(state.row_count);
    for (std::size_t index = state.first_row; index < state.first_row + state.row_count; ++index) {
      const auto& row = layout.rows[index];
      hash.text(row.path);
      hash.text(row.parent_path);
      hash.number(row.depth);
      hash.number(row.character_id);
      hash.number(static_cast<std::uint64_t>(row.kind));
      hash.number((row.visible ? 1u : 0u) | (row.has_rect ? 2u : 0u));
      hash.real(row.world_transform.scale_x);
      hash.real(row.world_transform.rotate_skew0);
      hash.real(row.world_transform.rotate_skew1);
      hash.real(row.world_transform.scale_y);
      hash.real(row.world_transform.translate_x);
      hash.real(row.world_transform.translate_y);
      if (row.has_rect) {
        hash.real(row.rect.x);
        hash.real(row.rect.y);
        hash.real(row.rect.width);
        hash.real(row.rect.height);
      }
      hash.number(row.clip_depth ? *row.clip_depth + 1u : 0u);
      hash.number(row.clip_frame_count);
      hash.number(row.clip_first_frame);
      hash.number(row.clip_last_frame);
      hash.number(row.flags);
      fold_text(hash, row.text);
    }
  }
}

std::string hex(std::uint64_t value) {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
  return buffer;
}

bool is_lzma(const std::vector<std::byte>& bytes) {
  return bytes.size() >= 3 && bytes[0] == static_cast<std::byte>('Z') &&
         bytes[1] == static_cast<std::byte>('W') && bytes[2] == static_cast<std::byte>('S');
}

}  // namespace

// Regression: a movie refused without a reason, or a layout that differs between two builds of the same bytes, passed the gate.
TEST(SwfLayoutReal, EveryInterfaceMovieExportsALayoutOrItsReason) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder");
  const std::filesystem::path data_dir(dir);
  record_game_build(detect_game_build(data_dir));
  const auto archive_path = data_dir / "Fallout4 - Interface.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "no Fallout4 - Interface.ba2 in FO4_DATA_DIR";
  }
  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  std::size_t movies = 0;
  std::size_t exported = 0;
  std::size_t refused = 0;
  std::size_t lzma_movies = 0;
  std::size_t lzma_exported = 0;
  std::size_t fonts = 0;
  std::size_t font_outline_fonts = 0;
  std::size_t font_glyphs = 0;
  std::size_t fonts_with_codes = 0;
  std::size_t font_codes = 0;
  std::size_t fonts_with_name_tag = 0;
  std::size_t fonts_with_class = 0;
  std::size_t info_fonts = 0;
  std::size_t text_rows_with_font_class = 0;
  std::size_t reasonless_refusals = 0;
  std::size_t states = 0;
  std::size_t rows = 0;
  std::size_t rows_with_rect = 0;
  std::size_t scripted_movies = 0;
  std::size_t flagged_rows = 0;
  std::size_t duplicate_rows = 0;
  Fnv corpus;
  for (const auto& entry : archive.entries) {
    if (!is_movie(entry.path)) {
      continue;
    }
    ++movies;
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    const bool lzma = is_lzma(bytes);
    lzma_movies += lzma ? 1u : 0u;
    const auto read = fo4::swf::read_swf(bytes);
    fo4::swf::SwfLayout layout;
    std::string reason;
    if (!read.ok() || !fo4::swf::build_layout(read.file, layout, reason)) {
      ++refused;
      if (!read.ok()) {
        reason = read.message;
      }
      if (reason.empty()) {
        ++reasonless_refusals;
      }
      std::fprintf(stderr, "layout refused %s: %s\n", entry.path.c_str(), reason.c_str());
      continue;
    }
    fo4::swf::SwfLayout again;
    std::string again_reason;
    EXPECT_TRUE(fo4::swf::build_layout(read.file, again, again_reason)) << entry.path;
    Fnv once;
    Fnv twice;
    fold(once, layout);
    fold(twice, again);
    EXPECT_EQ(once.value(), twice.value()) << entry.path;
    corpus.number(once.value());
    ++exported;
    lzma_exported += lzma ? 1u : 0u;
    fonts += layout.fonts.size();
    for (const auto& font : layout.fonts) {
      font_outline_fonts += font.has_outlines ? 1u : 0u;
      font_glyphs += font.has_glyph_count ? font.glyph_count : 0u;
      if (font.has_codes) {
        EXPECT_EQ(font.codes.size(), font.glyph_count) << entry.path << " font " << font.font_id;
      }
      fonts_with_codes += font.has_codes ? 1u : 0u;
      font_codes += font.codes.size();
      fonts_with_name_tag += font.has_font_name ? 1u : 0u;
      fonts_with_class += font.has_font_class ? 1u : 0u;
      info_fonts += font.version == 1 && font.has_style ? 1u : 0u;
    }
    states += layout.states.size();
    rows += layout.rows.size();
    scripted_movies += layout.scripted ? 1u : 0u;
    for (const auto& row : layout.rows) {
      rows_with_rect += row.has_rect ? 1u : 0u;
      flagged_rows += (row.flags & fo4::swf::kLayoutScripted) != 0 ? 1u : 0u;
      duplicate_rows += (row.flags & fo4::swf::kLayoutDuplicatePath) != 0 ? 1u : 0u;
      text_rows_with_font_class += row.text && row.text->has_font_class ? 1u : 0u;
    }
  }
  RecordProperty("movies", std::to_string(movies));
  RecordProperty("exported", std::to_string(exported));
  RecordProperty("refused", std::to_string(refused));
  RecordProperty("lzma_movies", std::to_string(lzma_movies));
  RecordProperty("lzma_exported", std::to_string(lzma_exported));
  RecordProperty("fonts", std::to_string(fonts));
  RecordProperty("font_outline_fonts", std::to_string(font_outline_fonts));
  RecordProperty("font_glyphs", std::to_string(font_glyphs));
  RecordProperty("fonts_with_codes", std::to_string(fonts_with_codes));
  RecordProperty("font_codes", std::to_string(font_codes));
  RecordProperty("fonts_with_name_tag", std::to_string(fonts_with_name_tag));
  RecordProperty("fonts_with_class", std::to_string(fonts_with_class));
  RecordProperty("info_fonts", std::to_string(info_fonts));
  RecordProperty("text_rows_with_font_class", std::to_string(text_rows_with_font_class));
  RecordProperty("reasonless_refusals", std::to_string(reasonless_refusals));
  RecordProperty("states", std::to_string(states));
  RecordProperty("rows", std::to_string(rows));
  RecordProperty("rows_with_rect", std::to_string(rows_with_rect));
  RecordProperty("scripted_movies", std::to_string(scripted_movies));
  RecordProperty("flagged_rows", std::to_string(flagged_rows));
  RecordProperty("duplicate_rows", std::to_string(duplicate_rows));
  RecordProperty("layout_hash", hex(corpus.value()));
  EXPECT_GT(movies, 0u);
  EXPECT_GT(exported, 0u);
  EXPECT_EQ(reasonless_refusals, 0u);
}
