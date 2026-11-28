#include <fo4/papyrus/lexer.hpp>

#include <algorithm>
#include <utility>

namespace fo4::papyrus {
namespace {

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isLetterOrDigit(char c) {
    return isLetter(c) || isDigit(c);
}

bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}  // namespace

LexResult Lexer::lex(std::string_view text) {
    DiagnosticBag bag;
    auto tokens = lex(text, bag);
    return LexResult{std::move(tokens), bag.items()};
}

std::vector<Token> Lexer::lex(std::string_view text, DiagnosticBag& diagnostics) {
    return Lexer(text, diagnostics).lexAll();
}

Lexer::Lexer(std::string_view text, DiagnosticBag& diagnostics)
    : text_(text), diagnostics_(diagnostics) {}

std::vector<Token> Lexer::lexAll() {
    std::vector<Token> tokens;
    tokens.reserve(std::max<std::size_t>(16, text_.size() / 4));
    while (true) {
        auto token = next();
        tokens.push_back(std::move(token));
        if (tokens.back().kind == TokenKind::EndOfFile) break;
    }
    return tokens;
}

Token Lexer::next() {
    skipTrivia();

    const auto start = static_cast<int>(pos_);
    const auto line = line_;
    const auto col = column();

    if (atEnd()) {
        return Token{TokenKind::EndOfFile, "", spanFrom(start, line, col)};
    }

    const auto c = current();

    if (c == '\r' || c == '\n') {
        consumeNewline();
        return Token{TokenKind::Newline, "\n", spanFrom(start, line, col)};
    }

    if (c == '_' || isLetter(c)) return lexWord(start, line, col);

    if (isDigit(c)) return lexNumber(start, line, col);

    if (c == '"') return lexString(start, line, col);

    if (c == '{') return lexDocComment(start, line, col);

    return lexOperator(start, line, col);
}

void Lexer::skipTrivia() {
    while (!atEnd()) {
        const auto c = current();

        if (c == ' ' || c == '\t') {
            ++pos_;
            continue;
        }

        if (c == ';' && peek() == '/') {
            skipBlockComment();
            continue;
        }

        if (c == ';') {
            while (!atEnd() && current() != '\r' && current() != '\n') ++pos_;
            continue;
        }

        if (c == '\\' && isContinuation()) {
            skipContinuation();
            continue;
        }

        return;
    }
}

bool Lexer::isContinuation() {
    auto i = pos_ + 1;
    while (i < text_.size() && (text_[i] == ' ' || text_[i] == '\t')) ++i;

    if (i < text_.size() && text_[i] == ';' && !(i + 1 < text_.size() && text_[i + 1] == '/')) {
        while (i < text_.size() && text_[i] != '\r' && text_[i] != '\n') ++i;
    }
    return i >= text_.size() || text_[i] == '\r' || text_[i] == '\n';
}

void Lexer::skipContinuation() {
    ++pos_;
    while (!atEnd() && current() != '\r' && current() != '\n') ++pos_;
    if (!atEnd()) consumeNewline();
}

void Lexer::skipBlockComment() {
    const auto start = static_cast<int>(pos_);
    const auto line = line_;
    const auto col = column();
    pos_ += 2;
    while (!atEnd()) {
        if (current() == '/' && peek() == ';') {
            pos_ += 2;
            return;
        }
        if (current() == '\r' || current() == '\n') consumeNewline();
        else ++pos_;
    }
    diagnostics_.report(
        DiagnosticCode::UnterminatedBlockComment,
        "Block comment is never closed; expected '/;'.",
        spanFrom(start, line, col));
}

void Lexer::consumeNewline() {
    if (current() == '\r' && peek() == '\n') pos_ += 2;
    else ++pos_;
    ++line_;
    lineStart_ = pos_;
}

Token Lexer::lexWord(int start, int line, int column) {
    while (!atEnd() && (current() == '_' || isLetterOrDigit(current()))) ++pos_;
    const auto text = std::string(text_.substr(static_cast<std::size_t>(start),
                                               pos_ - static_cast<std::size_t>(start)));
    TokenKind kind;
    const auto kind2 = Keywords::tryGet(text, kind) ? kind : TokenKind::Identifier;
    return Token{kind2, text, spanFrom(start, line, column)};
}

Token Lexer::lexNumber(int start, int line, int column) {
    if (current() == '0' && (peek() == 'x' || peek() == 'X')) {
        pos_ += 2;
        auto digits = 0;
        while (!atEnd() && isHexDigit(current())) {
            ++pos_;
            ++digits;
        }
        const auto hex = std::string(text_.substr(static_cast<std::size_t>(start),
                                                  pos_ - static_cast<std::size_t>(start)));
        if (digits == 0) {
            diagnostics_.report(
                DiagnosticCode::MalformedNumber,
                "Hexadecimal literal has no digits after '0x'.",
                spanFrom(start, line, column));
        }
        return Token{TokenKind::IntLiteral, hex, spanFrom(start, line, column)};
    }

    while (!atEnd() && isDigit(current())) ++pos_;

    auto isFloat = false;

    if (current() == '.' && isDigit(peek())) {
        isFloat = true;
        ++pos_;
        while (!atEnd() && isDigit(current())) ++pos_;
    }

    if (current() == 'f' || current() == 'F') {
        isFloat = true;
        ++pos_;
    }

    const auto text = std::string(text_.substr(static_cast<std::size_t>(start),
                                               pos_ - static_cast<std::size_t>(start)));
    return Token{isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral,
                 text, spanFrom(start, line, column)};
}

Token Lexer::lexString(int start, int line, int column) {
    ++pos_;
    std::string text;
    auto terminated = false;

    while (!atEnd()) {
        const auto c = current();
        if (c == '"') {
            ++pos_;
            terminated = true;
            break;
        }
        if (c == '\r' || c == '\n') break;

        if (c == '\\' && pos_ + 1 < text_.size()) {
            ++pos_;
            const auto esc = current();
            ++pos_;
            switch (esc) {
                case 'n': text.push_back('\n'); break;
                case 't': text.push_back('\t'); break;
                case 'r': text.push_back('\r'); break;
                case '\\': text.push_back('\\'); break;
                case '"': text.push_back('"'); break;
                default:
                    text.push_back('\\');
                    text.push_back(esc);
                    break;
            }
            continue;
        }

        text.push_back(c);
        ++pos_;
    }

    if (!terminated) {
        diagnostics_.report(
            DiagnosticCode::UnterminatedString,
            "String literal is never closed.",
            spanFrom(start, line, column));
    }

    return Token{TokenKind::StringLiteral, std::move(text), spanFrom(start, line, column)};
}

Token Lexer::lexDocComment(int start, int line, int column) {
    ++pos_;
    const auto contentStart = pos_;
    auto terminated = false;
    while (!atEnd()) {
        if (current() == '}') {
            terminated = true;
            break;
        }
        if (current() == '\r' || current() == '\n') consumeNewline();
        else ++pos_;
    }

    const auto content = std::string(text_.substr(contentStart, pos_ - contentStart));
    if (terminated) ++pos_;
    else {
        diagnostics_.report(
            DiagnosticCode::UnterminatedDocComment,
            "Documentation comment is never closed; expected '}'.",
            spanFrom(start, line, column));
    }

    return Token{TokenKind::DocComment, content, spanFrom(start, line, column)};
}

Token Lexer::lexOperator(int start, int line, int column) {
    const auto c = current();
    const auto two = pos_ + 1 < text_.size()
                         ? std::string(text_.substr(pos_, 2))
                         : std::string{};

    auto kind = TokenKind::EndOfFile;
    if (two == "==") kind = TokenKind::Equal;
    else if (two == "!=") kind = TokenKind::NotEqual;
    else if (two == "<=") kind = TokenKind::LessEqual;
    else if (two == ">=") kind = TokenKind::GreaterEqual;
    else if (two == "||") kind = TokenKind::Or;
    else if (two == "&&") kind = TokenKind::And;
    else if (two == "+=") kind = TokenKind::PlusAssign;
    else if (two == "-=") kind = TokenKind::MinusAssign;
    else if (two == "*=") kind = TokenKind::StarAssign;
    else if (two == "/=") kind = TokenKind::SlashAssign;
    else if (two == "%=") kind = TokenKind::PercentAssign;

    if (kind != TokenKind::EndOfFile) {
        pos_ += 2;
        return Token{kind, two, spanFrom(start, line, column)};
    }

    switch (c) {
        case '(': kind = TokenKind::LParen; break;
        case ')': kind = TokenKind::RParen; break;
        case '[': kind = TokenKind::LBracket; break;
        case ']': kind = TokenKind::RBracket; break;
        case ',': kind = TokenKind::Comma; break;
        case '.': kind = TokenKind::Dot; break;
        case ':': kind = TokenKind::Colon; break;
        case '#': kind = TokenKind::Hash; break;
        case '=': kind = TokenKind::Assign; break;
        case '<': kind = TokenKind::Less; break;
        case '>': kind = TokenKind::Greater; break;
        case '!': kind = TokenKind::Not; break;
        case '+': kind = TokenKind::Plus; break;
        case '-': kind = TokenKind::Minus; break;
        case '*': kind = TokenKind::Star; break;
        case '/': kind = TokenKind::Slash; break;
        case '%': kind = TokenKind::Percent; break;
        default:
            ++pos_;
            diagnostics_.report(
                DiagnosticCode::UnexpectedCharacter,
                "Unexpected character '" + std::string(1, c) + "'.",
                spanFrom(start, line, column));
            return next();
    }

    ++pos_;
    return Token{kind, std::string(1, c), spanFrom(start, line, column)};
}

}
