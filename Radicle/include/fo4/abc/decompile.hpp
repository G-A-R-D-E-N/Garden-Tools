#pragma once

#include <fo4/abc/abc.hpp>
#include <fo4/abc/flow.hpp>
#include <fo4/abc/opcodes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::abc {

enum class AbcDecompileStatus : std::uint8_t {
  Ok = 0,
  BodyNotFound,
  FlowRefused,
  StackShape,
  IrreducibleFlow,
  UnstructuredFlow,
  UnsupportedCondition,
  UnsupportedValueRegion,
  UnsupportedSwitch,
  UnsupportedTry,
  UnsupportedOpcode,
  UnsupportedExpression,
  UnsupportedMember,
  UnmappedName,
};

const char* decompile_status_name(AbcDecompileStatus status) noexcept;

/* Invariant: a refusal names the method, body and offset it stopped at. */
struct AbcDecompileIssue {
  AbcDecompileStatus status{AbcDecompileStatus::Ok};
  std::uint32_t method{kNoFlowIndex};
  std::uint32_t body{kNoFlowIndex};
  std::uint32_t offset{kNoFlowIndex};
  std::string message;

  bool ok() const noexcept { return status == AbcDecompileStatus::Ok; }
};

enum class AbcNodeKind : std::uint8_t {
  Block = 0,
  If,
  Loop,
  Switch,
  Try,
  Break,
  Continue,
  ShortCircuit,
  Ternary,
};

enum class AbcLoopForm : std::uint8_t {
  Infinite = 0,
  While,
  DoWhile,
};

enum class AbcCondRef : std::uint8_t {
  True = 0,
  False,
  Step,
};

/* Invariant: step 0 is the condition head and a Step reference names a later step of the same list. */
struct AbcCondStep {
  std::uint32_t block{kNoFlowIndex};
  AbcCondRef taken_kind{AbcCondRef::False};
  std::uint32_t taken_step{0};
  AbcCondRef fall_kind{AbcCondRef::True};
  std::uint32_t fall_step{0};
};

struct AbcNode;

struct AbcSwitchCase {
  std::vector<std::uint32_t> values;
  bool is_default{false};
  std::vector<AbcNode> body;
};

struct AbcCatch {
  std::uint32_t exception{0};
  std::uint32_t handler_block{kNoFlowIndex};
  std::vector<AbcNode> body;
};

/* Invariant: a Block node is a basic block minus its first `skip` instructions and minus a branch that a head node decodes. */
struct AbcNode {
  AbcNodeKind kind{AbcNodeKind::Block};
  std::uint32_t block{kNoFlowIndex};
  std::uint32_t skip{0};
  AbcLoopForm loop_form{AbcLoopForm::Infinite};
  bool is_and{false};
  std::vector<AbcCondStep> cond;
  std::vector<AbcNode> body;
  std::vector<AbcNode> other;
  std::vector<AbcSwitchCase> cases;
  std::vector<AbcCatch> catches;
};

/* Invariant: every live block appears in exactly one node of `root`, or the recovery is refused. */
struct AbcStructure {
  std::uint32_t body{0};
  std::uint32_t method{0};
  std::vector<AbcInstruction> instructions;
  AbcControlFlowGraph graph;
  std::vector<AbcExceptionRegion> exceptions;
  std::vector<std::uint32_t> entry_stack_depth;
  std::vector<AbcNode> root;
};

/* Requires body_index below file.bodies.size(); irreducible or unrecognised flow is refused, never guessed. */
AbcDecompileStatus recover_structure(const AbcFile& file, std::uint32_t body_index,
                                     AbcStructure& out, AbcDecompileIssue& issue);

/* Invariant: the text is a pure function of the tree. */
std::string dump_structure(const AbcStructure& structure);

enum class AbcExprKind : std::uint8_t {
  Literal = 0,
  Local,
  This,
  Super,
  Name,
  Member,
  Index,
  Descendants,
  Call,
  Construct,
  ApplyType,
  Unary,
  Postfix,
  Binary,
  Conditional,
  Cast,
  ArrayLiteral,
  ObjectLiteral,
  Assign,
  ScopeRef,
  GlobalScope,
  Activation,
  CatchScope,
};

const char* expr_kind_name(AbcExprKind kind) noexcept;

/* Invariant: the last four kinds are evaluator placeholders and never survive into a finished function. */
struct AbcExpr {
  AbcExprKind kind{AbcExprKind::Literal};
  std::string text;
  bool attribute{false};
  std::vector<AbcExpr> args;
};

enum class AbcStmtKind : std::uint8_t {
  Expr = 0,
  Return,
  Throw,
  If,
  While,
  DoWhile,
  Loop,
  Switch,
  Try,
  Break,
  Continue,
  With,
};

struct AbcStmt;

struct AbcStmtCase {
  std::vector<std::uint32_t> values;
  bool is_default{false};
  std::vector<AbcStmt> body;
};

struct AbcStmtCatch {
  std::string variable;
  std::string type;
  std::vector<AbcStmt> body;
};

/* Invariant: `expr` is the condition, discriminant, thrown value, returned value or with object; `has_expr` is false for a bare return. */
struct AbcStmt {
  AbcStmtKind kind{AbcStmtKind::Expr};
  bool has_expr{false};
  AbcExpr expr;
  std::vector<AbcStmt> body;
  std::vector<AbcStmt> other;
  std::vector<AbcStmtCase> cases;
  std::vector<AbcStmtCatch> catches;
};

/* Invariant: `locals` lists every non-parameter variable the body writes, in first use order, temporaries included. */
struct AbcFunction {
  std::uint32_t body{0};
  std::uint32_t method{0};
  std::vector<std::string> param_names;
  std::string rest_name;
  std::vector<std::string> locals;
  std::vector<AbcStmt> statements;
};

/* Requires body_index below file.bodies.size(); any construct that cannot be written back faithfully is refused. */
AbcDecompileStatus decompile_body(const AbcFile& file, std::uint32_t body_index, AbcFunction& out,
                                  AbcDecompileIssue& issue);

/* Invariant: the text is a pure function of the tree, one statement per line, two space indent. */
std::string dump_function(const AbcFunction& function);

/* Invariant: a refused method keeps its signature in `text` and has one entry in `refused`. */
struct AbcAs3Output {
  std::string text;
  std::uint64_t hash{0};
  std::uint32_t functions{0};
  std::vector<AbcDecompileIssue> refused;
};

/* Invariant: the text and hash are a pure function of the file; names are multiname_at strings. */
AbcAs3Output write_as3(const AbcFile& file);

/* Invariant: the same struct and the same refusal rules as write_as3, so one gate reads both languages. */
using AbcCsOutput = AbcAs3Output;

/* Requires class_index below file.instances.size() and method_index below file.methods.size(); an index past the pool gives empty text. */
AbcAs3Output write_as3_class(const AbcFile& file, std::uint32_t class_index);

AbcAs3Output write_as3_method(const AbcFile& file, std::uint32_t method_index);

/* Invariant: a flash.* or scaleform.* name without a mapping row is a marked refusal, never a guess. */
AbcCsOutput write_cs(const AbcFile& file);

AbcCsOutput write_cs_class(const AbcFile& file, std::uint32_t class_index);

AbcCsOutput write_cs_method(const AbcFile& file, std::uint32_t method_index);

/* Invariant: sorted and unique; lists the flash.* and scaleform.* names the file references that the C# table lacks. */
std::vector<std::string> cs_unmapped_names(const AbcFile& file);

/* Invariant: sorted and unique; every Flash or Scaleform name the C# writer can print, as "<kind> <name>" lines. */
std::vector<std::string> cs_emitted_names();

std::uint64_t as3_text_hash(const std::string& text);

std::string as3_hash_hex(std::uint64_t hash);

}  // namespace fo4::abc
