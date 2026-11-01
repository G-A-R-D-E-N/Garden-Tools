#include "fo4/as3/parser.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using fo4::as3::Expr;
using fo4::as3::ExprKind;
using fo4::as3::ParseResult;
using fo4::as3::parse_expression;

namespace {

std::string shape(const Expr& expression) {
  switch (expression.kind) {
    case ExprKind::number_literal:
      return expression.text;
    case ExprKind::string_literal:
      return "\"" + expression.value + "\"";
    case ExprKind::boolean_literal:
      return expression.text;
    case ExprKind::null_literal:
      return "null";
    case ExprKind::identifier:
      return expression.text;
    case ExprKind::this_expression:
      return "this";
    case ExprKind::member:
      return "(member " + shape(*expression.children[0]) + " " + expression.text + ")";
    case ExprKind::index:
      return "(index " + shape(*expression.children[0]) + " " + shape(*expression.children[1]) + ")";
    case ExprKind::call: {
      std::string out{"(call " + shape(*expression.children[0])};
      for (std::size_t index = 1; index < expression.children.size(); ++index) {
        out += " " + shape(*expression.children[index]);
      }
      return out + ")";
    }
    case ExprKind::construct: {
      std::string out{"(new " + shape(*expression.children[0])};
      for (std::size_t index = 1; index < expression.children.size(); ++index) {
        out += " " + shape(*expression.children[index]);
      }
      return out + ")";
    }
    case ExprKind::unary:
      return "(" + expression.text + " " + shape(*expression.children[0]) + ")";
    case ExprKind::binary:
    case ExprKind::logical:
      return "(" + expression.text + " " + shape(*expression.children[0]) + " " +
             shape(*expression.children[1]) + ")";
    case ExprKind::conditional:
      return "(?: " + shape(*expression.children[0]) + " " + shape(*expression.children[1]) + " " +
             shape(*expression.children[2]) + ")";
    case ExprKind::assignment:
      return "(" + expression.text + " " + shape(*expression.children[0]) + " " +
             shape(*expression.children[1]) + ")";
    case ExprKind::sequence: {
      std::string out{"(seq"};
      for (const auto& child : expression.children) out += " " + shape(*child);
      return out + ")";
    }
    case ExprKind::array_literal: {
      std::string out{"(array"};
      for (const auto& child : expression.children) out += " " + shape(*child);
      return out + ")";
    }
    case ExprKind::object_literal: {
      std::string out{"(object"};
      for (const auto& member : expression.members) {
        out += " " + member.first + ":" + shape(*member.second);
      }
      return out + ")";
    }
  }
  return "?";
}

std::string parse_shape(std::string_view source) {
  const auto result = parse_expression(source);
  EXPECT_TRUE(result) << source << " :: " << result.error;
  return result ? shape(*result.expression) : "<error>";
}

bool rejects(std::string_view source) {
  const auto result = parse_expression(source);
  return !result && !result.error.empty();
}

}  // namespace

// Detects: a parser that must apply the published multiplicative-over-additive precedence.
TEST(As3Parser, ParsesArithmeticPrecedence) {
  EXPECT_EQ(parse_shape("1 + 2 * 3"), "(+ 1 (* 2 3))");
  EXPECT_EQ(parse_shape("1 * 2 + 3"), "(+ (* 1 2) 3)");
  EXPECT_EQ(parse_shape("a - b - c"), "(- (- a b) c)");
}

// Detects: a parser that must give assignment, conditional and logical operators their published order.
TEST(As3Parser, ParsesAssignmentConditionalAndLogicalOperators) {
  EXPECT_EQ(parse_shape("a = b = c"), "(= a (= b c))");
  EXPECT_EQ(parse_shape("a ? b : c"), "(?: a b c)");
  EXPECT_EQ(parse_shape("a && b || !c"), "(|| (&& a b) (! c))");
  EXPECT_EQ(parse_shape("a == b < c"), "(== a (< b c))");
}

// Detects: a parser that must chain member, index and call operations left to right.
TEST(As3Parser, ParsesMemberIndexAndCallChains) {
  EXPECT_EQ(parse_shape("a.b[0].c(d, e)"), "(call (member (index (member a b) 0) c) d e)");
  EXPECT_EQ(parse_shape("f()"), "(call f)");
  EXPECT_EQ(parse_shape("this.x"), "(member this x)");
}

// Detects: a parser that must build a construct node with its arguments instead of a call.
TEST(As3Parser, ParsesNewExpressions) {
  EXPECT_EQ(parse_shape("new Foo(1, 2)"), "(new Foo 1 2)");
  EXPECT_EQ(parse_shape("new Foo"), "(new Foo)");
  EXPECT_EQ(parse_shape("new a.b.C()"), "(new (member (member a b) C))");
}

// Detects: a parser that must decode string escapes through the tokenizer value instead of the raw text.
TEST(As3Parser, ParsesStringAndNumericLiterals) {
  EXPECT_EQ(parse_shape("\"a\\n\\x41\""), "\"a\nA\"");
  EXPECT_EQ(parse_shape("0x1F"), "0x1F");
  EXPECT_EQ(parse_shape(".5"), ".5");
  EXPECT_EQ(parse_shape("true"), "true");
  EXPECT_EQ(parse_shape("null"), "null");
}

// Detects: a parser that must parse unary, shift and relational operators.
TEST(As3Parser, ParsesUnaryShiftAndRelationalOperators) {
  EXPECT_EQ(parse_shape("-a >>> 2"), "(>>> (- a) 2)");
  EXPECT_EQ(parse_shape("typeof a"), "(typeof a)");
  EXPECT_EQ(parse_shape("delete a.b"), "(delete (member a b))");
  EXPECT_EQ(parse_shape("a in b"), "(in a b)");
  EXPECT_EQ(parse_shape("a instanceof B"), "(instanceof a B)");
}

// Detects: a parser that must parse array and object literals, including empty and trailing-comma forms.
TEST(As3Parser, ParsesArrayAndObjectLiterals) {
  EXPECT_EQ(parse_shape("[1, 2, 3]"), "(array 1 2 3)");
  EXPECT_EQ(parse_shape("[]"), "(array)");
  EXPECT_EQ(parse_shape("[1, 2,]"), "(array 1 2)");
  EXPECT_EQ(parse_shape("{}"), "(object)");
  EXPECT_EQ(parse_shape("{a: 1, \"b\": 2, 3: c}"), "(object a:1 b:2 3:c)");
}

// Detects: a parser that must honour parentheses and the comma sequence operator.
TEST(As3Parser, ParsesParenthesisedAndSequenceExpressions) {
  EXPECT_EQ(parse_shape("(1 + 2) * 3"), "(* (+ 1 2) 3)");
  EXPECT_EQ(parse_shape("a, b, c"), "(seq a b c)");
  EXPECT_EQ(parse_shape("(a, b)"), "(seq a b)");
}

// Detects: a parser that must keep the source position of a node for later diagnostics.
TEST(As3Parser, RecordsSourcePositions) {
  const auto result = parse_expression("1 +\n  value");
  ASSERT_TRUE(result);
  const auto& value = *result.expression->children[1];
  EXPECT_EQ(value.kind, ExprKind::identifier);
  EXPECT_EQ(value.line, 2u);
  EXPECT_EQ(value.offset, 6u);
}

// Detects: trailing input that must be refused instead of being ignored.
TEST(As3Parser, RejectsTrailingTokens) {
  EXPECT_TRUE(rejects("1 2"));
  EXPECT_TRUE(rejects("a b"));
}

// Detects: incomplete expressions that must produce an error with a position, not an empty tree.
TEST(As3Parser, RejectsIncompleteExpressions) {
  EXPECT_TRUE(rejects("1 +"));
  EXPECT_TRUE(rejects("("));
  EXPECT_TRUE(rejects("[1,"));
  EXPECT_TRUE(rejects("a."));
  EXPECT_TRUE(rejects("a ? b"));
  EXPECT_TRUE(rejects("{a:}"));
  EXPECT_TRUE(rejects(""));
}

// Detects: a lexer error that must surface as a parse failure instead of being skipped.
TEST(As3Parser, ReportsLexerErrors) {
  const auto result = parse_expression("a ` b");
  EXPECT_FALSE(result);
  EXPECT_NE(result.error.find("lexer"), std::string::npos);
}

// Detects: a reserved word that must not be accepted where an identifier is required.
TEST(As3Parser, RejectsReservedWordsAsExpressions) {
  EXPECT_TRUE(rejects("var"));
  EXPECT_TRUE(rejects("function"));
}

// Detects: nested parentheses past the depth limit, which must be refused instead of overflowing the stack.
TEST(As3Parser, RefusesDeeplyNestedParentheses) {
  const auto result = parse_expression(std::string(1000, '(') + "1" + std::string(1000, ')'));
  EXPECT_FALSE(result);
  EXPECT_NE(result.error.find("nesting"), std::string::npos);
}

// Detects: unary and right-associative assignment chains past the depth limit, which must be refused.
TEST(As3Parser, RefusesDeeplyNestedUnaryAndAssignmentChains) {
  EXPECT_FALSE(parse_expression(std::string(1000, '!') + "a"));
  std::string assignments;
  for (int i = 0; i < 1000; ++i) assignments += "a = ";
  EXPECT_FALSE(parse_expression(assignments + "b"));
}

// Detects: a one-million-token flat chain whose left-deep tree would recurse when it is released.
TEST(As3Parser, ParsesLongFlatChainsWithoutDeepRecursion) {
  std::string source = "a";
  for (int i = 0; i < 500000; ++i) source += "+a";
  const auto result = parse_expression(source);
  ASSERT_TRUE(result);
  EXPECT_EQ(result.expression->kind, fo4::as3::ExprKind::binary);
}
