#include "fo4/as3/lexer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::as3 {
namespace {

constexpr std::array<std::string_view, 44> k_keywords{
    "as",         "break",   "case",       "catch",    "class",     "const",
    "continue",   "default", "delete",     "do",       "else",      "extends",
    "false",      "finally", "for",        "function", "if",        "implements",
    "import",     "in",      "instanceof", "interface", "internal", "is",
    "namespace",  "new",     "null",       "package",  "private",   "protected",
    "public",     "return",  "super",      "switch",   "this",      "throw",
    "true",       "try",     "typeof",     "use",      "var",       "void",
    "while",      "with"};

constexpr std::array<std::string_view, 54> k_punctuators{
    ">>>=",
    "...",  "===",  "!==",  "<<=", ">>>", ">>=", "&&=", "||=",
    "==",   "!=",   "<=",   ">=",  "+=",  "-=",  "*=",  "/=",  "%=",  "&=",  "|=",  "^=",
    "<<",   ">>",   "&&",   "||",  "++",  "--",  "::",  "..",
    "{",    "}",    "(",    ")",   "[",   "]",   ";",   ",",   ".",   "<",   ">",   "+",
    "-",    "*",    "/",    "%",   "&",   "|",   "^",   "~",   "!",   "?",   ":",   "=",
    "@"};

bool ident_start(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_' ||
         value == '$';
}

bool ident_char(char value) {
  return ident_start(value) || (value >= '0' && value <= '9');
}

bool digit(char value) {
  return value >= '0' && value <= '9';
}

bool hex_digit(char value) {
  return digit(value) || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

int hex_value(char value) {
  if (digit(value)) return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return value - 'A' + 10;
}

class Scanner {
public:
  explicit Scanner(std::string_view source) : source_(source) {}

  std::vector<Token> run() {
    while (index_ < source_.size()) {
      const auto start = index_;
      const auto start_line = line_;
      const char value = source_[index_];
      if (value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' ||
          value == '\v') {
        skip_space();
        continue;
      }
      if (value == '/' && peek(1) == '/') {
        skip_line_comment(start);
        continue;
      }
      if (value == '/' && peek(1) == '*') {
        skip_block_comment(start, start_line);
        continue;
      }
      if (value == '"' || value == '\'') {
        scan_string(start, start_line, value);
        continue;
      }
      if (ident_start(value)) {
        scan_identifier(start, start_line);
        continue;
      }
      if (digit(value) || (value == '.' && digit(peek(1)))) {
        scan_number(start, start_line);
        continue;
      }
      if (value == '/' && regex_allowed()) {
        scan_regex(start, start_line);
        continue;
      }
      if (value == '#') {
        auto end = index_;
        while (end < source_.size() && source_[end] != '\n') ++end;
        emit(TokenKind::error, start, end, start_line, {},
             "preprocessor directive is not supported");
        continue;
      }
      scan_punct(start, start_line);
    }
    Token end;
    end.kind = TokenKind::end_of_file;
    end.offset = source_.size();
    end.line = line_;
    tokens_.push_back(std::move(end));
    return std::move(tokens_);
  }

private:
  char peek(std::size_t ahead) const {
    const auto index = index_ + ahead;
    return index < source_.size() ? source_[index] : '\0';
  }

  void advance_to(std::size_t end) {
    for (std::size_t index = index_; index < end && index < source_.size(); ++index) {
      if (source_[index] == '\n') ++line_;
    }
    index_ = end;
  }

  void emit(TokenKind kind, std::size_t start, std::size_t end, std::size_t start_line,
            std::string value = {}, std::string error = {}) {
    Token token;
    token.kind = kind;
    token.text = std::string{source_.substr(start, end - start)};
    token.value = std::move(value);
    token.offset = start;
    token.line = start_line;
    token.error = std::move(error);
    tokens_.push_back(std::move(token));
    advance_to(end);
  }

  void skip_space() {
    auto end = index_;
    while (end < source_.size()) {
      const char value = source_[end];
      if (value != ' ' && value != '\t' && value != '\r' && value != '\n' && value != '\f' &&
          value != '\v') {
        break;
      }
      ++end;
    }
    advance_to(end);
  }

  void skip_line_comment(std::size_t start) {
    auto end = start + 2;
    while (end < source_.size() && source_[end] != '\n') ++end;
    advance_to(end);
  }

  void skip_block_comment(std::size_t start, std::size_t start_line) {
    const auto close = source_.find("*/", start + 2);
    if (close == std::string_view::npos) {
      emit(TokenKind::error, start, source_.size(), start_line, {},
           "unterminated block comment");
      return;
    }
    advance_to(close + 2);
  }

  void scan_identifier(std::size_t start, std::size_t start_line) {
    auto end = start;
    while (end < source_.size() && ident_char(source_[end])) ++end;
    const auto text = source_.substr(start, end - start);
    emit(is_keyword(text) ? TokenKind::keyword : TokenKind::identifier, start, end, start_line);
  }

  void scan_number(std::size_t start, std::size_t start_line) {
    auto end = start;
    if (source_[start] == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      end = start + 2;
      if (end >= source_.size() || !hex_digit(source_[end])) {
        while (end < source_.size() && hex_digit(source_[end])) ++end;
        emit(TokenKind::error, start, end, start_line, {}, "hex literal has no digits");
        return;
      }
      while (end < source_.size() && hex_digit(source_[end])) ++end;
    } else {
      if (source_[start] == '.') {
        end = start + 1;
      } else {
        while (end < source_.size() && digit(source_[end])) ++end;
      }
      while (end < source_.size() && digit(source_[end])) ++end;
      if (end < source_.size() && source_[end] == '.') {
        ++end;
        while (end < source_.size() && digit(source_[end])) ++end;
      }
      if (end < source_.size() && (source_[end] == 'e' || source_[end] == 'E')) {
        auto exponent = end + 1;
        if (exponent < source_.size() && (source_[exponent] == '+' || source_[exponent] == '-')) {
          ++exponent;
        }
        const auto digits_start = exponent;
        while (exponent < source_.size() && digit(source_[exponent])) ++exponent;
        if (exponent == digits_start) {
          emit(TokenKind::error, start, exponent, start_line, {}, "exponent has no digits");
          return;
        }
        end = exponent;
      }
    }
    if (end < source_.size() && ident_start(source_[end])) {
      while (end < source_.size() && ident_char(source_[end])) ++end;
      emit(TokenKind::error, start, end, start_line, {}, "number has an identifier suffix");
      return;
    }
    emit(TokenKind::number, start, end, start_line);
  }

  void scan_string(std::size_t start, std::size_t start_line, char quote) {
    std::string value;
    auto end = start + 1;
    while (end < source_.size()) {
      const char current = source_[end];
      if (current == quote) {
        emit(TokenKind::string, start, end + 1, start_line, std::move(value));
        return;
      }
      if (current == '\n' || current == '\r') {
        emit(TokenKind::error, start, end, start_line, {}, "unterminated string literal");
        return;
      }
      if (current != '\\') {
        value.push_back(current);
        ++end;
        continue;
      }
      if (end + 1 >= source_.size()) {
        emit(TokenKind::error, start, source_.size(), start_line, {},
             "unterminated string literal");
        return;
      }
      const char escape = source_[end + 1];
      switch (escape) {
        case 'n': value.push_back('\n'); end += 2; break;
        case 'r': value.push_back('\r'); end += 2; break;
        case 't': value.push_back('\t'); end += 2; break;
        case 'b': value.push_back('\b'); end += 2; break;
        case 'f': value.push_back('\f'); end += 2; break;
        case 'v': value.push_back('\v'); end += 2; break;
        case '0': value.push_back('\0'); end += 2; break;
        case '\\': value.push_back('\\'); end += 2; break;
        case '\'': value.push_back('\''); end += 2; break;
        case '"': value.push_back('"'); end += 2; break;
        case 'x': {
          if (end + 3 >= source_.size() || !hex_digit(source_[end + 2]) ||
              !hex_digit(source_[end + 3])) {
            emit(TokenKind::error, start, end + 2, start_line, {},
                 "hex escape needs two digits");
            return;
          }
          value.push_back(static_cast<char>(hex_value(source_[end + 2]) * 16 +
                                            hex_value(source_[end + 3])));
          end += 4;
          break;
        }
        case 'u': {
          if (end + 5 >= source_.size()) {
            emit(TokenKind::error, start, source_.size(), start_line, {},
                 "unicode escape needs four digits");
            return;
          }
          std::uint32_t unit = 0;
          for (std::size_t offset = 0; offset < 4; ++offset) {
            const char digit_char = source_[end + 2 + offset];
            if (!hex_digit(digit_char)) {
              emit(TokenKind::error, start, end + 2 + offset, start_line, {},
                   "unicode escape needs four digits");
              return;
            }
            unit = unit * 16 + static_cast<std::uint32_t>(hex_value(digit_char));
          }
          if (unit < 0x80) {
            value.push_back(static_cast<char>(unit));
          } else if (unit < 0x800) {
            value.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            value.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
          } else {
            value.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            value.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
          }
          end += 6;
          break;
        }
        default:
          emit(TokenKind::error, start, end + 2, start_line, {}, "unknown escape sequence");
          return;
      }
    }
    emit(TokenKind::error, start, source_.size(), start_line, {}, "unterminated string literal");
  }

  bool regex_allowed() const {
    if (tokens_.empty()) return true;
    const auto& last = tokens_.back();
    switch (last.kind) {
      case TokenKind::identifier:
      case TokenKind::number:
      case TokenKind::string:
      case TokenKind::regex:
        return false;
      case TokenKind::keyword:
        return last.text != "this" && last.text != "super" && last.text != "true" &&
               last.text != "false" && last.text != "null";
      case TokenKind::punct:
        return last.text != ")" && last.text != "]" && last.text != "}" && last.text != "++" &&
               last.text != "--";
      default:
        return true;
    }
  }

  void scan_regex(std::size_t start, std::size_t start_line) {
    auto end = start + 1;
    bool in_class = false;
    while (end < source_.size()) {
      const char current = source_[end];
      if (current == '\n' || current == '\r') {
        emit(TokenKind::error, start, end, start_line, {},
             "unterminated regular expression literal");
        return;
      }
      if (current == '\\') {
        if (end + 1 >= source_.size() || source_[end + 1] == '\n') {
          emit(TokenKind::error, start, end + 1, start_line, {},
               "unterminated regular expression literal");
          return;
        }
        end += 2;
        continue;
      }
      if (current == '[') {
        in_class = true;
        ++end;
        continue;
      }
      if (current == ']') {
        in_class = false;
        ++end;
        continue;
      }
      if (current == '/' && !in_class) {
        const auto pattern = source_.substr(start + 1, end - start - 1);
        ++end;
        while (end < source_.size() && ident_char(source_[end])) ++end;
        emit(TokenKind::regex, start, end, start_line, std::string{pattern});
        return;
      }
      ++end;
    }
    emit(TokenKind::error, start, source_.size(), start_line, {},
         "unterminated regular expression literal");
  }

  void scan_punct(std::size_t start, std::size_t start_line) {
    for (const auto& punct : k_punctuators) {
      if (source_.substr(start, punct.size()) == punct) {
        emit(TokenKind::punct, start, start + punct.size(), start_line);
        return;
      }
    }
    emit(TokenKind::error, start, start + 1, start_line, {}, "unexpected character");
  }

  std::string_view source_;
  std::size_t index_{0};
  std::size_t line_{1};
  std::vector<Token> tokens_;
};

}  // namespace

bool is_keyword(std::string_view text) {
  return std::find(k_keywords.begin(), k_keywords.end(), text) != k_keywords.end();
}

std::string_view token_kind_name(TokenKind kind) {
  switch (kind) {
    case TokenKind::end_of_file:
      return "end_of_file";
    case TokenKind::identifier:
      return "identifier";
    case TokenKind::keyword:
      return "keyword";
    case TokenKind::number:
      return "number";
    case TokenKind::string:
      return "string";
    case TokenKind::regex:
      return "regex";
    case TokenKind::punct:
      return "punct";
    case TokenKind::error:
      return "error";
  }
  return "unknown";
}

std::vector<Token> tokenize(std::string_view source) {
  Scanner scanner{source};
  return scanner.run();
}

}  // namespace fo4::as3
