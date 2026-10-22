#include "parser_internal.hpp"

#include <fo4/papyrus_source/lexer.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fo4::psc {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
  if (tokens_.empty() || tokens_.back().kind != TokenKind::EndOfFile) {
    Token last;
    last.kind = TokenKind::EndOfFile;
    if (!tokens_.empty()) last.position = tokens_.back().position;
    tokens_.push_back(std::move(last));
  }
}

const Token& Parser::peek(std::size_t ahead) const {
  const std::size_t at = index_ + ahead;
  return at < tokens_.size() ? tokens_[at] : tokens_.back();
}

const Token& Parser::advance() {
  const Token& token = peek();
  if (index_ + 1 < tokens_.size()) ++index_;
  return token;
}

bool Parser::match(TokenKind kind) {
  if (peek().kind != kind) return false;
  advance();
  return true;
}

bool Parser::expect(TokenKind kind) {
  if (peek().kind != kind) {
    fail(ParseErrorKind::ExpectedToken, peek(), token_kind_name(kind));
    return false;
  }
  advance();
  return true;
}

bool Parser::expect_name(std::string& out) {
  if (peek().kind != TokenKind::Identifier) {
    fail(ParseErrorKind::ExpectedIdentifier, peek(), "identifier");
    return false;
  }
  out = peek().text;
  advance();
  return true;
}

bool Parser::expect_qualified_name(std::string& out) {
  if (!expect_name(out)) return false;
  while (peek().kind == TokenKind::Colon) {
    advance();
    std::string part;
    if (!expect_name(part)) return false;
    out.push_back(':');
    out += part;
  }
  return true;
}

bool Parser::expect_member_name(std::string& out) {
  if (!expect_name(out)) return false;
  while (peek().kind == TokenKind::Dot) {
    advance();
    std::string part;
    if (!expect_name(part)) return false;
    out.push_back('.');
    out += part;
  }
  return true;
}

bool Parser::expect_type(std::string& out, bool& is_array) {
  if (!expect_qualified_name(out)) return false;
  is_array = false;
  if (peek().kind == TokenKind::LBracket) {
    advance();
    if (!expect(TokenKind::RBracket)) return false;
    is_array = true;
  }
  return true;
}

void Parser::skip_trivia() {
  for (;;) {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::NewLine) {
      advance();
      continue;
    }
    if (kind == TokenKind::DocComment) {
      pendingDoc_ = peek().text;
      advance();
      continue;
    }
    return;
  }
}

void Parser::skip_newlines() {
  while (peek().kind == TokenKind::NewLine) advance();
}

std::string Parser::take_doc() {
  std::string doc = std::move(pendingDoc_);
  pendingDoc_.clear();
  return doc;
}

void Parser::fail(ParseErrorKind kind, const Token& token, const std::string& expected) {
  if (!error_.ok()) return;
  error_.kind = kind;
  error_.expected = expected;
  error_.found = token.text.empty() ? token_kind_name(token.kind) : token.text;
  error_.position = token.position;
  error_.message = "expected " + expected + " but found " + error_.found;
}

bool Parser::slot_missing(const void* slot) {
  if (slot != nullptr) return false;
  fail(ParseErrorKind::UnknownDeclaration, peek(), "a declaration allowed in this scope");
  return true;
}

bool Parser::is_block_end(TokenKind kind) const {
  switch (kind) {
    case TokenKind::KwElseIf:
    case TokenKind::KwElse:
    case TokenKind::KwEndIf:
    case TokenKind::KwEndWhile:
    case TokenKind::KwEndFunction:
    case TokenKind::KwEndEvent:
    case TokenKind::KwEndState:
    case TokenKind::KwEndProperty:
    case TokenKind::KwEndStruct:
    case TokenKind::KwEndGroup:
      return true;
    default:
      return false;
  }
}

bool Parser::is_if_continuation(TokenKind kind) const {
  return kind == TokenKind::KwElseIf || kind == TokenKind::KwElse || kind == TokenKind::KwEndIf;
}

ParseResult Parser::run() {
  ParseResult result;
  if (!parse_script(result.script)) {
    result.error = error_;
    return result;
  }
  return result;
}

bool Parser::parse_script(Script& script) {
  skip_trivia();
  if (failed()) return false;
  if (peek().kind == TokenKind::EndOfFile) {
    fail(ParseErrorKind::UnexpectedEndOfFile, peek(), "ScriptName");
    return false;
  }
  if (peek().kind != TokenKind::KwScriptName) {
    fail(ParseErrorKind::UnknownDeclaration, peek(), "ScriptName");
    return false;
  }
  if (!parse_script_header(script)) return false;
  DeclarationTarget target;
  target.imports = &script.imports;
  target.structs = &script.structs;
  target.variables = &script.variables;
  target.properties = &script.properties;
  target.groups = &script.groups;
  target.customEvents = &script.customEvents;
  target.functions = &script.functions;
  target.states = &script.states;
  target.autoState = &script.autoState;
  for (;;) {
    skip_trivia();
    if (failed()) return false;
    if (peek().kind == TokenKind::EndOfFile) return true;
    const std::string doc = take_doc();
    if (!parse_declaration(target, doc)) return false;
  }
}

bool Parser::parse_script_header(Script& script) {
  advance();
  if (!expect_qualified_name(script.name)) return false;
  if (match(TokenKind::KwExtends)) {
    if (!expect_qualified_name(script.extends)) return false;
  }
  for (;;) {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwNative) {
      script.isNative = true;
    } else if (kind == TokenKind::KwHidden) {
      script.isHidden = true;
    } else if (kind == TokenKind::KwConditional) {
      script.isConditional = true;
    } else if (kind == TokenKind::KwBetaOnly) {
      script.isBetaOnly = true;
    } else if (kind == TokenKind::KwDebugOnly) {
      script.isDebugOnly = true;
    } else if (kind == TokenKind::KwConst) {
    } else {
      break;
    }
    advance();
  }
  skip_newlines();
  if (peek().kind == TokenKind::DocComment) {
    script.doc = peek().text;
    advance();
  }
  return true;
}

bool Parser::parse_declaration(DeclarationTarget& target, const std::string& doc) {
  links_ = 0;
  const TokenKind kind = peek().kind;
  if (kind == TokenKind::KwFunction || kind == TokenKind::KwEvent) {
    if (slot_missing(target.functions)) return false;
    FunctionDeclaration function;
    function.kind = kind == TokenKind::KwFunction ? FunctionKind::Function : FunctionKind::Event;
    function.doc = doc;
    function.position = peek().position;
    advance();
    return parse_function_rest(function, *target.functions);
  }
  if (kind == TokenKind::KwProperty) {
    if (slot_missing(target.properties)) return false;
    PropertyDeclaration property;
    property.doc = doc;
    property.position = peek().position;
    advance();
    return parse_property_rest(property, target);
  }
  if (kind == TokenKind::KwImport) {
    if (slot_missing(target.imports)) return false;
    return parse_import(target);
  }
  if (kind == TokenKind::KwGroup) {
    if (slot_missing(target.groups)) return false;
    return parse_group(target, doc);
  }
  if (kind == TokenKind::KwStruct) {
    if (slot_missing(target.structs)) return false;
    return parse_struct(target, doc);
  }
  if (kind == TokenKind::KwCustomEvent) {
    if (slot_missing(target.customEvents)) return false;
    return parse_custom_event(target, doc);
  }
  if (kind == TokenKind::KwState || kind == TokenKind::KwAuto) {
    if (slot_missing(target.states)) return false;
    return parse_state(target, doc);
  }
  if (kind == TokenKind::Identifier) return parse_typed_declaration(target, doc);
  fail(ParseErrorKind::UnknownDeclaration, peek(), "declaration");
  return false;
}

bool Parser::parse_typed_declaration(DeclarationTarget& target, const std::string& doc) {
  const SourcePosition position = peek().position;
  std::string type;
  bool isArray = false;
  if (!expect_type(type, isArray)) return false;
  const TokenKind kind = peek().kind;
  if (kind == TokenKind::KwFunction || kind == TokenKind::KwEvent) {
    if (slot_missing(target.functions)) return false;
    FunctionDeclaration function;
    function.kind = kind == TokenKind::KwFunction ? FunctionKind::Function : FunctionKind::Event;
    function.returnType = std::move(type);
    function.returnsArray = isArray;
    function.doc = doc;
    function.position = position;
    advance();
    return parse_function_rest(function, *target.functions);
  }
  if (kind == TokenKind::KwProperty) {
    if (slot_missing(target.properties)) return false;
    PropertyDeclaration property;
    property.type = std::move(type);
    property.isArray = isArray;
    property.doc = doc;
    property.position = position;
    advance();
    return parse_property_rest(property, target);
  }
  if (slot_missing(target.variables)) return false;
  VariableDeclaration variable;
  variable.type = std::move(type);
  variable.isArray = isArray;
  variable.doc = doc;
  variable.position = position;
  return parse_variable_rest(variable, target);
}

bool Parser::parse_import(DeclarationTarget& target) {
  advance();
  std::string name;
  if (!expect_qualified_name(name)) return false;
  if (!end_statement()) return false;
  target.imports->push_back(std::move(name));
  return true;
}

bool Parser::parse_group(DeclarationTarget& target, const std::string& doc) {
  GroupDeclaration group;
  group.doc = doc;
  group.position = peek().position;
  advance();
  if (!expect_name(group.name)) return false;
  while (peek().kind == TokenKind::Identifier) {
    group.flags.push_back(peek().text);
    advance();
  }
  if (!end_statement()) return false;
  DeclarationTarget inner;
  inner.variables = target.variables;
  inner.properties = target.properties;
  for (;;) {
    skip_trivia();
    if (failed()) return false;
    if (peek().kind == TokenKind::KwEndGroup) break;
    if (peek().kind == TokenKind::EndOfFile) {
      fail(ParseErrorKind::UnbalancedGroup, peek(), "EndGroup");
      return false;
    }
    const std::size_t before = target.properties != nullptr ? target.properties->size() : 0;
    const std::string innerDoc = take_doc();
    if (!parse_declaration(inner, innerDoc)) return false;
    if (target.properties == nullptr) continue;
    for (std::size_t at = before; at < target.properties->size(); ++at) {
      group.propertyNames.push_back((*target.properties)[at].name);
    }
  }
  advance();
  if (!end_statement()) return false;
  target.groups->push_back(std::move(group));
  return true;
}

bool Parser::parse_struct(DeclarationTarget& target, const std::string& doc) {
  StructDeclaration structure;
  structure.doc = doc;
  structure.position = peek().position;
  advance();
  if (!expect_name(structure.name)) return false;
  if (!end_statement()) return false;
  for (;;) {
    skip_trivia();
    if (failed()) return false;
    if (peek().kind == TokenKind::KwEndStruct) break;
    if (peek().kind == TokenKind::EndOfFile) {
      fail(ParseErrorKind::UnbalancedStruct, peek(), "EndStruct");
      return false;
    }
    StructMember member;
    member.doc = take_doc();
    member.position = peek().position;
    if (!expect_type(member.type, member.isArray)) return false;
    if (!expect_name(member.name)) return false;
    if (match(TokenKind::Assign)) {
      member.defaultValue = parse_expression();
      if (failed()) return false;
    }
    if (!end_statement()) return false;
    structure.members.push_back(std::move(member));
  }
  advance();
  if (!end_statement()) return false;
  target.structs->push_back(std::move(structure));
  return true;
}

bool Parser::parse_custom_event(DeclarationTarget& target, const std::string& doc) {
  CustomEventDeclaration event;
  event.doc = doc;
  event.position = peek().position;
  advance();
  if (!expect_member_name(event.name)) return false;
  if (peek().kind == TokenKind::LParen) {
    if (!parse_parameters(event.parameters)) return false;
  }
  if (match(TokenKind::KwNative)) event.isNative = true;
  if (!end_statement()) return false;
  target.customEvents->push_back(std::move(event));
  return true;
}

bool Parser::parse_variable_rest(VariableDeclaration& variable, DeclarationTarget& target) {
  if (!expect_name(variable.name)) return false;
  for (;;) {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwConst) {
      variable.isConst = true;
    } else if (kind == TokenKind::KwHidden) {
      variable.isHidden = true;
    } else if (kind == TokenKind::KwConditional) {
      variable.isConditional = true;
    } else if (kind == TokenKind::KwMandatory) {
      variable.isMandatory = true;
    } else {
      break;
    }
    advance();
  }
  if (match(TokenKind::Assign)) {
    variable.defaultValue = parse_expression();
    if (failed()) return false;
  }
  if (!end_statement()) return false;
  target.variables->push_back(std::move(variable));
  return true;
}

ParseResult parse_tokens(const std::vector<Token>& tokens) {
  Parser parser(tokens);
  return parser.run();
}

ParseResult parse(const std::string& source) {
  const LexResult lexed = lex(source);
  if (!lexed.ok()) {
    ParseResult result;
    result.error.kind = ParseErrorKind::LexFailed;
    result.error.message = lexed.error.message;
    result.error.position = lexed.error.position;
    return result;
  }
  return parse_tokens(lexed.tokens);
}

}  // namespace fo4::psc
