#pragma once

#include "fo4/papyrus_source/ast.hpp"
#include "fo4/papyrus_source/parser.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fo4::psc {

constexpr std::size_t kMaxNestingDepth = 96;
constexpr std::size_t kMaxChainLength = 512;

class DepthGuard {
public:
  DepthGuard(std::size_t& depth, std::size_t limit) : depth_(depth), within_(depth < limit) { ++depth_; }
  ~DepthGuard() { --depth_; }

  DepthGuard(const DepthGuard&) = delete;
  DepthGuard& operator=(const DepthGuard&) = delete;
  DepthGuard(DepthGuard&&) = delete;
  DepthGuard& operator=(DepthGuard&&) = delete;

  bool within() const { return within_; }

private:
  std::size_t& depth_;
  bool within_;
};

struct DeclarationTarget {
  std::vector<std::string>* imports{nullptr};
  std::vector<StructDeclaration>* structs{nullptr};
  std::vector<VariableDeclaration>* variables{nullptr};
  std::vector<PropertyDeclaration>* properties{nullptr};
  std::vector<GroupDeclaration>* groups{nullptr};
  std::vector<CustomEventDeclaration>* customEvents{nullptr};
  std::vector<FunctionDeclaration>* functions{nullptr};
  std::vector<StateDeclaration>* states{nullptr};
  std::string* autoState{nullptr};
};

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);

  ParseResult run();

private:
  const Token& peek(std::size_t ahead = 0) const;
  const Token& advance();
  bool match(TokenKind kind);
  bool expect(TokenKind kind);
  bool expect_name(std::string& out);
  bool expect_qualified_name(std::string& out);
  bool expect_member_name(std::string& out);
  bool expect_type(std::string& out, bool& is_array);
  void skip_trivia();
  void skip_newlines();
  std::string take_doc();
  void fail(ParseErrorKind kind, const Token& token, const std::string& expected);
  bool slot_missing(const void* slot);
  bool failed() const { return !error_.ok(); }
  bool is_block_end(TokenKind kind) const;
  bool is_if_continuation(TokenKind kind) const;

  bool parse_script(Script& script);
  bool parse_script_header(Script& script);
  bool parse_declaration(DeclarationTarget& target, const std::string& doc);
  bool parse_typed_declaration(DeclarationTarget& target, const std::string& doc);
  bool parse_import(DeclarationTarget& target);
  bool parse_group(DeclarationTarget& target, const std::string& doc);
  bool parse_struct(DeclarationTarget& target, const std::string& doc);
  bool parse_custom_event(DeclarationTarget& target, const std::string& doc);
  bool parse_variable_rest(VariableDeclaration& variable, DeclarationTarget& target);
  bool parse_function_rest(FunctionDeclaration& function, std::vector<FunctionDeclaration>& destination);
  bool parse_property_rest(PropertyDeclaration& property, DeclarationTarget& target);
  bool parse_state(DeclarationTarget& target, const std::string& doc);
  bool parse_parameters(std::vector<Parameter>& out);

  bool parse_block(std::vector<StatementPtr>& out);
  bool parse_statement(std::vector<StatementPtr>& out);
  bool parse_if(std::vector<StatementPtr>& out);
  bool parse_while(std::vector<StatementPtr>& out);
  bool parse_return(std::vector<StatementPtr>& out);
  bool parse_assignment_or_declaration(std::vector<StatementPtr>& out);
  bool looks_like_declaration() const;
  bool take_assignment_operator(BinaryOperator& binary, bool& compound);
  bool end_statement();

  ExpressionPtr parse_expression();
  ExpressionPtr parse_or();
  ExpressionPtr parse_and();
  ExpressionPtr parse_comparison();
  ExpressionPtr parse_additive();
  ExpressionPtr parse_multiplicative();
  ExpressionPtr parse_unary();
  ExpressionPtr parse_cast();
  ExpressionPtr parse_postfix();
  ExpressionPtr parse_primary();
  ExpressionPtr parse_new();
  bool parse_arguments(std::vector<ExpressionPtr>& arguments, std::vector<std::string>& names);
  bool chain_within(std::size_t links);

  std::vector<Token> tokens_;
  std::size_t index_{0};
  std::size_t depth_{0};
  std::size_t links_{0};
  std::string pendingDoc_;
  ParseError error_;
};

bool is_assignable(const Expression& expression);

}  // namespace fo4::psc
