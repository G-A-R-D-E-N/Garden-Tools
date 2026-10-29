#pragma once

#include <string_view>
#include <vector>

#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/token.hpp>

namespace fo4::papyrus {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

// Keyword matching must stay case-insensitive and spans must stay 1-based (line, column).
class Lexer {
public:
    static LexResult lex(std::string_view text);

    // The caller-owned bag must receive the diagnostics, not a private copy.
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
