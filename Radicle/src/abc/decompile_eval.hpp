#pragma once

#include "decompile_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::abc::detail {

inline constexpr int kPrimaryPrec = 17;
inline constexpr int kMemberPrec = 16;
inline constexpr int kPostfixPrec = 15;
inline constexpr int kUnaryPrec = 14;
inline constexpr int kConditionalPrec = 3;
inline constexpr int kAssignPrec = 2;

int binary_precedence(const std::string& op);
int precedence(const AbcExpr& expr);

std::string quote_string(const std::string& text);
std::string format_double(double value);
std::string render_expr(const AbcExpr& expr);
void render_statements(std::string& out, const std::vector<AbcStmt>& list, std::size_t depth);

AbcExpr make_expr(AbcExprKind kind, std::string text = {}, std::vector<AbcExpr> args = {});
AbcExpr negate_condition(AbcExpr expr);
const char* binary_operator(std::string_view mnemonic);
const char* conversion_type(std::string_view mnemonic);
bool is_simple_expr(const AbcExpr& expr);
bool is_placeholder(AbcExprKind kind);
bool has_placeholder(const AbcExpr& expr);
bool mentions_local(const AbcExpr& expr, const std::string& name);
std::uint32_t local_index_of(const AbcInstruction& instruction);
std::uint32_t catch_prologue_length(const AbcStructure& structure, std::uint32_t block,
                                    std::uint32_t exception);

struct NameRef {
  std::string text;
  bool attribute{false};
  bool has_ns{false};
  bool has_name{false};
  AbcExpr ns;
  AbcExpr name;
};

struct ScopeEntry {
  AbcExpr expr;
  int with_record{-1};
};

struct WithRecord {
  std::vector<AbcStmt>* list{nullptr};
  std::size_t index{0};
};

class Evaluator {
public:
  Evaluator(const AbcFile& file, const AbcStructure& structure, const AbcMethodInfo& method,
            const AbcMethodBody& body, AbcFunction& function, AbcDecompileIssue& issue);

  bool run();

private:
  const AbcFile& file_;
  const AbcStructure& s_;
  const AbcMethodInfo& method_;
  const AbcMethodBody& body_;
  AbcFunction& fn_;
  AbcDecompileIssue& issue_;

  std::vector<AbcExpr> stack_;
  std::size_t floor_{0};
  std::vector<ScopeEntry> scope_;
  std::vector<AbcStmt>* out_{nullptr};
  std::vector<WithRecord> withs_;
  std::map<std::uint32_t, AbcExpr> bound_;
  std::uint32_t value_depth_{0};
  bool in_member_{false};
  std::uint32_t temp_count_{0};
  std::uint32_t offset_{kNone};
  std::uint32_t catch_block_{kNone};
  std::uint32_t catch_skip_{0};

  bool bad() const;
  void fail(AbcDecompileStatus status, const std::string& message);
  const AbcBasicBlock& blk(std::uint32_t b) const { return s_.graph.blocks[b]; }

  AbcExpr pop();
  void push(AbcExpr expr);
  std::vector<AbcExpr> pop_args(std::uint32_t count);
  void emit(AbcStmt stmt);
  void emit_expr(AbcExpr expr);
  void emit_assign(AbcExpr target, AbcExpr value);
  void spill(AbcExpr& entry);
  void flush_stack(const std::string* written_local = nullptr);

  void name_params();
  std::string local_name(std::uint32_t index, bool& extra);
  AbcExpr local_expr(std::uint32_t index);
  AbcExpr local_target(std::uint32_t index);
  void declare_local(const std::string& name);
  void assign_local(std::uint32_t index, AbcExpr value);
  bool slot_name(std::uint32_t slot, std::string& name);
  std::string type_text(std::uint32_t index) const;
  std::uint32_t operand(const AbcInstruction& in, std::size_t at) const;

  bool pop_name(std::uint32_t index, NameRef& ref);
  AbcExpr member_expr(AbcExpr receiver, const NameRef& ref);
  AbcExpr numeric_step(AbcExpr value, const char* op);

  bool step(const AbcInstruction& in, const AbcInstruction* next, bool& consumed);
  bool step_stack(const AbcInstruction& in, std::string_view m, const AbcInstruction* next,
                  bool& consumed);
  bool step_local(const AbcInstruction& in, std::string_view m);
  bool step_scope(const AbcInstruction& in, std::string_view m);
  bool step_property(const AbcInstruction& in, std::string_view m);
  bool step_call(const AbcInstruction& in, std::string_view m);
  bool step_value(const AbcInstruction& in, std::string_view m);

  bool eval_instructions(std::uint32_t first, std::uint32_t last);
  std::uint32_t entry_skip(std::uint32_t b);
  bool eval_block(std::uint32_t b, std::uint32_t from, std::uint32_t trim);
  bool eval_nodes(const std::vector<AbcNode>& nodes, std::vector<AbcStmt>& out);
  bool eval_node(const AbcNode& node);
  bool eval_arm(const std::vector<AbcNode>& nodes, std::vector<AbcStmt>& out,
                const std::vector<AbcExpr>& stack, const std::vector<ScopeEntry>& scope);
  bool close_withs(std::vector<AbcStmt>& list);

  AbcExpr branch_condition(const AbcInstruction& in);
  AbcExpr build_condition(const std::vector<AbcCondStep>& steps, const std::vector<AbcExpr>& conds,
                          std::uint32_t index);
  bool eval_condition(const std::vector<AbcCondStep>& steps, std::uint32_t skip, AbcExpr& out);
  bool eval_condition_into(const std::vector<AbcCondStep>& steps, std::uint32_t skip, AbcExpr& cond,
                           std::vector<AbcStmt>& list);

  bool eval_if(const AbcNode& node);
  bool eval_loop(const AbcNode& node);
  bool eval_switch(const AbcNode& node);
  bool eval_try(const AbcNode& node);
  bool eval_short_circuit(const AbcNode& node);
  bool eval_ternary(const AbcNode& node);
};

}  // namespace fo4::abc::detail
