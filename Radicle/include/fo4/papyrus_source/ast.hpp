#pragma once

#include "fo4/papyrus_source/token.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fo4::psc {

enum class ExpressionKind : std::uint8_t {
  None,
  BoolLiteral,
  IntegerLiteral,
  FloatLiteral,
  StringLiteral,
  Identifier,
  Unary,
  Binary,
  Cast,
  Is,
  Index,
  Member,
  Call,
  New,
};

enum class UnaryOperator : std::uint8_t { Negate, Plus, Not };

enum class BinaryOperator : std::uint8_t {
  Or,
  And,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
};

struct Expression;
using ExpressionPtr = std::unique_ptr<Expression>;

struct Expression {
  ExpressionKind kind{ExpressionKind::None};
  SourcePosition position{};

  std::string text;
  std::string stringValue;
  std::int64_t intValue{0};
  double floatValue{0.0};
  bool boolValue{false};

  UnaryOperator unary{UnaryOperator::Negate};
  BinaryOperator binary{BinaryOperator::Add};
  bool arrayNew{false};

  ExpressionPtr left;
  ExpressionPtr right;
  std::vector<ExpressionPtr> arguments;
  std::vector<std::string> argumentNames;
};

enum class StatementKind : std::uint8_t {
  Expression,
  Assignment,
  VariableDeclaration,
  Return,
  If,
  While,
};

struct Statement;
using StatementPtr = std::unique_ptr<Statement>;

struct IfBranch {
  ExpressionPtr condition;
  std::vector<StatementPtr> body;
  SourcePosition position{};
};

struct Statement {
  StatementKind kind{StatementKind::Expression};
  SourcePosition position{};

  std::string typeName;
  std::string name;
  bool isArray{false};
  BinaryOperator binary{BinaryOperator::Add};
  bool compound{false};

  ExpressionPtr target;
  ExpressionPtr value;
  std::vector<IfBranch> branches;
  std::vector<StatementPtr> body;
};

struct Parameter {
  std::string name;
  std::string type;
  bool isArray{false};
  ExpressionPtr defaultValue;
  SourcePosition position{};
};

enum class FunctionKind : std::uint8_t { Function, Event };

struct FunctionDeclaration {
  FunctionKind kind{FunctionKind::Function};
  std::string name;
  std::string returnType{"None"};
  std::string doc;
  bool returnsArray{false};
  bool isNative{false};
  bool isGlobal{false};
  bool isDebugOnly{false};
  bool isBetaOnly{false};
  std::vector<Parameter> parameters;
  std::vector<StatementPtr> body;
  SourcePosition position{};
};

struct PropertyDeclaration {
  std::string name;
  std::string type;
  std::string doc;
  bool isArray{false};
  bool isAuto{false};
  bool isAutoReadOnly{false};
  bool isConst{false};
  bool isHidden{false};
  bool isConditional{false};
  bool isMandatory{false};
  bool isBetaOnly{false};
  bool isNative{false};
  ExpressionPtr defaultValue;
  std::vector<FunctionDeclaration> accessors;
  SourcePosition position{};
};

struct VariableDeclaration {
  std::string name;
  std::string type;
  std::string doc;
  bool isArray{false};
  bool isConst{false};
  bool isHidden{false};
  bool isConditional{false};
  bool isMandatory{false};
  ExpressionPtr defaultValue;
  SourcePosition position{};
};

struct StructMember {
  std::string name;
  std::string type;
  std::string doc;
  bool isArray{false};
  ExpressionPtr defaultValue;
  SourcePosition position{};
};

struct StructDeclaration {
  std::string name;
  std::string doc;
  std::vector<StructMember> members;
  SourcePosition position{};
};

struct GroupDeclaration {
  std::string name;
  std::string doc;
  std::vector<std::string> flags;
  std::vector<std::string> propertyNames;
  SourcePosition position{};
};

struct CustomEventDeclaration {
  std::string name;
  std::string doc;
  bool isNative{false};
  std::vector<Parameter> parameters;
  SourcePosition position{};
};

struct StateDeclaration {
  std::string name;
  std::string doc;
  bool isAuto{false};
  std::vector<VariableDeclaration> variables;
  std::vector<FunctionDeclaration> functions;
  std::vector<CustomEventDeclaration> customEvents;
  std::vector<StructDeclaration> structs;
  std::vector<StateDeclaration> states;
  SourcePosition position{};
};

struct Script {
  std::string name;
  std::string extends;
  std::string doc;
  std::string autoState;
  bool isNative{false};
  bool isHidden{false};
  bool isConditional{false};
  bool isBetaOnly{false};
  bool isDebugOnly{false};
  std::vector<std::string> imports;
  std::vector<StructDeclaration> structs;
  std::vector<VariableDeclaration> variables;
  std::vector<PropertyDeclaration> properties;
  std::vector<GroupDeclaration> groups;
  std::vector<CustomEventDeclaration> customEvents;
  std::vector<FunctionDeclaration> functions;
  std::vector<StateDeclaration> states;
};

}  // namespace fo4::psc
