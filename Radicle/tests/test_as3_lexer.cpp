#include "fo4/as3/lexer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using fo4::as3::Token;
using fo4::as3::TokenKind;

namespace {

std::vector<Token> lex(std::string_view source) {
  return fo4::as3::tokenize(source);
}

std::string kinds(const std::vector<Token>& tokens) {
  std::string out;
  for (const auto& token : tokens) {
    if (token.kind == TokenKind::end_of_file) continue;
    if (!out.empty()) out += ' ';
    out += fo4::as3::token_kind_name(token.kind);
  }
  return out;
}

std::string texts(const std::vector<Token>& tokens) {
  std::string out;
  for (const auto& token : tokens) {
    if (token.kind == TokenKind::end_of_file) continue;
    if (!out.empty()) out += '|';
    out += token.text;
  }
  return out;
}

std::optional<Token> first_error(const std::vector<Token>& tokens) {
  for (const auto& token : tokens) {
    if (token.kind == TokenKind::error) return token;
  }
  return std::nullopt;
}

}  // namespace

// Detects: a lexer that must separate reserved words from identifiers.
TEST(As3Lexer, LexesKeywordsAndIdentifiers) {
  const auto tokens = lex("class Foo { var $x_1; }");
  EXPECT_EQ(kinds(tokens), "keyword identifier punct keyword identifier punct punct");
  EXPECT_EQ(texts(tokens), "class|Foo|{|var|$x_1|;|}");
}

// Detects: contextual words that must stay identifiers so the parser can accept them in declarations.
TEST(As3Lexer, KeepsContextualWordsAsIdentifiers) {
  const auto tokens = lex("for each (getName in list) getName = each;");
  EXPECT_EQ(kinds(tokens),
            "keyword identifier punct identifier keyword identifier punct identifier punct "
            "identifier punct");
  EXPECT_EQ(tokens[1].kind, TokenKind::identifier);
  EXPECT_EQ(tokens[1].text, "each");
}

// Detects: a number scanner that must accept the decimal, exponent and hex forms.
TEST(As3Lexer, LexesNumberForms) {
  const auto tokens = lex("0 42 3.5 .5 1e3 1.5e-2 0xFF 1.");
  EXPECT_EQ(kinds(tokens),
            "number number number number number number number number");
  EXPECT_EQ(texts(tokens), "0|42|3.5|.5|1e3|1.5e-2|0xFF|1.");
}

// Detects: a number scanner that must refuse a hex marker without digits and an identifier suffix.
TEST(As3Lexer, RejectsBadNumbers) {
  for (const std::string_view source : {"0x", "1abc", "1e+"}) {
    const auto error = first_error(lex(source));
    ASSERT_TRUE(error.has_value()) << source;
    EXPECT_FALSE(error->error.empty()) << source;
  }
}

// Detects: a string scanner that must decode the single-character, hex and unicode escapes.
TEST(As3Lexer, DecodesStringEscapes) {
  const auto tokens = lex("\"a\\n\\t\\x41\\u0042\\\\\\\"\" '\\''");
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].kind, TokenKind::string);
  EXPECT_EQ(tokens[0].value, std::string{"a\n\tAB\\\""});
  EXPECT_EQ(tokens[1].kind, TokenKind::string);
  EXPECT_EQ(tokens[1].value, std::string{"'"});
}

// Detects: a string scanner that must refuse unterminated literals, raw newlines and bad escapes.
TEST(As3Lexer, RejectsBadStrings) {
  for (const std::string_view source : {"\"abc", "\"a\nb\"", "\"\\q\"", "\"\\x4\""}) {
    const auto error = first_error(lex(source));
    ASSERT_TRUE(error.has_value()) << source;
    EXPECT_EQ(error->kind, TokenKind::error) << source;
  }
}

// Detects: a lexer that must skip line and block comments, and must refuse an unterminated block.
TEST(As3Lexer, SkipsComments) {
  const auto tokens = lex("1 // one\n2 /* two */ 3");
  EXPECT_EQ(kinds(tokens), "number number number");
  const auto error = first_error(lex("/* never closed"));
  ASSERT_TRUE(error.has_value());
  EXPECT_NE(error->error.find("unterminated"), std::string::npos);
}

// Detects: punctuators that must match longest first instead of splitting into single characters.
TEST(As3Lexer, LexesLongestPunctuators) {
  const auto tokens = lex(">>>= >>> >>= ... === !== <= == :: .. .");
  EXPECT_EQ(texts(tokens), ">>>=|>>>|>>=|...|===|!==|<=|==|::|..|.");
}

// Detects: a regex literal that must be recognised only where an expression may start.
TEST(As3Lexer, UsesExpressionContextToTellRegexFromDivision) {
  const auto regex = lex("x = /ab+/gi;");
  ASSERT_EQ(regex.size(), 5u);
  EXPECT_EQ(regex[2].kind, TokenKind::regex);
  EXPECT_EQ(regex[2].text, "/ab+/gi");
  EXPECT_EQ(regex[2].value, "ab+");

  EXPECT_EQ(kinds(lex("a / b")), "identifier punct identifier");

  const auto division = lex("f() / 2");
  ASSERT_EQ(division.size(), 6u);
  EXPECT_EQ(division[3].kind, TokenKind::punct);
  EXPECT_EQ(division[3].text, "/");

  const auto returned = lex("return /x/;");
  ASSERT_EQ(returned.size(), 4u);
  EXPECT_EQ(returned[1].kind, TokenKind::regex);
}

// Detects: source locations that must point at the token start instead of the end of the scan.
TEST(As3Lexer, TracksOffsetsAndLines) {
  const auto tokens = lex("a\nbb\n  c");
  ASSERT_EQ(tokens.size(), 4u);
  EXPECT_EQ(tokens[0].offset, 0u);
  EXPECT_EQ(tokens[0].line, 1u);
  EXPECT_EQ(tokens[1].offset, 2u);
  EXPECT_EQ(tokens[1].line, 2u);
  EXPECT_EQ(tokens[2].offset, 7u);
  EXPECT_EQ(tokens[2].line, 3u);
}

// Detects: an unsupported preprocessor directive and an unlexable character that must each produce an error.
TEST(As3Lexer, RejectsDirectivesAndUnknownCharacters) {
  const auto directive = lex("#include foo.as\nbar");
  ASSERT_TRUE(first_error(directive).has_value());
  ASSERT_GE(directive.size(), 2u);
  EXPECT_EQ(directive[directive.size() - 2].kind, TokenKind::identifier);
  EXPECT_EQ(directive[directive.size() - 2].text, "bar");

  for (const std::string_view source : {"\\", "`"}) {
    ASSERT_TRUE(first_error(lex(source)).has_value()) << source;
  }
}

// Detects: a tokenizer that must always end with exactly one end_of_file token.
TEST(As3Lexer, EndsWithOneEndOfFileToken) {
  const auto empty = lex("");
  ASSERT_EQ(empty.size(), 1u);
  EXPECT_EQ(empty[0].kind, TokenKind::end_of_file);
  EXPECT_EQ(empty[0].offset, 0u);
  EXPECT_EQ(empty[0].line, 1u);

  const auto tokens = lex("a");
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens.back().kind, TokenKind::end_of_file);
  EXPECT_EQ(tokens.back().offset, 1u);
}
