#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/papyrus/Lexer.hpp>
#include <fo4ide/papyrus/Token.hpp>

namespace {

using fo4ide::papyrus::LexResult;
using fo4ide::papyrus::Lexer;
using fo4ide::papyrus::TokenKind;

std::vector<TokenKind> kinds(std::string_view source) {
    std::vector<TokenKind> out;
    for (const auto& token : Lexer::lex(source).tokens) out.push_back(token.kind);
    return out;
}

}  // namespace

TEST_CASE("Papyrus keywords are case insensitive") {
    const std::vector<std::pair<std::string, TokenKind>> cases{
        {"ScriptName", TokenKind::ScriptName},
        {"scriptname", TokenKind::ScriptName},
        {"SCRIPTNAME", TokenKind::ScriptName},
        {"EndIf", TokenKind::EndIf},
        {"endif", TokenKind::EndIf},
        {"AutoReadOnly", TokenKind::AutoReadOnly},
    };
    for (const auto& [word, expected] : cases) {
        CHECK(Lexer::lex(word).tokens[0].kind == expected);
    }
}

TEST_CASE("Papyrus keyword table matches the language reference count") {
    CHECK(fo4ide::papyrus::Keywords::all().size() == 45);
}

TEST_CASE("Identifier that merely contains a keyword is not a keyword") {
    CHECK(Lexer::lex("IfState").tokens[0].kind == TokenKind::Identifier);
    CHECK(Lexer::lex("_ifs").tokens[0].kind == TokenKind::Identifier);
}

TEST_CASE("Newline terminates a statement") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Newline, TokenKind::Identifier, TokenKind::EndOfFile};
    CHECK(kinds("x\ny") == expected);
}

TEST_CASE("Backslash continues a statement onto the next line") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Assign, TokenKind::IntLiteral,
        TokenKind::Plus, TokenKind::IntLiteral, TokenKind::EndOfFile};
    CHECK(kinds("x = 1 + \\\n 2") == expected);
}

TEST_CASE("Continuation still joins when a comment follows the backslash") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Assign, TokenKind::IntLiteral,
        TokenKind::Plus, TokenKind::IntLiteral, TokenKind::EndOfFile};
    CHECK(kinds("x = 1 + \\ ; keep going\n 2") == expected);
}

TEST_CASE("Backslash inside a comment does not continue the line") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Newline, TokenKind::Identifier, TokenKind::EndOfFile};
    CHECK(kinds("; comment \\\nx") == expected);
}

TEST_CASE("Single line comment runs to end of line") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Newline, TokenKind::Identifier, TokenKind::EndOfFile};
    CHECK(kinds("a ; b c d\ne") == expected);
}

TEST_CASE("Block comment spans lines and is not a single line comment") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Identifier, TokenKind::EndOfFile};
    CHECK(kinds("a ;/ still\ncommented\n/; b") == expected);
}

TEST_CASE("Unterminated block comment is reported") {
    const auto result = Lexer::lex("a ;/ never closed");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].code == fo4ide::papyrus::DiagnosticCode::UnterminatedBlockComment);
}

TEST_CASE("Doc comment is a token carrying its inner text") {
    const auto result = Lexer::lex("{hello\nworld}");
    CHECK(result.tokens[0].kind == TokenKind::DocComment);
    CHECK(result.tokens[0].text == "hello\nworld");
}

TEST_CASE("String escapes are decoded into the token text") {
    CHECK(Lexer::lex("\"a\\nb\\t\\\"c\\\"\"").tokens[0].text == "a\nb\t\"c\"");
}

TEST_CASE("Unknown escape keeps both characters so windows paths survive") {
    CHECK(Lexer::lex("\"Data\\Scripts\"").tokens[0].text == "Data\\Scripts");
}

TEST_CASE("Unterminated string is cut at the line end and reported") {
    const auto result = Lexer::lex("\"oops\nx");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].code == fo4ide::papyrus::DiagnosticCode::UnterminatedString);
    bool sawIdentifier = false;
    for (const auto& token : result.tokens) {
        if (token.kind == TokenKind::Identifier) sawIdentifier = true;
    }
    CHECK(sawIdentifier);
}

TEST_CASE("Numeric literal kinds") {
    const std::vector<std::pair<std::string, TokenKind>> cases{
        {"10", TokenKind::IntLiteral},
        {"0x1F2C8", TokenKind::IntLiteral},
        {"0XAB", TokenKind::IntLiteral},
        {"1.5234", TokenKind::FloatLiteral},
        {"60.0f", TokenKind::FloatLiteral},
        {"3F", TokenKind::FloatLiteral},
    };
    for (const auto& [source, expected] : cases) {
        CHECK(Lexer::lex(source).tokens[0].kind == expected);
    }
}

TEST_CASE("Minus is an operator not part of the literal") {
    const auto expected = std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Minus, TokenKind::IntLiteral, TokenKind::EndOfFile};
    CHECK(kinds("a-1") == expected);
}

TEST_CASE("Hex literal without digits is reported") {
    const auto result = Lexer::lex("0x");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].code == fo4ide::papyrus::DiagnosticCode::MalformedNumber);
}

TEST_CASE("Two character operators win over one character ones") {
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
    CHECK(kinds("a == b != c >= d <= e && f || g += 1") == expected);
}

TEST_CASE("Spans carry one based line and column") {
    const auto tokens = Lexer::lex("ab\n  cd").tokens;
    const auto cd = std::find_if(tokens.begin(), tokens.end(),
                                 [](const auto& token) { return token.text == "cd"; });
    REQUIRE(cd != tokens.end());
    CHECK(cd->span.line == 2);
    CHECK(cd->span.column == 3);
    CHECK(cd->span.start == 5);
    CHECK(cd->span.length == 2);
}

TEST_CASE("Carriage returns do not shift line numbers") {
    const auto tokens = Lexer::lex("a\r\nb\r\nc").tokens;
    const auto c = std::find_if(tokens.begin(), tokens.end(),
                                [](const auto& token) { return token.text == "c"; });
    REQUIRE(c != tokens.end());
    CHECK(c->span.line == 3);
}

TEST_CASE("Unexpected character is reported and skipped") {
    const auto result = Lexer::lex("a @ b");
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics[0].code == fo4ide::papyrus::DiagnosticCode::UnexpectedCharacter);
    std::size_t identifiers = 0;
    for (const auto& token : result.tokens) {
        if (token.kind == TokenKind::Identifier) ++identifiers;
    }
    CHECK(identifiers == 2);
}

TEST_CASE("Hash lexes as a qualifier separator, not an unexpected character") {
    // Deliberate deviation from the C# oracle: the decompiler emits
    // Script#Struct type names (with #), so the lexer accepts '#' as a
    // ':'-equivalent in qualified names instead of rejecting the script.
    const auto result = Lexer::lex("a # b");
    CHECK(result.diagnostics.empty());
    const auto hash = std::find_if(result.tokens.begin(), result.tokens.end(),
                                   [](const auto& token) { return token.kind == TokenKind::Hash; });
    REQUIRE(hash != result.tokens.end());
    CHECK(hash->text == "#");
}
