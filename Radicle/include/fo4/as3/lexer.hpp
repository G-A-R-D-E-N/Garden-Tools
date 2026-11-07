#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::as3 {

enum class TokenKind : std::uint8_t {
  end_of_file,
  identifier,
  keyword,
  number,
  string,
  regex,
  punct,
  error,
};

struct Token {
  TokenKind kind{TokenKind::end_of_file};
  std::string text;
  std::string value;
  std::size_t offset{0};
  std::size_t line{1};
  std::string error;

  friend bool operator==(const Token&, const Token&) = default;
};

std::vector<Token> tokenize(std::string_view source);

bool is_keyword(std::string_view text);

std::string_view token_kind_name(TokenKind kind);

}  // namespace fo4::as3
