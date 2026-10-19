#include <fo4/papyrus_source/lexer.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace fo4::psc {

namespace {

struct Keyword {
  std::string_view text;
  TokenKind kind;
};

constexpr Keyword kKeywords[] = {
    {"as", TokenKind::KwAs},
    {"auto", TokenKind::KwAuto},
    {"autoreadonly", TokenKind::KwAutoReadOnly},
    {"betaonly", TokenKind::KwBetaOnly},
    {"conditional", TokenKind::KwConditional},
    {"const", TokenKind::KwConst},
    {"customevent", TokenKind::KwCustomEvent},
    {"debugonly", TokenKind::KwDebugOnly},
    {"else", TokenKind::KwElse},
    {"elseif", TokenKind::KwElseIf},
    {"endevent", TokenKind::KwEndEvent},
    {"endfunction", TokenKind::KwEndFunction},
    {"endgroup", TokenKind::KwEndGroup},
    {"endif", TokenKind::KwEndIf},
    {"endproperty", TokenKind::KwEndProperty},
    {"endstate", TokenKind::KwEndState},
    {"endstruct", TokenKind::KwEndStruct},
    {"endwhile", TokenKind::KwEndWhile},
    {"event", TokenKind::KwEvent},
    {"extends", TokenKind::KwExtends},
    {"false", TokenKind::KwFalse},
    {"function", TokenKind::KwFunction},
    {"global", TokenKind::KwGlobal},
    {"group", TokenKind::KwGroup},
    {"hidden", TokenKind::KwHidden},
    {"if", TokenKind::KwIf},
    {"import", TokenKind::KwImport},
    {"is", TokenKind::KwIs},
    {"mandatory", TokenKind::KwMandatory},
    {"native", TokenKind::KwNative},
    {"new", TokenKind::KwNew},
    {"none", TokenKind::KwNone},
    {"property", TokenKind::KwProperty},
    {"return", TokenKind::KwReturn},
    {"scriptname", TokenKind::KwScriptName},
    {"state", TokenKind::KwState},
    {"struct", TokenKind::KwStruct},
    {"true", TokenKind::KwTrue},
    {"while", TokenKind::KwWhile},
};

char ascii_lower(char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

bool equals_ignore_case(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (ascii_lower(left[i]) != ascii_lower(right[i])) return false;
  }
  return true;
}

bool is_identifier_start(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool is_identifier_char(char ch) { return is_identifier_start(ch) || (ch >= '0' && ch <= '9'); }

bool is_digit(char ch) { return ch >= '0' && ch <= '9'; }

bool is_hex_digit(char ch) {
  return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool is_horizontal_space(char ch) { return ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v'; }

TokenKind keyword_kind(std::string_view text) {
  for (const Keyword& entry : kKeywords) {
    if (equals_ignore_case(entry.text, text)) return entry.kind;
  }
  return TokenKind::Identifier;
}

std::uint32_t hex_value(std::string_view text) {
  std::uint32_t value = 0;
  for (const char ch : text) {
    std::uint32_t digit = 0;
    if (ch >= '0' && ch <= '9') {
      digit = static_cast<std::uint32_t>(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = static_cast<std::uint32_t>(ch - 'a' + 10);
    } else {
      digit = static_cast<std::uint32_t>(ch - 'A' + 10);
    }
    value = value * 16U + digit;
  }
  return value;
}

std::string describe_character(char ch) {
  const auto code = static_cast<unsigned char>(ch);
  if (code >= 0x20 && code < 0x7F) return std::string("'") + ch + "'";
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string out = "byte 0x";
  out.push_back(digits[(code >> 4) & 0xF]);
  out.push_back(digits[code & 0xF]);
  return out;
}

class Lexer {
public:
  explicit Lexer(const std::string& source) : source_(source) {
    if (source_.size() >= 3 && static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB && static_cast<unsigned char>(source_[2]) == 0xBF) {
      index_ = 3;
    }
  }

  LexResult run() {
    LexResult result;
    for (;;) {
      Token token = next();
      if (!error_.ok()) {
        result.error = error_;
        return result;
      }
      const bool finished = token.kind == TokenKind::EndOfFile;
      result.tokens.push_back(std::move(token));
      if (finished) return result;
    }
  }

private:
  SourcePosition position() const {
    SourcePosition out;
    out.line = line_;
    out.column = column_;
    out.offset = static_cast<std::uint32_t>(index_);
    return out;
  }

  bool at_end() const { return index_ >= source_.size(); }

  char peek(std::size_t ahead = 0) const {
    const std::size_t at = index_ + ahead;
    return at < source_.size() ? source_[at] : '\0';
  }

  void advance() {
    if (index_ >= source_.size()) return;
    const char ch = source_[index_++];
    if (ch == '\r') {
      if (index_ < source_.size() && source_[index_] == '\n') ++index_;
      ++line_;
      column_ = 1;
      return;
    }
    if (ch == '\n') {
      ++line_;
      column_ = 1;
      return;
    }
    ++column_;
  }

  void fail(LexErrorKind kind, const SourcePosition& at, const std::string& message) {
    if (!error_.ok()) return;
    error_.kind = kind;
    error_.message = message;
    error_.position = at;
  }

  Token make(TokenKind kind, const SourcePosition& at) {
    Token token;
    token.kind = kind;
    token.position = at;
    return token;
  }

  bool skip_block_comment() {
    const SourcePosition at = position();
    advance();
    advance();
    for (;;) {
      if (at_end()) {
        fail(LexErrorKind::UnterminatedBlockComment, at, "unterminated block comment");
        return false;
      }
      if (peek() == '/' && peek(1) == ';') {
        advance();
        advance();
        return true;
      }
      advance();
    }
  }

  void skip_line_comment() {
    while (!at_end() && peek() != '\n' && peek() != '\r') advance();
  }

  bool skip_doc_comment(std::string& text) {
    const SourcePosition at = position();
    advance();
    const std::size_t start = index_;
    for (;;) {
      if (at_end()) {
        fail(LexErrorKind::UnterminatedDocComment, at, "unterminated documentation comment");
        return false;
      }
      if (peek() == '}') {
        text.assign(source_, start, index_ - start);
        advance();
        return true;
      }
      advance();
    }
  }

  void skip_trivia() {
    for (;;) {
      if (at_end()) return;
      const char ch = peek();
      if (is_horizontal_space(ch)) {
        advance();
        continue;
      }
      if (ch == '\\') {
        std::size_t probe = index_ + 1;
        while (probe < source_.size() && is_horizontal_space(source_[probe])) ++probe;
        if (probe < source_.size() && (source_[probe] == '\n' || source_[probe] == '\r')) {
          while (index_ < probe) advance();
          advance();
          continue;
        }
        return;
      }
      if (ch == ';' && peek(1) == '/') {
        if (!skip_block_comment()) return;
        continue;
      }
      if (ch == ';') {
        skip_line_comment();
        continue;
      }
      return;
    }
  }

  Token lex_identifier(const SourcePosition& at) {
    const std::size_t start = index_;
    while (!at_end() && is_identifier_char(peek())) advance();
    const std::string_view text(source_.data() + start, index_ - start);
    Token token = make(keyword_kind(text), at);
    token.text.assign(text);
    return token;
  }

  Token lex_number(const SourcePosition& at) {
    const std::size_t start = index_;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      advance();
      advance();
      const std::size_t digits = index_;
      while (!at_end() && is_hex_digit(peek())) advance();
      if (index_ == digits) {
        fail(LexErrorKind::InvalidNumber, at, "hexadecimal literal without digits");
        return make(TokenKind::Integer, at);
      }
      Token token = make(TokenKind::Integer, at);
      token.text.assign(source_, start, index_ - start);
      token.intValue = static_cast<std::int32_t>(hex_value(token.text.substr(2)));
      return token;
    }
    bool isFloat = false;
    while (!at_end() && is_digit(peek())) advance();
    if (peek() == '.' && is_digit(peek(1))) {
      isFloat = true;
      advance();
      while (!at_end() && is_digit(peek())) advance();
    } else if (peek() == '.' && !is_identifier_start(peek(1))) {
      isFloat = true;
      advance();
    }
    if (peek() == 'e' || peek() == 'E') {
      std::size_t probe = index_ + 1;
      if (probe < source_.size() && (source_[probe] == '+' || source_[probe] == '-')) ++probe;
      if (probe < source_.size() && is_digit(source_[probe])) {
        isFloat = true;
        while (index_ < probe) advance();
        while (!at_end() && is_digit(peek())) advance();
      }
    }
    Token token = make(isFloat ? TokenKind::Float : TokenKind::Integer, at);
    token.text.assign(source_, start, index_ - start);
    if (isFloat) {
      token.floatValue = parse_double(token.text);
    } else {
      token.intValue = wrap_integer(token.text);
    }
    return token;
  }

  static std::int64_t wrap_integer(const std::string& text) {
    std::uint64_t value = 0;
    for (const char ch : text) value = value * 10U + static_cast<std::uint64_t>(ch - '0');
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
  }

  static double parse_double(const std::string& text) {
    double value = 0.0;
    std::size_t at = 0;
    while (at < text.size() && is_digit(text[at])) {
      value = value * 10.0 + static_cast<double>(text[at] - '0');
      ++at;
    }
    if (at < text.size() && text[at] == '.') {
      ++at;
      double scale = 0.1;
      while (at < text.size() && is_digit(text[at])) {
        value += static_cast<double>(text[at] - '0') * scale;
        scale *= 0.1;
        ++at;
      }
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
      ++at;
      bool negative = false;
      if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
        negative = text[at] == '-';
        ++at;
      }
      constexpr int kExponentCap = 10000;
      int exponent = 0;
      while (at < text.size() && is_digit(text[at])) {
        exponent = std::min(kExponentCap, exponent * 10 + (text[at] - '0'));
        ++at;
      }
      for (int step = 0; step < exponent; ++step) value = negative ? value / 10.0 : value * 10.0;
    }
    return value;
  }

  Token lex_string(const SourcePosition& at) {
    advance();
    std::string value;
    for (;;) {
      if (at_end()) {
        fail(LexErrorKind::UnterminatedString, at, "unterminated string literal");
        return make(TokenKind::String, at);
      }
      const char ch = peek();
      if (ch == '\n' || ch == '\r') {
        fail(LexErrorKind::UnterminatedString, at, "unterminated string literal");
        return make(TokenKind::String, at);
      }
      if (ch == '"') {
        advance();
        Token token = make(TokenKind::String, at);
        token.stringValue = value;
        token.text = "\"" + value + "\"";
        return token;
      }
      if (ch == '\\') {
        const char escaped = peek(1);
        if (escaped == '\n' || escaped == '\r') {
          advance();
          advance();
          continue;
        }
        if (escaped == '\\' || escaped == '"' || escaped == '\'') {
          value.push_back(escaped);
          advance();
          advance();
          continue;
        }
        if (escaped == 'n') {
          value.push_back('\n');
          advance();
          advance();
          continue;
        }
        if (escaped == 'r') {
          value.push_back('\r');
          advance();
          advance();
          continue;
        }
        if (escaped == 't') {
          value.push_back('\t');
          advance();
          advance();
          continue;
        }
      }
      value.push_back(ch);
      advance();
    }
  }

  Token lex_punctuation(const SourcePosition& at) {
    const char ch = peek();
    const char following = peek(1);
    advance();
    switch (ch) {
      case '(':
        ++parenDepth_;
        return make(TokenKind::LParen, at);
      case ')':
        if (parenDepth_ > 0) --parenDepth_;
        return make(TokenKind::RParen, at);
      case '[':
        ++parenDepth_;
        return make(TokenKind::LBracket, at);
      case ']':
        if (parenDepth_ > 0) --parenDepth_;
        return make(TokenKind::RBracket, at);
      case ',':
        return make(TokenKind::Comma, at);
      case '.':
        return make(TokenKind::Dot, at);
      case ':':
        return make(TokenKind::Colon, at);
      case '+':
        if (following == '=') {
          advance();
          return make(TokenKind::PlusAssign, at);
        }
        return make(TokenKind::Plus, at);
      case '-':
        if (following == '=') {
          advance();
          return make(TokenKind::MinusAssign, at);
        }
        return make(TokenKind::Minus, at);
      case '*':
        if (following == '=') {
          advance();
          return make(TokenKind::StarAssign, at);
        }
        return make(TokenKind::Star, at);
      case '/':
        if (following == '=') {
          advance();
          return make(TokenKind::SlashAssign, at);
        }
        if (following == ';') {
          fail(LexErrorKind::InvalidCharacter, at, "unexpected '/;' outside a block comment");
        }
        return make(TokenKind::Slash, at);
      case '%':
        if (following == '=') {
          advance();
          return make(TokenKind::PercentAssign, at);
        }
        return make(TokenKind::Percent, at);
      case '=':
        if (following == '=') {
          advance();
          return make(TokenKind::Equal, at);
        }
        return make(TokenKind::Assign, at);
      case '!':
        if (following == '=') {
          advance();
          return make(TokenKind::NotEqual, at);
        }
        return make(TokenKind::Not, at);
      case '<':
        if (following == '=') {
          advance();
          return make(TokenKind::LessEqual, at);
        }
        return make(TokenKind::Less, at);
      case '>':
        if (following == '=') {
          advance();
          return make(TokenKind::GreaterEqual, at);
        }
        return make(TokenKind::Greater, at);
      case '&':
        if (following == '&') {
          advance();
          return make(TokenKind::AndAnd, at);
        }
        fail(LexErrorKind::InvalidCharacter, at, "unexpected '&'");
        return make(TokenKind::EndOfFile, at);
      case '|':
        if (following == '|') {
          advance();
          return make(TokenKind::OrOr, at);
        }
        fail(LexErrorKind::InvalidCharacter, at, "unexpected '|'");
        return make(TokenKind::EndOfFile, at);
      default:
        fail(LexErrorKind::InvalidCharacter, at, "unexpected character " + describe_character(ch));
        return make(TokenKind::EndOfFile, at);
    }
  }

  Token next() {
    for (;;) {
      skip_trivia();
      if (!error_.ok()) return Token{};
      if (at_end()) return make(TokenKind::EndOfFile, position());
      const char pending = peek();
      if (pending == '\n' || pending == '\r') {
        const SourcePosition at = position();
        advance();
        if (parenDepth_ > 0) continue;
        return make(TokenKind::NewLine, at);
      }
      break;
    }
    const SourcePosition at = position();
    const char first = peek();
    if (is_identifier_start(first)) return lex_identifier(at);
    if (is_digit(first)) return lex_number(at);
    if (first == '"') return lex_string(at);
    if (first == '{') {
      std::string text;
      if (!skip_doc_comment(text)) return Token{};
      Token token = make(TokenKind::DocComment, at);
      token.text = std::move(text);
      return token;
    }
    return lex_punctuation(at);
  }

  const std::string& source_;
  std::size_t index_{0};
  std::uint32_t line_{1};
  std::uint32_t column_{1};
  std::uint32_t parenDepth_{0};
  LexError error_;
};

}  // namespace

const char* token_kind_name(TokenKind kind) {
  switch (kind) {
    case TokenKind::EndOfFile:
      return "end of file";
    case TokenKind::NewLine:
      return "end of line";
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::Integer:
      return "integer literal";
    case TokenKind::Float:
      return "float literal";
    case TokenKind::String:
      return "string literal";
    case TokenKind::DocComment:
      return "documentation comment";
    case TokenKind::KwScriptName:
      return "ScriptName";
    case TokenKind::KwExtends:
      return "Extends";
    case TokenKind::KwImport:
      return "Import";
    case TokenKind::KwProperty:
      return "Property";
    case TokenKind::KwEndProperty:
      return "EndProperty";
    case TokenKind::KwFunction:
      return "Function";
    case TokenKind::KwEndFunction:
      return "EndFunction";
    case TokenKind::KwEvent:
      return "Event";
    case TokenKind::KwEndEvent:
      return "EndEvent";
    case TokenKind::KwState:
      return "State";
    case TokenKind::KwEndState:
      return "EndState";
    case TokenKind::KwGroup:
      return "Group";
    case TokenKind::KwEndGroup:
      return "EndGroup";
    case TokenKind::KwStruct:
      return "Struct";
    case TokenKind::KwEndStruct:
      return "EndStruct";
    case TokenKind::KwCustomEvent:
      return "CustomEvent";
    case TokenKind::KwNative:
      return "Native";
    case TokenKind::KwGlobal:
      return "Global";
    case TokenKind::KwDebugOnly:
      return "DebugOnly";
    case TokenKind::KwBetaOnly:
      return "BetaOnly";
    case TokenKind::KwConditional:
      return "Conditional";
    case TokenKind::KwHidden:
      return "Hidden";
    case TokenKind::KwConst:
      return "Const";
    case TokenKind::KwAuto:
      return "Auto";
    case TokenKind::KwAutoReadOnly:
      return "AutoReadOnly";
    case TokenKind::KwMandatory:
      return "Mandatory";
    case TokenKind::KwIf:
      return "If";
    case TokenKind::KwElseIf:
      return "ElseIf";
    case TokenKind::KwElse:
      return "Else";
    case TokenKind::KwEndIf:
      return "EndIf";
    case TokenKind::KwWhile:
      return "While";
    case TokenKind::KwEndWhile:
      return "EndWhile";
    case TokenKind::KwReturn:
      return "Return";
    case TokenKind::KwNew:
      return "New";
    case TokenKind::KwAs:
      return "As";
    case TokenKind::KwIs:
      return "Is";
    case TokenKind::KwTrue:
      return "True";
    case TokenKind::KwFalse:
      return "False";
    case TokenKind::KwNone:
      return "None";
    case TokenKind::LParen:
      return "'('";
    case TokenKind::RParen:
      return "')'";
    case TokenKind::LBracket:
      return "'['";
    case TokenKind::RBracket:
      return "']'";
    case TokenKind::Comma:
      return "','";
    case TokenKind::Dot:
      return "'.'";
    case TokenKind::Colon:
      return "':'";
    case TokenKind::Assign:
      return "'='";
    case TokenKind::PlusAssign:
      return "'+='";
    case TokenKind::MinusAssign:
      return "'-='";
    case TokenKind::StarAssign:
      return "'*='";
    case TokenKind::SlashAssign:
      return "'/='";
    case TokenKind::PercentAssign:
      return "'%='";
    case TokenKind::Plus:
      return "'+'";
    case TokenKind::Minus:
      return "'-'";
    case TokenKind::Star:
      return "'*'";
    case TokenKind::Slash:
      return "'/'";
    case TokenKind::Percent:
      return "'%'";
    case TokenKind::Equal:
      return "'=='";
    case TokenKind::NotEqual:
      return "'!='";
    case TokenKind::Less:
      return "'<'";
    case TokenKind::LessEqual:
      return "'<='";
    case TokenKind::Greater:
      return "'>'";
    case TokenKind::GreaterEqual:
      return "'>='";
    case TokenKind::AndAnd:
      return "'&&'";
    case TokenKind::OrOr:
      return "'||'";
    case TokenKind::Not:
      return "'!'";
  }
  return "token";
}

LexResult lex(const std::string& source) {
  Lexer lexer(source);
  return lexer.run();
}

}  // namespace fo4::psc
