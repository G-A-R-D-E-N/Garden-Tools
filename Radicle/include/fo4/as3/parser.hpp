#pragma once

#include "fo4/as3/ast.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::as3 {

struct ParseResult {
  ExprPtr expression;
  std::string error;
  std::size_t offset{0};
  std::size_t line{1};

  explicit operator bool() const noexcept { return expression != nullptr; }
};

ParseResult parse_expression(std::string_view source);

struct ProgramResult {
  std::vector<StmtPtr> statements;
  std::string error;
  std::size_t offset{0};
  std::size_t line{1};

  explicit operator bool() const noexcept { return error.empty(); }
};

ProgramResult parse_program(std::string_view source);

}  // namespace fo4::as3
