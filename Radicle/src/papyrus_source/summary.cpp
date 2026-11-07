#include <fo4/papyrus_source/summary.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace fo4::psc {

namespace {

void append_unique(std::vector<std::string>& out, const std::string& value) {
  if (value.empty()) return;
  for (const std::string& existing : out) {
    if (existing == value) return;
  }
  out.push_back(value);
}

bool equals_ignore_case(const std::string& left, const char* right) {
  std::size_t at = 0;
  for (; at < left.size() && right[at] != '\0'; ++at) {
    char first = left[at];
    char second = right[at];
    if (first >= 'A' && first <= 'Z') first = static_cast<char>(first - 'A' + 'a');
    if (second >= 'A' && second <= 'Z') second = static_cast<char>(second - 'A' + 'a');
    if (first != second) return false;
  }
  return at == left.size() && right[at] == '\0';
}

bool is_goto_state(const std::string& path) {
  const std::size_t length = 9;
  if (path.size() < length) return false;
  const std::size_t at = path.size() - length;
  if (at > 0 && path[at - 1] != '.') return false;
  return equals_ignore_case(path.substr(at), "gotostate");
}

bool build_path(const Expression& expression, std::string& out) {
  switch (expression.kind) {
    case ExpressionKind::Identifier:
      out = expression.text;
      return true;
    case ExpressionKind::Member: {
      std::string base;
      if (expression.left == nullptr || !build_path(*expression.left, base)) return false;
      out = base + "." + expression.text;
      return true;
    }
    case ExpressionKind::Index: {
      std::string base;
      if (expression.left == nullptr || !build_path(*expression.left, base)) return false;
      out = base + "[]";
      return true;
    }
    case ExpressionKind::Call: {
      std::string base;
      if (expression.left == nullptr || !build_path(*expression.left, base)) return false;
      out = base + "()";
      return true;
    }
    default:
      return false;
  }
}

void walk_expression(const Expression& expression, bool written, FunctionSummary& summary);

void record_transition(const Expression& call, FunctionSummary& summary) {
  if (call.arguments.empty() || call.arguments[0] == nullptr) return;
  const Expression& argument = *call.arguments[0];
  if (argument.kind == ExpressionKind::StringLiteral) {
    append_unique(summary.stateTransitions, argument.stringValue);
    return;
  }
  if (argument.kind == ExpressionKind::Identifier) {
    append_unique(summary.stateTransitions, argument.text);
  }
}

void walk_call(const Expression& expression, FunctionSummary& summary) {
  std::string path;
  if (expression.left != nullptr && build_path(*expression.left, path)) {
    if (is_goto_state(path)) {
      record_transition(expression, summary);
    } else {
      append_unique(summary.calledFunctions, path);
    }
  }
  for (const ExpressionPtr& argument : expression.arguments) {
    if (argument != nullptr) walk_expression(*argument, false, summary);
  }
}

void walk_expression(const Expression& expression, bool written, FunctionSummary& summary) {
  switch (expression.kind) {
    case ExpressionKind::None:
    case ExpressionKind::BoolLiteral:
    case ExpressionKind::IntegerLiteral:
    case ExpressionKind::FloatLiteral:
    case ExpressionKind::StringLiteral:
      return;
    case ExpressionKind::Identifier:
      append_unique(written ? summary.propertiesWritten : summary.propertiesRead, expression.text);
      return;
    case ExpressionKind::Member: {
      std::string path;
      if (build_path(expression, path)) {
        append_unique(written ? summary.propertiesWritten : summary.propertiesRead, path);
      }
      return;
    }
    case ExpressionKind::Index:
      if (expression.left != nullptr) walk_expression(*expression.left, written, summary);
      if (expression.right != nullptr) walk_expression(*expression.right, false, summary);
      return;
    case ExpressionKind::New:
      if (expression.left != nullptr) walk_expression(*expression.left, false, summary);
      return;
    case ExpressionKind::Unary:
    case ExpressionKind::Cast:
    case ExpressionKind::Is:
      if (expression.left != nullptr) walk_expression(*expression.left, false, summary);
      return;
    case ExpressionKind::Binary:
      if (expression.left != nullptr) walk_expression(*expression.left, false, summary);
      if (expression.right != nullptr) walk_expression(*expression.right, false, summary);
      return;
    case ExpressionKind::Call:
      walk_call(expression, summary);
      return;
  }
}

void walk_statement(const Statement& statement, FunctionSummary& summary) {
  switch (statement.kind) {
    case StatementKind::Expression:
      if (statement.value != nullptr) walk_expression(*statement.value, false, summary);
      return;
    case StatementKind::Assignment:
      if (statement.target != nullptr) walk_expression(*statement.target, true, summary);
      if (statement.value != nullptr) walk_expression(*statement.value, false, summary);
      return;
    case StatementKind::VariableDeclaration:
    case StatementKind::Return:
      if (statement.value != nullptr) walk_expression(*statement.value, false, summary);
      return;
    case StatementKind::If:
      for (const IfBranch& branch : statement.branches) {
        if (branch.condition != nullptr) walk_expression(*branch.condition, false, summary);
        for (const StatementPtr& nested : branch.body) {
          if (nested != nullptr) walk_statement(*nested, summary);
        }
      }
      return;
    case StatementKind::While:
      if (statement.value != nullptr) walk_expression(*statement.value, false, summary);
      for (const StatementPtr& nested : statement.body) {
        if (nested != nullptr) walk_statement(*nested, summary);
      }
      return;
  }
}

FunctionSummary summarize_function(const FunctionDeclaration& function, const std::string& state) {
  FunctionSummary summary;
  summary.name = function.name;
  summary.state = state;
  summary.isEvent = function.kind == FunctionKind::Event;
  summary.isNative = function.isNative;
  for (const StatementPtr& statement : function.body) {
    if (statement != nullptr) walk_statement(*statement, summary);
  }
  return summary;
}

FunctionSummary summarize_custom_event(const CustomEventDeclaration& event, const std::string& state) {
  FunctionSummary summary;
  summary.name = event.name;
  summary.state = state;
  summary.isEvent = true;
  summary.isNative = event.isNative;
  return summary;
}

std::string join_state(const std::string& prefix, const std::string& name) {
  return prefix.empty() ? name : prefix + "." + name;
}

void collect_state(const StateDeclaration& state, const std::string& prefix, ScriptSummary& summary) {
  const std::string path = join_state(prefix, state.name);
  append_unique(summary.states, path);
  for (const FunctionDeclaration& function : state.functions) {
    summary.functions.push_back(summarize_function(function, path));
  }
  for (const CustomEventDeclaration& event : state.customEvents) {
    summary.functions.push_back(summarize_custom_event(event, path));
  }
  for (const StateDeclaration& nested : state.states) {
    collect_state(nested, path, summary);
  }
}

}  // namespace

ScriptSummary summarize(const Script& script) {
  ScriptSummary summary;
  summary.name = script.name;
  summary.extends = script.extends;
  summary.autoState = script.autoState;
  summary.imports = script.imports;
  for (const PropertyDeclaration& property : script.properties) {
    append_unique(summary.declaredProperties, property.name);
    if (property.isAuto || property.isAutoReadOnly) append_unique(summary.autoProperties, property.name);
    for (const FunctionDeclaration& accessor : property.accessors) {
      summary.functions.push_back(summarize_function(accessor, std::string()));
      summary.functions.back().name = property.name + "." + accessor.name;
    }
  }
  for (const FunctionDeclaration& function : script.functions) {
    summary.functions.push_back(summarize_function(function, std::string()));
  }
  for (const CustomEventDeclaration& event : script.customEvents) {
    summary.functions.push_back(summarize_custom_event(event, std::string()));
  }
  for (const StateDeclaration& state : script.states) {
    collect_state(state, std::string(), summary);
  }
  for (const FunctionSummary& function : summary.functions) {
    for (const std::string& name : function.calledFunctions) append_unique(summary.calledFunctions, name);
    for (const std::string& name : function.propertiesRead) append_unique(summary.propertiesRead, name);
    for (const std::string& name : function.propertiesWritten) append_unique(summary.propertiesWritten, name);
    for (const std::string& name : function.stateTransitions) append_unique(summary.stateTransitions, name);
  }
  return summary;
}

const FunctionSummary* find_function(const ScriptSummary& summary, const std::string& name) {
  for (const FunctionSummary& function : summary.functions) {
    if (function.name == name) return &function;
  }
  return nullptr;
}

}  // namespace fo4::psc
