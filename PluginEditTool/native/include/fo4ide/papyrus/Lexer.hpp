#pragma once

#include <string_view>
#include <vector>

#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Token.hpp>

namespace fo4ide::papyrus {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

// Port of the C# PapyrusLexer. Faithful details: keywords are matched
// case-insensitively, ';' and ';/ ... /;' comments, '\\' line continuations
// (including continuations that skip a trailing ';' comment), 0x hex literals,
// f/F float suffixes, string escape decoding with unknown escapes preserved,
// and { } doc comments. Diagnostics use the same PAP codes and 1-based
// (line, column) spans.
class Lexer {
public:
    static LexResult lex(std::string_view text);

    // Lex into a caller-owned diagnostic bag (used by the parser pipeline).
    static std::vector<Token> lex(std::string_view text, DiagnosticBag& diagnostics);

private:
    Lexer(std::string_view text, DiagnosticBag& diagnostics);

    std::vector<Token> lexAll();
    Token next();

    void skipTrivia();
    bool isContinuation();
    void skipContinuation();
    void skipBlockComment();
    void consumeNewline();

    Token lexWord(int start, int line, int column);
    Token lexNumber(int start, int line, int column);
    Token lexString(int start, int line, int column);
    Token lexDocComment(int start, int line, int column);
    Token lexOperator(int start, int line, int column);

    char current() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char peek(int offset = 1) const {
        return pos_ + offset < text_.size() ? text_[pos_ + offset] : '\0';
    }
    bool atEnd() const { return pos_ >= text_.size(); }
    int column() const { return static_cast<int>(pos_ - lineStart_) + 1; }
    Span spanFrom(int start, int line, int column) const {
        return Span{start, static_cast<int>(pos_ - start), line, column};
    }

    std::string_view text_;
    DiagnosticBag& diagnostics_;
    std::size_t pos_{0};
    int line_{1};
    std::size_t lineStart_{0};
};

}
