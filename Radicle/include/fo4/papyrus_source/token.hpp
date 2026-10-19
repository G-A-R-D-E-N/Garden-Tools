#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fo4::psc {

struct SourcePosition {
  std::uint32_t line{1};
  std::uint32_t column{1};
  std::uint32_t offset{0};
};

enum class TokenKind : std::uint8_t {
  EndOfFile,
  NewLine,
  Identifier,
  Integer,
  Float,
  String,
  DocComment,
  KwScriptName,
  KwExtends,
  KwImport,
  KwProperty,
  KwEndProperty,
  KwFunction,
  KwEndFunction,
  KwEvent,
  KwEndEvent,
  KwState,
  KwEndState,
  KwGroup,
  KwEndGroup,
  KwStruct,
  KwEndStruct,
  KwCustomEvent,
  KwNative,
  KwGlobal,
  KwDebugOnly,
  KwBetaOnly,
  KwConditional,
  KwHidden,
  KwConst,
  KwAuto,
  KwAutoReadOnly,
  KwMandatory,
  KwIf,
  KwElseIf,
  KwElse,
  KwEndIf,
  KwWhile,
  KwEndWhile,
  KwReturn,
  KwNew,
  KwAs,
  KwIs,
  KwTrue,
  KwFalse,
  KwNone,
  LParen,
  RParen,
  LBracket,
  RBracket,
  Comma,
  Dot,
  Colon,
  Assign,
  PlusAssign,
  MinusAssign,
  StarAssign,
  SlashAssign,
  PercentAssign,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  AndAnd,
  OrOr,
  Not,
};

struct Token {
  TokenKind kind{TokenKind::EndOfFile};
  std::string text;
  std::string stringValue;
  std::int64_t intValue{0};
  double floatValue{0.0};
  SourcePosition position{};
};

const char* token_kind_name(TokenKind kind);

enum class LexErrorKind : std::uint8_t {
  None,
  UnterminatedString,
  UnterminatedBlockComment,
  UnterminatedDocComment,
  InvalidCharacter,
  InvalidNumber,
};

struct LexError {
  LexErrorKind kind{LexErrorKind::None};
  std::string message;
  SourcePosition position{};

  bool ok() const { return kind == LexErrorKind::None; }
};

struct LexResult {
  std::vector<Token> tokens;
  LexError error;

  bool ok() const { return error.ok(); }
};

}  // namespace fo4::psc
