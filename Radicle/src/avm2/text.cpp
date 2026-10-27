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

const FontResource* FontRegistry::find(std::uint16_t character_id) const {
  for (const FontResource& font : fonts_) {
    if (font.character_id == character_id) {
      return &font;
    }
  }
  return nullptr;
}

const FontResource* FontRegistry::find_by_name(std::string_view name) const {
  if (name.empty()) {
    return nullptr;
  }
  for (const FontResource& font : fonts_) {
    if (font.name == name) {
      return &font;
    }
  }
  return nullptr;
}

const FontResource* FontRegistry::first_with_layout() const {
  for (const FontResource& font : fonts_) {
    if (font.has_layout && !font.glyphs.empty()) {
      return &font;
    }
  }
  return nullptr;
}

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

void TextFieldObject::relayout() {
  runs_.clear();
  text_width_ = 0.0;
  const FontResource* font = nullptr;
  if (registry_ != nullptr) {
    font = registry_->find_by_name(format_.font_name);
    if (font == nullptr) {
      font = registry_->first_with_layout();
    }
  }
  const double size = format_.size > 0.0 ? format_.size : 12.0;
  const double line_height = size * 1.2 + format_.leading;
  std::size_t line_index = 0;
  std::size_t line_start = 0;
  while (line_start <= text_.size()) {
    std::size_t line_end = text_.find('\n', line_start);
    if (line_end == std::string::npos) {
      line_end = text_.size();
    }
    TextRun run;
    run.font_id = font == nullptr ? 0 : font->character_id;
    run.font_size = size;
    run.color = format_.color;
    run.x = format_.left_margin;
    run.y = format_.indent + static_cast<double>(line_index) * line_height;
    double pen_x = run.x;
    for (std::size_t index = line_start; index < line_end; ++index) {
      const auto code = static_cast<std::uint32_t>(static_cast<unsigned char>(text_[index]));
      GlyphPlacement placement;
      placement.code = code;
      placement.glyph_id = static_cast<std::uint16_t>(code);
      placement.advance = size * 0.5;
      if (font != nullptr) {
        const std::size_t glyph_index = font->glyph_index_for_code(code);
        if (glyph_index != static_cast<std::size_t>(-1)) {
          placement.glyph_id = static_cast<std::uint16_t>(glyph_index);
          placement.advance = size * font->glyphs[glyph_index].advance;
        }
      }
      placement.x = pen_x;
      pen_x += placement.advance;
      run.glyphs.push_back(placement);
    }
    text_width_ = std::max(text_width_, pen_x - run.x);
    runs_.push_back(std::move(run));
    ++line_index;
    if (line_end >= text_.size()) {
      break;
    }
    line_start = line_end + 1;
  }
  lines_ = line_index;
  text_height_ = static_cast<double>(lines_) * line_height;
}

std::string TextFieldObject::line_text(std::size_t index) const {
  std::size_t line_index = 0;
  std::size_t line_start = 0;
  while (line_start <= text_.size()) {
    std::size_t line_end = text_.find('\n', line_start);
    if (line_end == std::string::npos) {
      line_end = text_.size();
    }
    if (line_index == index) {
      return text_.substr(line_start, line_end - line_start);
    }
    if (line_end >= text_.size()) {
      break;
    }
    ++line_index;
    line_start = line_end + 1;
  }
  return {};
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
