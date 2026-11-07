#include "fo4/as3/parser.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using fo4::as3::Expr;
using fo4::as3::ExprKind;
using fo4::as3::parse_program;
using fo4::as3::ProgramResult;
using fo4::as3::Stmt;
using fo4::as3::StmtKind;

namespace {

std::string expr_shape(const Expr& expression) {
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
      return "(member " + expr_shape(*expression.children[0]) + " " + expression.text + ")";
    case ExprKind::index:
      return "(index " + expr_shape(*expression.children[0]) + " " +
             expr_shape(*expression.children[1]) + ")";
    case ExprKind::call: {
      std::string out{"(call " + expr_shape(*expression.children[0])};
      for (std::size_t index = 1; index < expression.children.size(); ++index) {
        out += " " + expr_shape(*expression.children[index]);
      }
      return out + ")";
    }
    case ExprKind::construct: {
      std::string out{"(new " + expr_shape(*expression.children[0])};
      for (std::size_t index = 1; index < expression.children.size(); ++index) {
        out += " " + expr_shape(*expression.children[index]);
      }
      return out + ")";
    }
    case ExprKind::unary:
      return "(" + expression.text + " " + expr_shape(*expression.children[0]) + ")";
    case ExprKind::binary:
    case ExprKind::logical:
      return "(" + expression.text + " " + expr_shape(*expression.children[0]) + " " +
             expr_shape(*expression.children[1]) + ")";
    case ExprKind::conditional:
      return "(?: " + expr_shape(*expression.children[0]) + " " +
             expr_shape(*expression.children[1]) + " " + expr_shape(*expression.children[2]) + ")";
    case ExprKind::assignment:
      return "(" + expression.text + " " + expr_shape(*expression.children[0]) + " " +
             expr_shape(*expression.children[1]) + ")";
    case ExprKind::sequence: {
      std::string out{"(seq"};
      for (const auto& child : expression.children) out += " " + expr_shape(*child);
      return out + ")";
    }
    case ExprKind::array_literal: {
      std::string out{"(array"};
      for (const auto& child : expression.children) out += " " + expr_shape(*child);
      return out + ")";
    }
    case ExprKind::object_literal: {
      std::string out{"(object"};
      for (const auto& member : expression.members) {
        out += " " + member.first + ":" + expr_shape(*member.second);
      }
      return out + ")";
    }
  }
  return "?";
}

std::string stmt_shape(const Stmt& statement) {
  switch (statement.kind) {
    case StmtKind::empty:
      return "empty";
    case StmtKind::expression:
      return statement.expression ? expr_shape(*statement.expression) : "expr?";
    case StmtKind::block: {
      std::string out{"(block"};
      for (const auto& child : statement.body) out += " " + stmt_shape(*child);
      return out + ")";
    }
    case StmtKind::var_declaration: {
      std::string out{"(var"};
      for (const auto& modifier : statement.modifiers) out += " " + modifier;
      for (const auto& binding : statement.bindings) {
        out += " " + binding.name;
        if (!binding.type.empty()) out += ":" + binding.type;
        if (binding.initializer) out += "=" + expr_shape(*binding.initializer);
      }
      return out + ")";
    }
    case StmtKind::import_directive:
      return "(import " + statement.text + ")";
    case StmtKind::package_declaration: {
      std::string out{"(package"};
      if (!statement.name.empty()) out += " " + statement.name;
      for (const auto& child : statement.body) out += " " + stmt_shape(*child);
      return out + ")";
    }
    case StmtKind::class_declaration: {
      std::string out{"(class"};
      for (const auto& modifier : statement.modifiers) out += " " + modifier;
      out += " " + statement.name;
      if (!statement.base.empty()) out += " extends " + statement.base;
      if (!statement.interfaces.empty()) {
        out += " implements ";
        for (std::size_t index = 0; index < statement.interfaces.size(); ++index) {
          if (index > 0) out += ",";
          out += statement.interfaces[index];
        }
      }
      for (const auto& child : statement.body) out += " " + stmt_shape(*child);
      return out + ")";
    }
    case StmtKind::if_statement: {
      std::string out{"(if " + expr_shape(*statement.expression) + " " +
                      stmt_shape(*statement.body.front())};
      if (!statement.alternate.empty()) out += " " + stmt_shape(*statement.alternate.front());
      return out + ")";
    }
    case StmtKind::while_statement:
      return "(while " + expr_shape(*statement.expression) + " " +
             stmt_shape(*statement.body.front()) + ")";
    case StmtKind::do_while_statement:
      return "(do " + stmt_shape(*statement.body.front()) + " " +
             expr_shape(*statement.expression) + ")";
    case StmtKind::for_statement: {
      std::string out{"(for"};
      out += statement.initializer ? " " + stmt_shape(*statement.initializer) : " none";
      out += statement.expression ? " " + expr_shape(*statement.expression) : " none";
      out += statement.second_expression ? " " + expr_shape(*statement.second_expression) : " none";
      out += " " + stmt_shape(*statement.body.front()) + ")";
      return out;
    }
    case StmtKind::for_in_statement: {
      std::string out{"(for-in"};
      if (statement.each) out += " each";
      out += statement.initializer ? " " + stmt_shape(*statement.initializer) : " none";
      out += statement.expression ? " " + expr_shape(*statement.expression) : " none";
      out += " " + stmt_shape(*statement.body.front()) + ")";
      return out;
    }
    case StmtKind::function_declaration: {
      std::string out{"(function"};
      for (const auto& modifier : statement.modifiers) out += " " + modifier;
      out += " " + statement.name + "(";
      for (std::size_t index = 0; index < statement.parameters.size(); ++index) {
        if (index > 0) out += ",";
        const auto& parameter = statement.parameters[index];
        if (parameter.rest) out += "...";
        out += parameter.name;
        if (!parameter.type.empty()) out += ":" + parameter.type;
        if (parameter.default_value) out += "=" + expr_shape(*parameter.default_value);
      }
      out += ")";
      if (!statement.return_type.empty()) out += ":" + statement.return_type;
      out += " " + stmt_shape(*statement.body.front()) + ")";
      return out;
    }
    case StmtKind::switch_statement: {
      std::string out{"(switch " + expr_shape(*statement.expression)};
      for (const auto& entry : statement.cases) {
        if (entry.is_default) {
          out += " (default";
        } else {
          out += " (case " + expr_shape(*entry.label);
        }
        for (const auto& child : entry.statements) out += " " + stmt_shape(*child);
        out += ")";
      }
      return out + ")";
    }
    case StmtKind::throw_statement:
      return "(throw " + expr_shape(*statement.expression) + ")";
    case StmtKind::return_statement:
      return statement.expression ? "(return " + expr_shape(*statement.expression) + ")"
                                  : "(return)";
    case StmtKind::break_statement:
      return "break";
    case StmtKind::continue_statement:
      return "continue";
  }
  return "?";
}

std::string program_shape(std::string_view source) {
  const auto result = parse_program(source);
  EXPECT_TRUE(result) << source << " :: " << result.error;
  if (!result) return "<error>";
  std::string out{"(program"};
  for (const auto& statement : result.statements) out += " " + stmt_shape(*statement);
  return out + ")";
}

bool rejects(std::string_view source) {
  const auto result = parse_program(source);
  return !result && !result.error.empty();
}

}  // namespace

// Detects: a statement parser that must read typed and untyped variable declarations.
TEST(As3Statements, ParsesVariableDeclarations) {
  EXPECT_EQ(program_shape("var a = 1;"), "(program (var a=1))");
  EXPECT_EQ(program_shape("var b:int;"), "(program (var b:int))");
  EXPECT_EQ(program_shape("var c:Array = [1, 2];"), "(program (var c:Array=(array 1 2)))");
  EXPECT_EQ(program_shape("var d:int[];"), "(program (var d:int[]))");
  EXPECT_EQ(program_shape("var e = 1, f:String = \"x\";"),
            "(program (var e=1 f:String=\"x\"))");
}

// Detects: empty and expression statements that must keep their terminating semicolon requirement.
TEST(As3Statements, ParsesEmptyAndExpressionStatements) {
  EXPECT_EQ(program_shape(";"), "(program empty)");
  EXPECT_EQ(program_shape("a = 1;"), "(program (= a 1))");
  EXPECT_EQ(program_shape("f();"), "(program (call f))");
}

// Detects: a block that must keep its statements in order.
TEST(As3Statements, ParsesBlocks) {
  EXPECT_EQ(program_shape("{ var a; a = 1; }"), "(program (block (var a) (= a 1)))");
  EXPECT_EQ(program_shape("{ }"), "(program (block))");
}

// Detects: if/else chains that must nest an else-if as the alternate branch.
TEST(As3Statements, ParsesIfElseChains) {
  EXPECT_EQ(program_shape("if (a) b = 1;"), "(program (if a (= b 1)))");
  EXPECT_EQ(program_shape("if (a) b = 1; else b = 2;"),
            "(program (if a (= b 1) (= b 2)))");
  EXPECT_EQ(program_shape("if (a) b = 1; else if (c) b = 2; else b = 3;"),
            "(program (if a (= b 1) (if c (= b 2) (= b 3))))");
}

// Detects: while and do-while loops that must keep the condition on the right side.
TEST(As3Statements, ParsesWhileAndDoWhileLoops) {
  EXPECT_EQ(program_shape("while (a < 3) a = a + 1;"),
            "(program (while (< a 3) (= a (+ a 1))))");
  EXPECT_EQ(program_shape("do a = a - 1; while (a > 0);"),
            "(program (do (= a (- a 1)) (> a 0)))");
}

// Detects: for loops that must keep the initialiser, condition, update and body apart.
TEST(As3Statements, ParsesForLoops) {
  EXPECT_EQ(program_shape("for (var i = 0; i < 3; i = i + 1) { }"),
            "(program (for (var i=0) (< i 3) (= i (+ i 1)) (block)))");
  EXPECT_EQ(program_shape("for (;;) ;"), "(program (for none none none empty))");
  EXPECT_EQ(program_shape("for (a = 0; a < 2; a = a + 1) b = a;"),
            "(program (for (= a 0) (< a 2) (= a (+ a 1)) (= b a)))");
}

// Detects: return, break and continue that must carry an optional value and require a semicolon.
TEST(As3Statements, ParsesReturnBreakAndContinue) {
  EXPECT_EQ(program_shape("return;"), "(program (return))");
  EXPECT_EQ(program_shape("return a + 1;"), "(program (return (+ a 1)))");
  EXPECT_EQ(program_shape("break; continue;"), "(program break continue)");
}

// Detects: statement positions that must be recorded for later diagnostics.
TEST(As3Statements, RecordsStatementPositions) {
  const auto result = parse_program("var a = 1;\n a = 2;");
  ASSERT_TRUE(result);
  ASSERT_EQ(result.statements.size(), 2u);
  EXPECT_EQ(result.statements[0]->line, 1u);
  EXPECT_EQ(result.statements[1]->line, 2u);
  EXPECT_EQ(result.statements[1]->offset, 12u);
}

// Detects: a whole program that must hold every statement in source order.
TEST(As3Statements, ParsesAWholeProgramInOrder) {
  const auto result = parse_program("var a = 1; a = 2; if (a) { a = 3; }");
  ASSERT_TRUE(result);
  ASSERT_EQ(result.statements.size(), 3u);
  EXPECT_EQ(result.statements[0]->kind, StmtKind::var_declaration);
  EXPECT_EQ(result.statements[1]->kind, StmtKind::expression);
  EXPECT_EQ(result.statements[2]->kind, StmtKind::if_statement);
}

// Detects: statements this slice does not implement, which must be refused by name instead of misparsed.
TEST(As3Statements, RejectsUnsupportedStatements) {
  EXPECT_TRUE(rejects("interface I { }"));
  EXPECT_TRUE(rejects("try { } finally { }"));
}

// Detects: a throw that must keep its thrown expression and require a terminating semicolon.
TEST(As3Statements, ParsesThrowStatements) {
  EXPECT_EQ(program_shape("throw new Error(\"x\");"),
            "(program (throw (new Error \"x\")))");
  EXPECT_EQ(program_shape("throw a ? b : c;"), "(program (throw (?: a b c)))");
}

// Detects: a throw without an expression or semicolon, which must be refused.
TEST(As3Statements, RejectsMalformedThrows) {
  EXPECT_TRUE(rejects("throw;"));
  EXPECT_TRUE(rejects("throw a"));
}

// Detects: imports and packages that must keep their path text and their declarations in order.
TEST(As3Statements, ParsesImportsAndPackages) {
  EXPECT_EQ(program_shape("import flash.display.*;"),
            "(program (import flash.display.*))");
  EXPECT_EQ(program_shape("package foo.bar { import a.B; function f() { } }"),
            "(program (package foo.bar (import a.B) (function f() (block))))");
  EXPECT_EQ(program_shape("package { }"), "(program (package))");
}

// Detects: class declarations that must keep modifiers, base, interfaces and members apart.
TEST(As3Statements, ParsesClassDeclarations) {
  EXPECT_EQ(
      program_shape(
          "public class A extends B implements C, D { public var x:int = 0; "
          "public static function f(a:int):void { } }"),
      "(program (class public A extends B implements C,D (var public x:int=0) "
      "(function public static f(a:int):void (block))))");
  EXPECT_EQ(program_shape("class A { ; }"), "(program (class A empty))");
}

// Detects: malformed classes that must be refused, including a nested class and a non-declaration member.
TEST(As3Statements, RejectsMalformedClasses) {
  EXPECT_TRUE(rejects("class { }"));
  EXPECT_TRUE(rejects("class A extends { }"));
  EXPECT_TRUE(rejects("class A { a = 1; }"));
  EXPECT_TRUE(rejects("class A { public }"));
  EXPECT_TRUE(rejects("class A { class B { } }"));
}

// Detects: malformed packages and import paths that must be refused instead of half-read.
TEST(As3Statements, RejectsMalformedPackagesAndImports) {
  EXPECT_TRUE(rejects("package foo bar { }"));
  EXPECT_TRUE(rejects("package foo {"));
  EXPECT_TRUE(rejects("import ;"));
  EXPECT_TRUE(rejects("import a.;"));
}

// Detects: incomplete statements that must produce an error with the parser's position.
TEST(As3Statements, RejectsIncompleteStatements) {
  EXPECT_TRUE(rejects("var = 1;"));
  EXPECT_TRUE(rejects("if (a"));
  EXPECT_TRUE(rejects("while a) ;"));
  EXPECT_TRUE(rejects("for (var i = 0 i < 3; i = i + 1) ;"));
  EXPECT_TRUE(rejects("do while (a);"));
  EXPECT_TRUE(rejects("return"));
  EXPECT_TRUE(rejects("var a:Foo.Bar;"));
}

// Detects: function declarations that must keep their parameters, defaults, rest flag, return type and body.
TEST(As3Statements, ParsesFunctionDeclarations) {
  EXPECT_EQ(program_shape("function f() { }"), "(program (function f() (block)))");
  EXPECT_EQ(program_shape("function g(a:int, b:String = \"x\"):void { return; }"),
            "(program (function g(a:int,b:String=\"x\"):void (block (return))))");
  EXPECT_EQ(program_shape("function h(...args:Array):int { return 0; }"),
            "(program (function h(...args:Array):int (block (return 0))))");
  EXPECT_EQ(program_shape("function u(a, b) { a = b; }"),
            "(program (function u(a,b) (block (= a b))))");
}

// Detects: malformed functions that must be refused instead of parsed partially.
TEST(As3Statements, RejectsMalformedFunctions) {
  EXPECT_TRUE(rejects("function () { }"));
  EXPECT_TRUE(rejects("function f("));
  EXPECT_TRUE(rejects("function f() a = 1;"));
  EXPECT_TRUE(rejects("function f():int"));
  EXPECT_TRUE(rejects("function f(...rest, b:int) { }"));
}

// Detects: for-in and for-each loops that must keep the binding, the collection and the body apart.
TEST(As3Statements, ParsesForInAndForEach) {
  EXPECT_EQ(program_shape("for (var k in obj) { }"), "(program (for-in (var k) obj (block)))");
  EXPECT_EQ(program_shape("for each (var item:Object in list) x = item;"),
            "(program (for-in each (var item:Object) list (= x item)))");
  EXPECT_EQ(program_shape("for (k in obj) ;"), "(program (for-in k obj empty))");
}

// Detects: invalid for-in headers that must be refused, including an initialised or repeated binding.
TEST(As3Statements, RejectsInvalidForInHeaders) {
  EXPECT_TRUE(rejects("for each (a = 0; a < 2) ;"));
  EXPECT_TRUE(rejects("for (var x = 1 in y) ;"));
  EXPECT_TRUE(rejects("for (var a, b in y) ;"));
}

// Detects: incompleteness of a for-in loop that must be an error rather than an empty collection.
TEST(As3Statements, RejectsIncompleteForIn) {
  EXPECT_TRUE(rejects("for (var k in) ;"));
  EXPECT_TRUE(rejects("for (k in obj"));
}

// Detects: a switch that must keep each case label with its own statement list, fallthrough included.
TEST(As3Statements, ParsesSwitchStatements) {
  EXPECT_EQ(program_shape("switch (a) { case 1: b = 1; break; default: b = 2; }"),
            "(program (switch a (case 1 (= b 1) break) (default (= b 2))))");
  EXPECT_EQ(program_shape("switch (a) { case 1: case 2: b = 1; }"),
            "(program (switch a (case 1) (case 2 (= b 1))))");
  EXPECT_EQ(program_shape("switch (a) { }"), "(program (switch a))");
}

// Detects: a malformed switch body that must be refused instead of read as a case with no statements.
TEST(As3Statements, RejectsMalformedSwitches) {
  EXPECT_TRUE(rejects("switch a) { }"));
  EXPECT_TRUE(rejects("switch (a) { default }"));
  EXPECT_TRUE(rejects("switch (a) { case 1 }"));
  EXPECT_TRUE(rejects("switch (a) { b = 1; }"));
  EXPECT_TRUE(rejects("switch (a) { default: ; default: ; }"));
  EXPECT_TRUE(rejects("switch (a) { case 1: b = 1;"));
}

// Detects: an empty source that must parse as an empty program rather than an error.
TEST(As3Statements, AcceptsAnEmptyProgram) {
  const auto result = parse_program("");
  EXPECT_TRUE(result);
  EXPECT_TRUE(result.statements.empty());
}

// Detects: nested blocks past the depth limit, which must be refused instead of overflowing the stack.
TEST(As3Statements, RefusesDeeplyNestedBlocks) {
  const auto result = parse_program(std::string(1000, '{') + std::string(1000, '}'));
  EXPECT_FALSE(result);
  EXPECT_NE(result.error.find("nesting"), std::string::npos);
}
