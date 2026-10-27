#include <fo4/avm2/text.hpp>

#include <fo4/avm2/class.hpp>
#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::avm2 {

std::size_t FontResource::glyph_index_for_code(std::uint32_t code) const {
  for (std::size_t index = 0; index < glyphs.size(); ++index) {
    if (glyphs[index].code == code) {
      return index;
    }
  }
  return static_cast<std::size_t>(-1);
}

void FontRegistry::add(FontResource font) { fonts_.push_back(std::move(font)); }

const FontResource* FontRegistry::find(std::uint64_t owner, std::uint16_t character_id) const {
  for (const FontResource& font : fonts_) {
    if (font.owner == owner && font.character_id == character_id) {
      return &font;
    }
  }
  return nullptr;
}

const FontResource* FontRegistry::find_by_name(std::uint64_t owner, std::string_view name) const {
  if (name.empty()) {
    return nullptr;
  }
  for (const FontResource& font : fonts_) {
    if ((owner == 0 || font.owner == owner) && font.name == name) {
      return &font;
    }
  }
  return nullptr;
}

const FontResource* FontRegistry::first_with_layout(std::uint64_t owner) const {
  for (const FontResource& font : fonts_) {
    if ((owner == 0 || font.owner == owner) && font.has_layout && !font.glyphs.empty()) {
      return &font;
    }
  }
  return nullptr;
}

namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

GlyphPlacement glyph_for(const FontResource* font, double size, std::uint32_t code) {
  GlyphPlacement placement;
  placement.code = code;
  placement.glyph_id = static_cast<std::uint16_t>(code);
  placement.advance = size * 0.5;
  if (font != nullptr) {
    const std::size_t glyph_index = font->glyph_index_for_code(code);
    if (glyph_index != kNone) {
      placement.glyph_id = static_cast<std::uint16_t>(glyph_index);
      placement.advance = size * font->glyphs[glyph_index].advance;
    }
  }
  return placement;
}

std::size_t wrap_cut(const FontResource* font, double size, const std::u16string& units,
                     std::size_t begin, std::size_t end, double wrap_width) {
  if (wrap_width <= 0.0) {
    return end;
  }
  double pen = 0.0;
  std::size_t last_break = kNone;
  for (std::size_t index = begin; index < end; ++index) {
    const double advance = glyph_for(font, size, static_cast<std::uint32_t>(units[index])).advance;
    if (index > begin && pen + advance > wrap_width) {
      return last_break != kNone ? last_break : index;
    }
    if (units[index] == u' ') {
      last_break = index + 1;
    }
    pen += advance;
  }
  return end;
}

}  // namespace

TextFieldObject::TextFieldObject(Class* display_class, FontRegistry* registry)
    : DisplayObject(display_class), registry_(registry) {
  set_dynamic(true);
  relayout();
}

void TextFieldObject::set_text(std::string text) {
  text_ = std::move(text);
  relayout();
}

void TextFieldObject::set_html_text(std::string html) {
  html_text_ = std::move(html);
  std::string plain;
  bool in_tag = false;
  for (const char character : html_text_) {
    if (character == '<') {
      in_tag = true;
      continue;
    }
    if (character == '>') {
      in_tag = false;
      continue;
    }
    if (!in_tag) {
      plain.push_back(character);
    }
  }
  const struct Replacement {
    const char* from;
    const char* to;
  } replacements[] = {{"&amp;", "&"},
                      {"&lt;", "<"},
                      {"&gt;", ">"},
                      {"&quot;", "\""},
                      {"&#39;", "'"}};
  for (const Replacement& replacement : replacements) {
    std::size_t position = 0;
    const std::string from = replacement.from;
    while ((position = plain.find(from, position)) != std::string::npos) {
      plain.replace(position, from.size(), replacement.to);
      position += std::string{replacement.to}.size();
    }
  }
  set_text(std::move(plain));
}

void TextFieldObject::set_format(TextFormatData format) {
  format_ = std::move(format);
  relayout();
}

void TextFieldObject::set_word_wrap(bool value) {
  word_wrap_ = value;
  relayout();
}

void TextFieldObject::relayout() {
  runs_.clear();
  text_width_ = 0.0;
  const FontResource* font = nullptr;
  if (registry_ != nullptr) {
    if (format_.font_id != 0) {
      font = registry_->find(font_owner_, format_.font_id);
    }
    if (font == nullptr) {
      font = registry_->find_by_name(font_owner_, format_.font_name);
    }
    if (font == nullptr) {
      font = registry_->first_with_layout(font_owner_);
    }
  }
  const double size = format_.size > 0.0 ? format_.size : 12.0;
  const double line_height = size * 1.2 + format_.leading;
  const double wrap_width = word_wrap_ ? width() - format_.left_margin - format_.right_margin : 0.0;
  const std::u16string units = utf16_from_utf8(text_);
  std::size_t line_start = 0;
  while (true) {
    std::size_t line_end = units.find(u'\n', line_start);
    if (line_end == std::u16string::npos) {
      line_end = units.size();
    }
    std::size_t begin = line_start;
    while (true) {
      const std::size_t cut = wrap_cut(font, size, units, begin, line_end, wrap_width);
      TextRun run;
      run.font_id = font == nullptr ? 0 : font->character_id;
      run.font_size = size;
      run.color = format_.color;
      run.x = format_.left_margin;
      run.y = format_.indent + static_cast<double>(runs_.size()) * line_height;
      double pen_x = run.x;
      for (std::size_t index = begin; index < cut; ++index) {
        GlyphPlacement placement = glyph_for(font, size, static_cast<std::uint32_t>(units[index]));
        placement.x = pen_x;
        pen_x += placement.advance;
        run.glyphs.push_back(placement);
      }
      text_width_ = std::max(text_width_, pen_x - run.x);
      runs_.push_back(std::move(run));
      if (cut >= line_end) {
        break;
      }
      begin = cut;
    }
    if (line_end >= units.size()) {
      break;
    }
    line_start = line_end + 1;
  }
  lines_ = runs_.size();
  text_height_ = static_cast<double>(lines_) * line_height;
}

std::string TextFieldObject::line_text(std::size_t index) const {
  if (index >= runs_.size()) {
    return {};
  }
  std::u16string units;
  for (const GlyphPlacement& glyph : runs_[index].glyphs) {
    units.push_back(static_cast<char16_t>(glyph.code));
  }
  return utf8_from_utf16(units);
}

StaticTextObject::StaticTextObject(Class* display_class, const FontRegistry* registry)
    : DisplayObject(display_class), registry_(registry) {}

std::string StaticTextObject::text() const {
  std::u16string units;
  for (const StaticTextRecord& record : records_) {
    if (record.glyph_indexes.empty()) {
      continue;
    }
    if (registry_ == nullptr || !record.font_id.has_value()) {
      return std::string{};
    }
    const FontResource* font = registry_->find(font_owner_, *record.font_id);
    if (font == nullptr) {
      return std::string{};
    }
    for (const std::uint32_t index : record.glyph_indexes) {
      if (index >= font->glyphs.size()) {
        return std::string{};
      }
      units.push_back(static_cast<char16_t>(font->glyphs[index].code));
    }
  }
  return utf8_from_utf16(units);
}

Value make_text_format_instance(Interpreter& interpreter, const TextFormatData& format) {
  Class* klass = const_cast<Class*>(interpreter.runtime().find_class("flash.text::TextFormat"));
  if (klass == nullptr) {
    return Value::null();
  }
  auto instance = interpreter.make_instance(*klass);
  const auto set_value = [&instance](const char* name, Value value) {
    Multiname multiname;
    multiname.name = name;
    multiname.namespaces.push_back(Namespace{});
    Property& property = instance->define_own_property(primary_key(multiname));
    property.kind = PropertyKind::Value;
    property.value = std::move(value);
  };
  set_value("font", Value::string(format.font_name));
  set_value("size", Value::number(format.size));
  set_value("color", Value::uint32(format.color));
  set_value("bold", Value::boolean(format.bold));
  set_value("italic", Value::boolean(format.italic));
  set_value("underline", Value::boolean(format.underline));
  set_value("align", Value::string(format.align));
  set_value("leftMargin", Value::number(format.left_margin));
  set_value("rightMargin", Value::number(format.right_margin));
  set_value("indent", Value::number(format.indent));
  set_value("leading", Value::number(format.leading));
  return Value::object(std::move(instance));
}

TextFormatData read_text_format_instance(const Value& value) {
  TextFormatData format;
  if (!value.is_object()) {
    return format;
  }
  const auto read = [&value](const char* name) {
    Multiname multiname;
    multiname.name = name;
    multiname.namespaces.push_back(Namespace{});
    Value out;
    if (!value.as_object()->read_value(multiname, out)) {
      return Value::undefined();
    }
    return out;
  };
  const Value font = read("font");
  if (font.is_string()) {
    format.font_name = font.as_string();
  }
  const Value size = read("size");
  if (size.is_number_like()) {
    format.size = size.as_number();
  }
  const Value color = read("color");
  if (color.is_number_like()) {
    format.color = static_cast<std::uint32_t>(color.as_number());
  }
  const Value bold = read("bold");
  if (bold.is_boolean()) {
    format.bold = bold.as_boolean();
  }
  const Value italic = read("italic");
  if (italic.is_boolean()) {
    format.italic = italic.as_boolean();
  }
  const Value underline = read("underline");
  if (underline.is_boolean()) {
    format.underline = underline.as_boolean();
  }
  const Value align = read("align");
  if (align.is_string()) {
    format.align = align.as_string();
  }
  const Value left = read("leftMargin");
  if (left.is_number_like()) {
    format.left_margin = left.as_number();
  }
  const Value right = read("rightMargin");
  if (right.is_number_like()) {
    format.right_margin = right.as_number();
  }
  const Value indent = read("indent");
  if (indent.is_number_like()) {
    format.indent = indent.as_number();
  }
  const Value leading = read("leading");
  if (leading.is_number_like()) {
    format.leading = leading.as_number();
  }
  return format;
}

}  // namespace fo4::avm2
