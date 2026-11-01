#include <fo4/avm2/interpreter.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/runtime.hpp>
#include <fo4/avm2/value.hpp>
#include <fo4/avm2/xml.hpp>

#include "avm2_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

namespace abc = fo4::abc;

using fo4::avm2::ExecutionResult;
using fo4::avm2::Interpreter;
using fo4::avm2::InterpreterStatus;
using fo4::avm2::Runtime;

ExecutionResult run_script(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                           std::uint32_t locals = 1) {
  const std::uint32_t method = script.method("init", code.bytes(), locals);
  abc::AbcFile file = script.finish(method);
  Runtime runtime;
  if (!runtime.add_file(file).ok() || !runtime.link().ok()) {
    ExecutionResult failure;
    failure.status = InterpreterStatus::TypeError;
    failure.message = "runtime did not link";
    return failure;
  }
  Interpreter interpreter(runtime);
  return interpreter.run_script(0);
}

void parse_text(avm2_test::ScriptBuilder& script, avm2_test::CodeBuilder& code,
                std::uint32_t xml_name, const std::string& text) {
  code.findpropstrict(xml_name);
  code.pushstring(script.string_of(text));
  code.callproperty(xml_name, 1);
}

}  // namespace

TEST(Avm2Xml, ConvertsTextToAnElementThatSerializesBack) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a x=\"1\"><b>t</b></a>");
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a x=\"1\"><b>t</b></a>");
}

TEST(Avm2Xml, ChildAccessReturnsTheNamedChildsText) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t child = script.multiname("b");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b>one</b><c>two</c></a>");
  code.getproperty(child);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "one");
}

TEST(Avm2Xml, AttributeAccessReadsTheAttributeValue) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t attribute = script.attribute_multiname("id");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a id=\"7\"/>");
  code.getproperty(attribute);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "7");
}

TEST(Avm2Xml, MismatchedTagsAreATypeError) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b></a>");
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, NestingWithinTheDepthLimitParses) {
  std::string text;
  for (std::size_t level = 0; level < 60; ++level) text += "<a>";
  for (std::size_t level = 0; level < 60; ++level) text += "</a>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.callproperty(length, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 1.0);
}

TEST(Avm2Xml, NestingPastTheDepthLimitIsATypeError) {
  std::string text;
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth + 6; ++level) text += "<a>";
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth + 6; ++level) text += "</a>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, NodeCountPastTheLimitIsATypeError) {
  std::string text = "<r>";
  for (std::size_t index = 0; index < fo4::avm2::kMaxXmlNodes; ++index) text += "<a/>";
  text += "</r>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, TextPastTheByteLimitIsATypeError) {
  const std::string text(fo4::avm2::kMaxXmlBytes + 1, ' ');
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, PredefinedAndNumericEntitiesDecodeInText) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a>&lt;&amp;&#65;&#x42;</a>");
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<&AB");
}

TEST(Avm2Xml, AnUnknownEntityIsATypeError) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a>&bogus;</a>");
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, WhitespaceOnlyTextIsNotAChild) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t children = script.multiname("children");
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a>\n  <b>x</b>\n</a>");
  code.callproperty(children, 0);
  code.callproperty(length, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 1.0);
}

TEST(Avm2Xml, ChildrenOfAnElementIsAnXmlListThatCoercesToXMLList) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t xml_list = script.multiname("XMLList");
  const std::uint32_t children = script.multiname("children");
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b/><c/></a>");
  code.callproperty(children, 0);
  code.coerce(xml_list);
  code.callproperty(length, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 2.0);
}

TEST(Avm2Xml, CoercingAnXmlListToXmlIsATypeError) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t xml_list = script.multiname("XMLList");
  avm2_test::CodeBuilder code;
  code.findpropstrict(xml_list);
  code.pushstring(script.string_of("<a/><b/>"));
  code.callproperty(xml_list, 1);
  code.coerce(xml);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, XmlListConversionKeepsEveryTopLevelNode) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml_list = script.multiname("XMLList");
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder code;
  code.findpropstrict(xml_list);
  code.pushstring(script.string_of("<a/><b/>"));
  code.callproperty(xml_list, 1);
  code.callproperty(length, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 2.0);
}

TEST(Avm2Xml, ChildWriteReplacesTheContentOfTheFirstMatchingChild) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t child = script.multiname("b");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b x=\"1\">one</b><b>two</b></a>");
  code.dup();
  code.pushstring(script.string_of("three"));
  code.setproperty(child);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a><b x=\"1\">three</b><b>two</b></a>");
}

TEST(Avm2Xml, ChildWriteAppendsANewElementWhenNoChildMatches) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t child = script.multiname("c");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b>one</b></a>");
  code.dup();
  code.pushstring(script.string_of("two"));
  code.setproperty(child);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a><b>one</b><c>two</c></a>");
}

TEST(Avm2Xml, AttributeWriteSetsAnExistingAttributeOrCreatesIt) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t existing = script.attribute_multiname("x");
  const std::uint32_t created = script.attribute_multiname("y");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a x=\"1\"/>");
  code.dup();
  code.pushstring(script.string_of("2"));
  code.setproperty(existing);
  code.dup();
  code.pushstring(script.string_of("3"));
  code.setproperty(created);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a x=\"2\" y=\"3\"/>");
}

TEST(Avm2Xml, WriteThroughAListWithOneNodeChangesTheDocument) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t list_child = script.multiname("b");
  const std::uint32_t new_child = script.multiname("c");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b/></a>");
  code.dup();
  code.getproperty(list_child);
  code.dup();
  code.pushstring(script.string_of("y"));
  code.setproperty(new_child);
  code.pop();
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a><b><c>y</c></b></a>");
}

TEST(Avm2Xml, WriteThroughAListOfTwoNodesIsATypeError) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t list_child = script.multiname("b");
  const std::uint32_t new_child = script.multiname("c");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b/><b/></a>");
  code.getproperty(list_child);
  code.pushstring(script.string_of("y"));
  code.setproperty(new_child);
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, WriteThatWouldPassTheNodeLimitIsATypeError) {
  std::string text = "<r>";
  for (std::size_t index = 0; index + 1 < fo4::avm2::kMaxXmlNodes; ++index) text += "<a/>";
  text += "</r>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t new_child = script.multiname("c");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.dup();
  code.pushstring(script.string_of("y"));
  code.setproperty(new_child);
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, WriteThroughAChildCountsTheWholeDocumentAgainstTheLimit) {
  std::string text = "<r><a/>";
  for (std::size_t index = 0; index + 3 < fo4::avm2::kMaxXmlNodes; ++index) text += "<b/>";
  text += "</r>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t child = script.multiname("a");
  const std::uint32_t new_child = script.multiname("c");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.getproperty(child);
  code.pushstring(script.string_of("y"));
  code.setproperty(new_child);
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, TextMethodReturnsTheTextChildren) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t text = script.multiname("text");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a>one</a>");
  code.callproperty(text, 0);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "one");
}

TEST(Avm2Xml, CopyMakesASeparateDocumentWithTheSameMarkup) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t copy = script.multiname("copy");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a><b>x</b></a>");
  code.callproperty(copy, 0);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a><b>x</b></a>");
}

TEST(Avm2Xml, WriteThatWouldNestPastTheDepthLimitIsATypeError) {
  std::string text;
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth; ++level) text += "<e>";
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth; ++level) text += "</e>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t level_name = script.multiname("e");
  const std::uint32_t child = script.multiname("c");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  for (std::size_t step = 1; step < fo4::avm2::kMaxXmlDepth; ++step) code.getproperty(level_name);
  code.pushstring(script.string_of("t"));
  code.setproperty(child);
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, WriteThatStaysWithinTheDepthLimitSucceeds) {
  std::string text;
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth; ++level) text += "<e>";
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth; ++level) text += "</e>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t level_name = script.multiname("e");
  const std::uint32_t child = script.multiname("c");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  for (std::size_t step = 1; step + 1 < fo4::avm2::kMaxXmlDepth; ++step) {
    code.getproperty(level_name);
  }
  code.pushstring(script.string_of("t"));
  code.setproperty(child);
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_TRUE(result.ok()) << result.message;
}

TEST(Avm2Xml, CopyKeepsTheNestingDepthSoAWriteStillStopsAtTheLimit) {
  std::string text;
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth; ++level) text += "<e>";
  for (std::size_t level = 0; level < fo4::avm2::kMaxXmlDepth; ++level) text += "</e>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t copy = script.multiname("copy");
  const std::uint32_t level_name = script.multiname("e");
  const std::uint32_t child = script.multiname("c");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.callproperty(copy, 0);
  for (std::size_t step = 1; step < fo4::avm2::kMaxXmlDepth; ++step) code.getproperty(level_name);
  code.pushstring(script.string_of("t"));
  code.setproperty(child);
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, RepeatedDeepeningWritesStopAtTheDepthLimit) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t level_name = script.multiname("a");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<r/>");
  for (std::size_t step = 0; step < 70; ++step) {
    code.dup();
    code.pushstring(script.string_of("t"));
    code.setproperty(level_name);
    code.getproperty(level_name);
  }
  code.pushnull();
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, DuplicateAttributeAfterManyAttributesIsATypeError) {
  std::string text = "<r";
  for (std::size_t index = 0; index < 5000; ++index) {
    text += " a" + std::to_string(index) + "=\"v\"";
  }
  text += " a0=\"again\"/>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, ManyDistinctAttributesParseAndReadBack) {
  std::string text = "<r";
  for (std::size_t index = 0; index < 80000; ++index) {
    text += " a" + std::to_string(index) + "=\"v" + std::to_string(index) + "\"";
  }
  text += "/>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t last = script.attribute_multiname("a79999");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, text);
  code.getproperty(last);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "v79999");
}

TEST(Avm2Xml, RemovedSubtreeIsNotSubtractedFromTheDocumentCountAgain) {
  std::string text = "<x><a><b><c>t</c></b></a>";
  for (std::size_t index = 5; index < fo4::avm2::kMaxXmlNodes; ++index) text += "<p/>";
  text += "</x>";
  for (const bool write_again : {false, true}) {
    avm2_test::ScriptBuilder script;
    const std::uint32_t xml = script.multiname("XML");
    const std::uint32_t a = script.multiname("a");
    const std::uint32_t b = script.multiname("b");
    const std::uint32_t c = script.multiname("c");
    const std::uint32_t q = script.multiname("q");
    const std::uint32_t r = script.multiname("r");
    avm2_test::CodeBuilder code;
    parse_text(script, code, xml, text);
    code.setlocal(1);
    code.getlocal(1);
    code.getproperty(a);
    code.getproperty(b);
    code.setlocal(2);
    code.getlocal(1);
    code.pushstring(script.string_of(""));
    code.setproperty(a);
    code.getlocal(2);
    code.pushstring(script.string_of(""));
    code.setproperty(c);
    code.getlocal(1);
    code.pushstring(script.string_of("y"));
    code.setproperty(q);
    if (write_again) {
      code.getlocal(1);
      code.pushstring(script.string_of("y"));
      code.setproperty(r);
    }
    code.pushnull();
    code.returnvalue();
    const ExecutionResult result = run_script(script, code, 3);
    if (write_again) {
      EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
    } else {
      EXPECT_TRUE(result.ok()) << result.message;
    }
  }
}

TEST(Avm2Xml, OneNodeXmlListCoercesToXml) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t xml_list = script.multiname("XMLList");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  code.findpropstrict(xml_list);
  code.pushstring(script.string_of("<a>t</a>"));
  code.callproperty(xml_list, 1);
  code.coerce(xml);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a>t</a>");
}

TEST(Avm2Xml, XmlCoercesToAOneNodeXmlList) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t xml_list = script.multiname("XMLList");
  const std::uint32_t length = script.multiname("length");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a/>");
  code.coerce(xml_list);
  code.callproperty(length, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_number_like());
  EXPECT_EQ(result.value.as_number(), 1.0);
}

TEST(Avm2Xml, AByteOrderMarkBeforeTheRootIsATypeError) {
  const std::string text = "\xEF\xBB\xBF<a/>";
  avm2_test::ScriptBuilder xml_script;
  const std::uint32_t xml = xml_script.multiname("XML");
  avm2_test::CodeBuilder xml_code;
  parse_text(xml_script, xml_code, xml, text);
  xml_code.returnvalue();
  EXPECT_EQ(run_script(xml_script, xml_code).status, InterpreterStatus::TypeError);

  avm2_test::ScriptBuilder list_script;
  const std::uint32_t xml_list = list_script.multiname("XMLList");
  avm2_test::CodeBuilder list_code;
  list_code.findpropstrict(xml_list);
  list_code.pushstring(list_script.string_of(text));
  list_code.callproperty(xml_list, 1);
  list_code.returnvalue();
  EXPECT_EQ(run_script(list_script, list_code).status, InterpreterStatus::TypeError);
}

TEST(Avm2Xml, AnOpenerWithoutContentIsNotAClosedComment) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<!--><a/>");
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, AProcessingInstructionOpenerWithoutContentIsNotClosed) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<?><a/>");
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  EXPECT_EQ(result.status, InterpreterStatus::TypeError) << result.message;
}

TEST(Avm2Xml, AnOpenerInsideAnElementWithoutContentIsNotClosed) {
  for (const std::string text : {"<r><!--></r>", "<r><?></r>"}) {
    avm2_test::ScriptBuilder script;
    const std::uint32_t xml = script.multiname("XML");
    avm2_test::CodeBuilder code;
    parse_text(script, code, xml, text);
    code.returnvalue();
    const ExecutionResult result = run_script(script, code);
    EXPECT_EQ(result.status, InterpreterStatus::TypeError) << text << ": " << result.message;
  }
}

TEST(Avm2Xml, AttributeLineBreaksAndTabsRoundTripAsCharacterReferences) {
  const std::string markup = "<a v=\"x&#10;y&#9;z&#13;w\"/>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, markup);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), markup);
}

TEST(Avm2Xml, CarriageReturnInTextRoundTripsAsACharacterReference) {
  const std::string markup = "<a>x&#13;y</a>";
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, markup);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), markup);
}

TEST(Avm2Xml, XmlListToStringJoinsItsItemsWithNewlines) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml_list = script.multiname("XMLList");
  const std::uint32_t to_string = script.multiname("toString");
  avm2_test::CodeBuilder code;
  code.findpropstrict(xml_list);
  code.pushstring(script.string_of("<a>1</a><b>2</b>"));
  code.callproperty(xml_list, 1);
  code.callproperty(to_string, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "1\n2");
}

TEST(Avm2Xml, ElementAndAttributeNamesThatWouldNotReparseAreATypeError) {
  for (const std::string name : {"b c", "b<c", "", "1x", "b\"c"}) {
    for (const bool attribute : {false, true}) {
      avm2_test::ScriptBuilder script;
      const std::uint32_t xml = script.multiname("XML");
      const std::uint32_t target =
          attribute ? script.attribute_multiname(name) : script.multiname(name);
      avm2_test::CodeBuilder code;
      parse_text(script, code, xml, "<a/>");
      code.pushstring(script.string_of("t"));
      code.setproperty(target);
      code.pushnull();
      code.returnvalue();
      EXPECT_EQ(run_script(script, code).status, InterpreterStatus::TypeError) << name;
    }
  }
}

TEST(Avm2Xml, PlainAndNamespacedNamesWriteAndReparse) {
  avm2_test::ScriptBuilder script;
  const std::uint32_t xml = script.multiname("XML");
  const std::uint32_t plain = script.multiname("good");
  const std::uint32_t namespaced = script.multiname("ns:good");
  const std::uint32_t attribute = script.attribute_multiname("ns:k");
  const std::uint32_t to_markup = script.multiname("toXMLString");
  avm2_test::CodeBuilder code;
  parse_text(script, code, xml, "<a/>");
  code.setlocal(1);
  code.getlocal(1);
  code.pushstring(script.string_of("t"));
  code.setproperty(plain);
  code.getlocal(1);
  code.pushstring(script.string_of("u"));
  code.setproperty(namespaced);
  code.getlocal(1);
  code.pushstring(script.string_of("v"));
  code.setproperty(attribute);
  code.getlocal(1);
  code.callproperty(to_markup, 0);
  code.setlocal(2);
  code.findpropstrict(xml);
  code.getlocal(2);
  code.callproperty(xml, 1);
  code.callproperty(to_markup, 0);
  code.returnvalue();
  const ExecutionResult result = run_script(script, code, 3);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(result.value.is_string());
  EXPECT_EQ(result.value.as_string(), "<a ns:k=\"v\"><good>t</good><ns:good>u</ns:good></a>");
}
