#include "parser_helpers.hpp"

#include <array>
#include <string_view>

namespace fo4::as3::detail {

bool is_assignment_operator(std::string_view text) {
  static constexpr std::array<std::string_view, 12> operators{
      "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", ">>>=", "&=", "|=", "^="};
  for (const auto candidate : operators) {
    if (candidate == text) return true;
  }
  return false;
}

int binary_precedence(const Token& token) {
  const auto text = token.kind == TokenKind::punct || token.kind == TokenKind::keyword
                        ? std::string_view{token.text}
                        : std::string_view{};
  if (text == "||") return 1;
  if (text == "&&") return 2;
  if (text == "|") return 3;
  if (text == "^") return 4;
  if (text == "&") return 5;
  if (text == "==" || text == "!=" || text == "===" || text == "!==") return 6;
  if (text == "<" || text == ">" || text == "<=" || text == ">=" || text == "in" ||
      text == "instanceof") {
    return 7;
  }
  if (text == "<<" || text == ">>" || text == ">>>") return 8;
  if (text == "+" || text == "-") return 9;
  if (text == "*" || text == "/" || text == "%") return 10;
  return 0;
}

bool is_unary_operator(const Token& token) {
  if (token.kind == TokenKind::punct) {
    return token.text == "!" || token.text == "~" || token.text == "+" || token.text == "-";
  }
  if (token.kind == TokenKind::keyword) {
    return token.text == "typeof" || token.text == "void" || token.text == "delete";
  }
  return false;
}

bool is_property_name(const Token& token) {
  return token.kind == TokenKind::identifier || token.kind == TokenKind::keyword;
}

bool is_literal_key(const Token& token) {
  return token.kind == TokenKind::identifier || token.kind == TokenKind::keyword ||
         token.kind == TokenKind::number || token.kind == TokenKind::string;
}

bool is_keyword_modifier(const Token& token) {
  if (token.kind != TokenKind::keyword) return false;
  return token.text == "public" || token.text == "private" || token.text == "protected" ||
         token.text == "internal";
}

bool is_contextual_modifier(const Token& token) {
  if (token.kind != TokenKind::identifier) return false;
  return token.text == "static" || token.text == "final" || token.text == "override" ||
         token.text == "dynamic" || token.text == "native" || token.text == "virtual";
}

}  // namespace fo4::as3::detail
