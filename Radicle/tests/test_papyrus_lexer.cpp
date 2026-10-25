#include <fo4/papyrus/lexer.hpp>
#include <fo4/papyrus/token.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fo4::papyrus::Lexer;
using fo4::papyrus::TokenKind;

std::vector<TokenKind> kinds(std::string_view source) {
  std::vector<TokenKind> out;
  for (const auto& token : Lexer::lex(source).tokens) out.push_back(token.kind);
  return out;
}

}  // namespace

// Port of PET's native PapyrusLexerTests: every name below must keep stating the behaviour it pins.

TEST(PapyrusLexer, KeywordsAreCaseInsensitive) {
  const std::vector<std::pair<std::string, TokenKind>> cases{
      {"ScriptName", TokenKind::ScriptName},
      {"scriptname", TokenKind::ScriptName},
      {"SCRIPTNAME", TokenKind::ScriptName},
      {"EndIf", TokenKind::EndIf},
      {"endif", TokenKind::EndIf},
      {"AutoReadOnly", TokenKind::AutoReadOnly},
  };
  for (const auto& [word, expected] : cases) {
    EXPECT_EQ(Lexer::lex(word).tokens[0].kind, expected) << word;
  }
}

TEST(PapyrusLexer, KeywordTableMatchesTheLanguageReferenceCount) {
  EXPECT_EQ(fo4::papyrus::Keywords::all().size(), 45u);
}

TEST(PapyrusLexer, KeywordInsideAnIdentifierIsNotAKeyword) {
  EXPECT_EQ(Lexer::lex("IfState").tokens[0].kind, TokenKind::Identifier);
  EXPECT_EQ(Lexer::lex("_ifs").tokens[0].kind, TokenKind::Identifier);
}

TEST(PapyrusLexer, NewlineTerminatesAStatement) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Newline, TokenKind::Identifier, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("x\ny"), expected);
}

TEST(PapyrusLexer, BackslashContinuesAStatement) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Assign, TokenKind::IntLiteral,
      TokenKind::Plus, TokenKind::IntLiteral, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("x = 1 + \\\n 2"), expected);
}

TEST(PapyrusLexer, ContinuationJoinsWhenACommentFollowsTheBackslash) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Assign, TokenKind::IntLiteral,
      TokenKind::Plus, TokenKind::IntLiteral, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("x = 1 + \\ ; keep going\n 2"), expected);
}

TEST(PapyrusLexer, BackslashInsideACommentDoesNotContinueTheLine) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Newline, TokenKind::Identifier, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("; comment \\\nx"), expected);
}

TEST(PapyrusLexer, SingleLineCommentRunsToEndOfLine) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Newline, TokenKind::Identifier, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("a ; b c d\ne"), expected);
}

TEST(PapyrusLexer, BlockCommentSpansLinesAndIsNotASingleLineComment) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Identifier, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("a ;/ still\ncommented\n/; b"), expected);
}

TEST(PapyrusLexer, UnterminatedBlockCommentIsReported) {
  const auto result = Lexer::lex("a ;/ never closed");
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics[0].code, fo4::papyrus::DiagnosticCode::UnterminatedBlockComment);
}

TEST(PapyrusLexer, DocCommentIsATokenCarryingItsInnerText) {
  const auto result = Lexer::lex("{hello\nworld}");
  EXPECT_EQ(result.tokens[0].kind, TokenKind::DocComment);
  EXPECT_EQ(result.tokens[0].text, "hello\nworld");
}

TEST(PapyrusLexer, StringEscapesAreDecodedIntoTheTokenText) {
  EXPECT_EQ(Lexer::lex("\"a\\nb\\t\\\"c\\\"\"").tokens[0].text, "a\nb\t\"c\"");
}

TEST(PapyrusLexer, UnknownEscapeKeepsBothCharactersSoWindowsPathsSurvive) {
  EXPECT_EQ(Lexer::lex("\"Data\\Scripts\"").tokens[0].text, "Data\\Scripts");
}

TEST(PapyrusLexer, UnterminatedStringIsCutAtTheLineEndAndReported) {
  const auto result = Lexer::lex("\"oops\nx");
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics[0].code, fo4::papyrus::DiagnosticCode::UnterminatedString);
  bool sawIdentifier = false;
  for (const auto& token : result.tokens) {
    if (token.kind == TokenKind::Identifier) sawIdentifier = true;
  }
  EXPECT_TRUE(sawIdentifier);
}

TEST(PapyrusLexer, NumericLiteralKinds) {
  const std::vector<std::pair<std::string, TokenKind>> cases{
      {"10", TokenKind::IntLiteral},
      {"0x1F2C8", TokenKind::IntLiteral},
      {"0XAB", TokenKind::IntLiteral},
      {"1.5234", TokenKind::FloatLiteral},
      {"60.0f", TokenKind::FloatLiteral},
      {"3F", TokenKind::FloatLiteral},
  };
  for (const auto& [source, expected] : cases) {
    EXPECT_EQ(Lexer::lex(source).tokens[0].kind, expected) << source;
  }
}

TEST(PapyrusLexer, MinusIsAnOperatorNotPartOfTheLiteral) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Minus, TokenKind::IntLiteral, TokenKind::EndOfFile};
  EXPECT_EQ(kinds("a-1"), expected);
}

TEST(PapyrusLexer, HexLiteralWithoutDigitsIsReported) {
  const auto result = Lexer::lex("0x");
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics[0].code, fo4::papyrus::DiagnosticCode::MalformedNumber);
}

TEST(PapyrusLexer, TwoCharacterOperatorsWinOverOneCharacterOnes) {
  const auto expected = std::vector<TokenKind>{
      TokenKind::Identifier, TokenKind::Equal,
      TokenKind::Identifier, TokenKind::NotEqual,
      TokenKind::Identifier, TokenKind::GreaterEqual,
      TokenKind::Identifier, TokenKind::LessEqual,
      TokenKind::Identifier, TokenKind::And,
      TokenKind::Identifier, TokenKind::Or,
      TokenKind::Identifier, TokenKind::PlusAssign,
      TokenKind::IntLiteral,
      TokenKind::EndOfFile};
  EXPECT_EQ(kinds("a == b != c >= d <= e && f || g += 1"), expected);
}

TEST(PapyrusLexer, SpansCarryOneBasedLineAndColumn) {
  const auto tokens = Lexer::lex("ab\n  cd").tokens;
  const auto cd = std::find_if(tokens.begin(), tokens.end(),
                               [](const auto& token) { return token.text == "cd"; });
  ASSERT_NE(cd, tokens.end());
  EXPECT_EQ(cd->span.line, 2);
  EXPECT_EQ(cd->span.column, 3);
  EXPECT_EQ(cd->span.start, 5);
  EXPECT_EQ(cd->span.length, 2);
}

TEST(PapyrusLexer, CarriageReturnsDoNotShiftLineNumbers) {
  const auto tokens = Lexer::lex("a\r\nb\r\nc").tokens;
  const auto c = std::find_if(tokens.begin(), tokens.end(),
                              [](const auto& token) { return token.text == "c"; });
  ASSERT_NE(c, tokens.end());
  EXPECT_EQ(c->span.line, 3);
}

TEST(PapyrusLexer, UnexpectedCharacterIsReportedAndSkipped) {
  const auto result = Lexer::lex("a @ b");
  ASSERT_EQ(result.diagnostics.size(), 1u);
  EXPECT_EQ(result.diagnostics[0].code, fo4::papyrus::DiagnosticCode::UnexpectedCharacter);
  std::size_t identifiers = 0;
  for (const auto& token : result.tokens) {
    if (token.kind == TokenKind::Identifier) ++identifiers;
  }
  EXPECT_EQ(identifiers, 2u);
}

// Constraint: '#' must stay a qualifier separator for decompiled Script#Struct names.
TEST(PapyrusLexer, HashLexesAsAQualifierSeparatorNotAnUnexpectedCharacter) {
  const auto result = Lexer::lex("a # b");
  EXPECT_TRUE(result.diagnostics.empty());
  const auto hash = std::find_if(result.tokens.begin(), result.tokens.end(),
                                 [](const auto& token) { return token.kind == TokenKind::Hash; });
  ASSERT_NE(hash, result.tokens.end());
  EXPECT_EQ(hash->text, "#");
}
