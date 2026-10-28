#include <fo4/avm2/coercion.hpp>
#include <fo4/avm2/fonts.hpp>
#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/text.hpp>
#include <fo4/avm2/value.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::FontGlyph;
using fo4::avm2::FontRegistry;
using fo4::avm2::FontResource;
using fo4::avm2::Interpreter;
using fo4::avm2::Runtime;
using fo4::avm2::TextFormatData;
using fo4::avm2::TextFieldObject;

FontRegistry test_fonts() {
  FontRegistry registry;
  FontResource font;
  font.character_id = 5;
  font.name = "Test";
  font.has_layout = true;
  font.glyphs.push_back(FontGlyph{0, 'A', 0.5});
  font.glyphs.push_back(FontGlyph{1, 'B', 0.25});
  registry.add(font);
  return registry;
}

ExecutionResult run_script(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                           std::uint32_t local_count = 1) {
  const std::uint32_t method = script.method("init", code.bytes(), local_count);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.status = fo4::avm2::InterpreterStatus::TypeError;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

TEST(Avm2Text, LaysOutGlyphsFromAnEmbeddedFont) {
  FontRegistry registry = test_fonts();
  auto field = std::make_shared<TextFieldObject>(nullptr, &registry);
  TextFormatData format;
  format.font_name = "Test";
  format.size = 20.0;
  field->set_format(format);
  field->set_text("AB");
  ASSERT_EQ(field->line_count(), 1u);
  ASSERT_EQ(field->runs().size(), 1u);
  const auto& run = field->runs()[0];
  ASSERT_EQ(run.glyphs.size(), 2u);
  EXPECT_EQ(run.glyphs[0].glyph_id, 0);
  EXPECT_EQ(run.glyphs[1].glyph_id, 1);
  EXPECT_DOUBLE_EQ(run.glyphs[0].advance, 10.0);
  EXPECT_DOUBLE_EQ(run.glyphs[1].advance, 5.0);
  EXPECT_DOUBLE_EQ(run.glyphs[1].x, 10.0);
  EXPECT_DOUBLE_EQ(field->text_width(), 15.0);
  EXPECT_DOUBLE_EQ(field->text_height(), 24.0);
}

TEST(Avm2Text, LineBreakStartsASecondRun) {
  FontRegistry registry = test_fonts();
  auto field = std::make_shared<TextFieldObject>(nullptr, &registry);
  TextFormatData format;
  format.font_name = "Test";
  format.size = 10.0;
  field->set_format(format);
  field->set_text("A\nB");
  EXPECT_EQ(field->line_count(), 2u);
  ASSERT_EQ(field->runs().size(), 2u);
  EXPECT_DOUBLE_EQ(field->runs()[1].y, 12.0);
  EXPECT_EQ(field->line_text(1), "B");
  EXPECT_DOUBLE_EQ(field->text_height(), 24.0);
}

TEST(Avm2Text, HtmlTextStripsTagsAndDecodesEntities) {
  FontRegistry registry = test_fonts();
  auto field = std::make_shared<TextFieldObject>(nullptr, &registry);
  field->set_html_text("<b>A</b>&amp;B");
  EXPECT_EQ(field->text(), "A&B");
  EXPECT_EQ(field->line_count(), 1u);
}

TEST(Avm2Text, TextFormatInstanceRoundTripsTheData) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  code.returnvoid();
  const std::uint32_t method = script.method("init", code.bytes(), 1);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  ASSERT_TRUE(runtime.add_file(file).ok());
  ASSERT_TRUE(runtime.link().ok());
  Interpreter interpreter(runtime);
  const ExecutionResult boot = interpreter.run_script(0);
  ASSERT_TRUE(boot.ok());

  TextFormatData format;
  format.font_name = "Test";
  format.size = 18.0;
  format.color = 0xFF112233u;
  format.bold = true;
  format.align = "center";
  const fo4::avm2::Value value = fo4::avm2::make_text_format_instance(interpreter, format);
  ASSERT_TRUE(value.is_object());
  const TextFormatData read = fo4::avm2::read_text_format_instance(value);
  EXPECT_EQ(read.font_name, "Test");
  EXPECT_DOUBLE_EQ(read.size, 18.0);
  EXPECT_EQ(read.color, 0xFF112233u);
  EXPECT_TRUE(read.bold);
  EXPECT_EQ(read.align, "center");
}

TEST(Avm2Text, ScriptCanSetTheTextAndReadMeasuredWidth) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t text_field = script.multiname("TextField", "flash.text");
  const std::uint32_t text_name = script.multiname("text");
  const std::uint32_t width_name = script.multiname("textWidth");
  const std::uint32_t lines_name = script.multiname("numLines");
  code.getlex(text_field);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(script.string_of("AB"));
  code.setproperty(text_name);
  code.getlocal(1);
  code.getproperty(width_name);
  code.getlocal(1);
  code.getproperty(lines_name);
  code.add();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 13.0);
}

TEST(Avm2Text, AppendTextAndGetLineTextWorkThroughNatives) {
  avm2_test::ScriptBuilder script;
  avm2_test::CodeBuilder code;
  const std::uint32_t text_field = script.multiname("TextField", "flash.text");
  const std::uint32_t text_name = script.multiname("text");
  const std::uint32_t append = script.multiname("appendText");
  const std::uint32_t get_line = script.multiname("getLineText");
  code.getlex(text_field);
  code.construct(0);
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(script.string_of("AB"));
  code.setproperty(text_name);
  code.getlocal(1);
  code.pushstring(script.string_of("CD"));
  code.callproperty(append, 1);
  code.pop();
  code.getlocal(1);
  code.pushbyte(0);
  code.callproperty(get_line, 1);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code, 2);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "ABCD");
}

FontRegistry wrap_fonts() {
  FontRegistry registry;
  FontResource font;
  font.character_id = 6;
  font.glyphs.push_back(FontGlyph{0, 'A', 0.5});
  font.glyphs.push_back(FontGlyph{1, ' ', 0.25});
  font.glyphs.push_back(FontGlyph{2, 0xE9u, 0.25});
  registry.add(font);
  return registry;
}

TEST(Avm2Text, WordWrapBreaksAfterTheLastSpaceThatFits) {
  FontRegistry registry = wrap_fonts();
  auto field = std::make_shared<TextFieldObject>(nullptr, &registry);
  TextFormatData format;
  format.font_id = 6;
  format.size = 20.0;
  field->set_format(format);
  field->set_width(25.0);
  field->set_word_wrap(true);
  field->set_text("AA AA");
  ASSERT_EQ(field->runs().size(), 2u);
  EXPECT_EQ(field->line_text(0), "AA ");
  EXPECT_EQ(field->line_text(1), "AA");
  EXPECT_EQ(field->line_count(), 2u);
  EXPECT_DOUBLE_EQ(field->runs()[1].glyphs[1].x, 10.0);
  EXPECT_DOUBLE_EQ(field->text_width(), 25.0);
  EXPECT_DOUBLE_EQ(field->text_height(), 48.0);
}

TEST(Avm2Text, WordWrapBreaksInsideAWordWhenNoSpaceFits) {
  FontRegistry registry = wrap_fonts();
  auto field = std::make_shared<TextFieldObject>(nullptr, &registry);
  TextFormatData format;
  format.font_id = 6;
  format.size = 20.0;
  field->set_format(format);
  field->set_width(25.0);
  field->set_text("AAAA");
  EXPECT_EQ(field->runs().size(), 1u);
  field->set_word_wrap(true);
  ASSERT_EQ(field->runs().size(), 2u);
  EXPECT_EQ(field->line_text(0), "AA");
  EXPECT_EQ(field->line_text(1), "AA");
  field->set_word_wrap(false);
  EXPECT_EQ(field->runs().size(), 1u);
  EXPECT_DOUBLE_EQ(field->text_width(), 40.0);
}

TEST(Avm2Text, TextIsLaidOutInUtf16CodeUnits) {
  FontRegistry registry = wrap_fonts();
  auto field = std::make_shared<TextFieldObject>(nullptr, &registry);
  TextFormatData format;
  format.font_id = 6;
  format.size = 20.0;
  field->set_format(format);
  const std::string text = "\xC3\xA9\xF0\x9F\x98\x80";
  field->set_text(text);
  ASSERT_EQ(field->runs().size(), 1u);
  const auto& glyphs = field->runs()[0].glyphs;
  ASSERT_EQ(glyphs.size(), 3u);
  EXPECT_EQ(glyphs[0].code, 0xE9u);
  EXPECT_DOUBLE_EQ(glyphs[0].advance, 5.0);
  EXPECT_EQ(glyphs[1].code, 0xD83Du);
  EXPECT_EQ(glyphs[2].code, 0xDE00u);
  EXPECT_DOUBLE_EQ(field->text_width(), 25.0);
  EXPECT_EQ(field->line_text(0), text);
  EXPECT_EQ(fo4::avm2::utf8_from_utf16(fo4::avm2::utf16_from_utf8(text)), text);
}

TEST(Avm2Text, OwnerZeroReadsTheEarliestRegisteredLayoutFontAcrossOwners) {
  FontRegistry registry;
  FontResource first;
  first.character_id = 1;
  first.owner = 5;
  first.has_layout = true;
  first.glyphs.push_back(FontGlyph{0, 'A', 0.5});
  FontResource second = first;
  second.owner = 6;
  registry.add(first);
  registry.add(second);
  ASSERT_NE(registry.first_with_layout(0), nullptr);
  EXPECT_EQ(registry.first_with_layout(0)->owner, 5u);
  EXPECT_EQ(registry.first_with_layout(6)->owner, 6u);
  registry.remove_owner(5);
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_EQ(registry.find(5, 1), nullptr);
  ASSERT_NE(registry.first_with_layout(0), nullptr);
  EXPECT_EQ(registry.first_with_layout(0)->owner, 6u);
}

}  // namespace
