#pragma once

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/display.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::avm2 {

struct TextFormatData {
  std::string font_name;
  std::uint16_t font_id{0};
  double size{12.0};
  std::uint32_t color{0xFF000000u};
  bool bold{false};
  bool italic{false};
  bool underline{false};
  std::string align{"left"};
  double left_margin{0.0};
  double right_margin{0.0};
  double indent{0.0};
  double leading{0.0};
};

struct GlyphPlacement {
  std::uint16_t glyph_id{0};
  std::uint32_t code{0};
  double advance{0.0};
  double x{0.0};
};

struct TextRun {
  std::uint16_t font_id{0};
  double font_size{12.0};
  std::uint32_t color{0xFF000000u};
  double x{0.0};
  double y{0.0};
  std::vector<GlyphPlacement> glyphs;
};

enum class TextFieldAutoSize : std::uint8_t { none = 0, left, center, right };

class TextFieldObject : public DisplayObject {
 public:
  explicit TextFieldObject(Class* display_class, FontRegistry* registry = nullptr);

  bool is_text_field() const noexcept override { return true; }

  void set_font_registry(FontRegistry* registry) noexcept { registry_ = registry; }

  const std::string& text() const noexcept { return text_; }
  void set_text(std::string text);
  const std::string& html_text() const noexcept { return html_text_; }
  /* Requires an AS3 htmlText subset: tags are stripped and the five basic entities decoded. */
  void set_html_text(std::string html);

  const TextFormatData& format() const noexcept { return format_; }
  void set_format(TextFormatData format);

  TextFieldAutoSize auto_size() const noexcept { return auto_size_; }
  void set_auto_size(TextFieldAutoSize value) noexcept { auto_size_ = value; }
  bool word_wrap() const noexcept { return word_wrap_; }
  void set_word_wrap(bool value);
  bool multiline() const noexcept { return multiline_; }
  void set_multiline(bool value) noexcept { multiline_ = value; }
  std::uint32_t text_color() const noexcept { return format_.color; }
  void set_text_color(std::uint32_t color) noexcept { format_.color = color; }

  /* Requires the current text and registry; refreshes the runs and the measured size. */
  void relayout();
  const std::vector<TextRun>& runs() const noexcept { return runs_; }
  double text_width() const noexcept { return text_width_; }
  double text_height() const noexcept { return text_height_; }
  std::size_t line_count() const noexcept { return lines_; }
  std::string line_text(std::size_t index) const;

 private:
  FontRegistry* registry_{nullptr};
  std::string text_;
  std::string html_text_;
  TextFormatData format_{};
  TextFieldAutoSize auto_size_{TextFieldAutoSize::none};
  bool word_wrap_{false};
  bool multiline_{false};
  std::vector<TextRun> runs_;
  double text_width_{0.0};
  double text_height_{0.0};
  std::size_t lines_{1};
};

struct StaticTextRecord {
  std::optional<std::uint16_t> font_id;
  std::vector<std::uint32_t> glyph_indexes;
};

class StaticTextObject : public DisplayObject {
 public:
  StaticTextObject(Class* display_class, const FontRegistry* registry);

  bool is_static_text() const noexcept override { return true; }

  void set_records(std::vector<StaticTextRecord> records) { records_ = std::move(records); }
  /* Requires the registry that holds the record fonts; one unmapped glyph makes the text empty. */
  std::string text() const;

 private:
  const FontRegistry* registry_{nullptr};
  std::vector<StaticTextRecord> records_;
};

/* Requires UTF-8 input. */
std::u16string utf16_from_utf8(std::string_view text);
/* Requires UTF-16 input. */
std::string utf8_from_utf16(std::u16string_view units);

/* Requires a linked runtime; creates the TextFormat instance carrying the data. */
Value make_text_format_instance(Interpreter& interpreter, const TextFormatData& format);
/* Requires a TextFormat instance; fills the data from its properties. */
TextFormatData read_text_format_instance(const Value& value);

}  // namespace fo4::avm2
