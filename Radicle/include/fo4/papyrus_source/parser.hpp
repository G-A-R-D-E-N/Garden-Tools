#pragma once

#include "fo4/papyrus_source/ast.hpp"
#include "fo4/papyrus_source/token.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::psc {

enum class ParseErrorKind : std::uint8_t {
  None,
  LexFailed,
  UnexpectedToken,
  ExpectedIdentifier,
  ExpectedToken,
  UnknownDeclaration,
  InvalidAssignmentTarget,
  UnbalancedIf,
  UnbalancedWhile,
  UnbalancedState,
  UnbalancedFunction,
  UnbalancedProperty,
  UnbalancedStruct,
  UnbalancedGroup,
  UnexpectedEndOfFile,
  DepthLimit,
};

struct ParseError {
  ParseErrorKind kind{ParseErrorKind::None};
  std::string message;
  std::string expected;
  std::string found;
  SourcePosition position{};

  bool ok() const { return kind == ParseErrorKind::None; }
};

struct ParseResult {
  Script script;
  ParseError error;

  bool ok() const { return error.ok(); }
};

ParseResult parse_tokens(const std::vector<Token>& tokens);
ParseResult parse(const std::string& source);

}  // namespace fo4::psc
