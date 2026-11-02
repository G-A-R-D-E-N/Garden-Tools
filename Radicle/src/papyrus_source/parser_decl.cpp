#include "parser_internal.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fo4::psc {

bool Parser::parse_parameters(std::vector<Parameter>& out) {
  if (!expect(TokenKind::LParen)) return false;
  if (match(TokenKind::RParen)) return true;
  for (;;) {
    Parameter parameter;
    parameter.position = peek().position;
    if (!expect_type(parameter.type, parameter.isArray)) return false;
    if (!expect_name(parameter.name)) return false;
    if (match(TokenKind::Assign)) {
      parameter.defaultValue = parse_expression();
      if (failed()) return false;
    }
    out.push_back(std::move(parameter));
    if (match(TokenKind::Comma)) continue;
    break;
  }
  return expect(TokenKind::RParen);
}

bool Parser::parse_function_rest(FunctionDeclaration& function, std::vector<FunctionDeclaration>& destination) {
  if (!expect_member_name(function.name)) return false;
  if (peek().kind == TokenKind::LParen) {
    if (!parse_parameters(function.parameters)) return false;
  }
  for (;;) {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwNative) {
      function.isNative = true;
    } else if (kind == TokenKind::KwGlobal) {
      function.isGlobal = true;
    } else if (kind == TokenKind::KwDebugOnly) {
      function.isDebugOnly = true;
    } else if (kind == TokenKind::KwBetaOnly) {
      function.isBetaOnly = true;
    } else {
      break;
    }
    advance();
  }
  if (!end_statement()) return false;
  if (!function.isNative) {
    if (!parse_block(function.body)) return false;
    if (peek().kind != TokenKind::KwEndFunction && peek().kind != TokenKind::KwEndEvent) {
      fail(ParseErrorKind::UnbalancedFunction, peek(), "EndFunction");
      return false;
    }
    advance();
    if (!end_statement()) return false;
  }
  destination.push_back(std::move(function));
  return true;
}

bool Parser::parse_property_rest(PropertyDeclaration& property, DeclarationTarget& target) {
  if (!expect_name(property.name)) return false;
  if (match(TokenKind::Assign)) {
    property.defaultValue = parse_expression();
    if (failed()) return false;
  }
  for (;;) {
    const TokenKind kind = peek().kind;
    if (kind == TokenKind::KwAuto) {
      property.isAuto = true;
    } else if (kind == TokenKind::KwAutoReadOnly) {
      property.isAutoReadOnly = true;
    } else if (kind == TokenKind::KwConst) {
      property.isConst = true;
    } else if (kind == TokenKind::KwHidden) {
      property.isHidden = true;
    } else if (kind == TokenKind::KwConditional) {
      property.isConditional = true;
    } else if (kind == TokenKind::KwMandatory) {
      property.isMandatory = true;
    } else if (kind == TokenKind::KwBetaOnly) {
      property.isBetaOnly = true;
    } else if (kind == TokenKind::KwNative) {
      property.isNative = true;
    } else {
      break;
    }
    advance();
  }
  if (match(TokenKind::Assign)) {
    property.defaultValue = parse_expression();
    if (failed()) return false;
  }
  if (!end_statement()) return false;
  if (!property.isAuto && !property.isAutoReadOnly && !property.isNative) {
    for (;;) {
      skip_trivia();
      if (failed()) return false;
      if (peek().kind == TokenKind::KwEndProperty) break;
      if (peek().kind == TokenKind::EndOfFile) {
        fail(ParseErrorKind::UnbalancedProperty, peek(), "EndProperty");
        return false;
      }
      FunctionDeclaration accessor;
      accessor.doc = take_doc();
      accessor.position = peek().position;
      if (peek().kind == TokenKind::KwEvent) {
        accessor.kind = FunctionKind::Event;
      } else if (peek().kind == TokenKind::Identifier) {
        bool isArray = false;
        if (!expect_type(accessor.returnType, isArray)) return false;
        accessor.returnsArray = isArray;
      }
      if (peek().kind != TokenKind::KwFunction && peek().kind != TokenKind::KwEvent) {
        fail(ParseErrorKind::UnknownDeclaration, peek(), "Function");
        return false;
      }
      advance();
      if (!parse_function_rest(accessor, property.accessors)) return false;
    }
    advance();
    if (!end_statement()) return false;
  }
  target.properties->push_back(std::move(property));
  return true;
}

bool Parser::parse_state(DeclarationTarget& target, const std::string& doc) {
  StateDeclaration state;
  state.doc = doc;
  state.position = peek().position;
  if (match(TokenKind::KwAuto)) state.isAuto = true;
  if (!expect(TokenKind::KwState)) return false;
  if (!expect_name(state.name)) return false;
  if (!end_statement()) return false;
  DeclarationTarget inner;
  inner.variables = &state.variables;
  inner.functions = &state.functions;
  inner.customEvents = &state.customEvents;
  inner.structs = &state.structs;
  inner.states = &state.states;
  inner.autoState = target.autoState;
  for (;;) {
    skip_trivia();
    if (failed()) return false;
    if (peek().kind == TokenKind::KwEndState) break;
    if (peek().kind == TokenKind::EndOfFile) {
      fail(ParseErrorKind::UnbalancedState, peek(), "EndState");
      return false;
    }
    const std::string innerDoc = take_doc();
    if (!parse_declaration(inner, innerDoc)) return false;
  }
  advance();
  if (!end_statement()) return false;
  if (state.isAuto && target.autoState != nullptr && target.autoState->empty()) {
    *target.autoState = state.name;
  }
  target.states->push_back(std::move(state));
  return true;
}

}  // namespace fo4::psc
