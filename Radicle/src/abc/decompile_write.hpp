#pragma once

#include "decompile_eval.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fo4::abc::detail {

/* Invariant: trait is null for an initializer, and an owner with kind None has no other field set. */
struct MethodOwner {
  enum class Kind : std::uint8_t { None = 0, Script, Instance, Class };
  Kind kind{Kind::None};
  std::uint32_t index{0};
  const AbcTrait* trait{nullptr};
};

MethodOwner find_method_owner(const AbcFile& file, std::uint32_t method);

using PackageSet = std::set<std::string>;

enum class CsNameStatus : std::uint8_t { Mapped = 0, Passthrough, Unmapped, Unsupported };

struct CsName {
  CsNameStatus status{CsNameStatus::Passthrough};
  bool type{false};
  std::string text;
};

PackageSet package_namespaces(const AbcFile& file);
bool is_flash_namespace(const std::string& ns);

/* Requires an AS3 name as written by AbcFile::multiname_at; a table hit wins over every other rule. */
CsName cs_resolve_name(const std::string& as3, const PackageSet& packages);
CsName cs_resolve_member(const std::string& as3);
std::string cs_type_text(const std::string& as3, const PackageSet& packages,
                         std::vector<std::string>& unmapped);
std::string cs_identifier(const std::string& name);
std::string cs_literal(const std::string& as3_literal);
const char* cs_cast_helper(const std::string& type);
std::string cs_dotted(const std::string& ns);

/* Invariant: after fail() the produced text is meaningless and the caller must refuse the function. */
class CsRenderer {
public:
  explicit CsRenderer(const PackageSet& packages) : packages_(packages) {}

  void rename_local(const std::string& from, const std::string& to) { renames_[from] = to; }
  std::string expr(const AbcExpr& expr);
  std::string cond(const AbcExpr& expr, int minimum);
  void statements(std::string& out, const std::vector<AbcStmt>& list, std::size_t depth);
  std::string catch_type(const std::string& type);

  bool failed() const { return failed_; }
  AbcDecompileStatus status() const { return status_; }
  const std::string& message() const { return message_; }

private:
  const PackageSet& packages_;
  std::map<std::string, std::string> renames_;
  bool failed_{false};
  AbcDecompileStatus status_{AbcDecompileStatus::Ok};
  std::string message_;

  void fail(AbcDecompileStatus status, const std::string& message);
  std::string wrap(const AbcExpr& expr, int minimum);
  std::string name_text(const AbcExpr& expr);
  std::string type_name(const std::string& as3);
  std::string args_text(const std::vector<AbcExpr>& args, std::size_t from);
  std::string call_text(const AbcExpr& expr);
  std::string construct_text(const AbcExpr& expr);
  std::string cast_text(const AbcExpr& expr);
  std::string binary_text(const AbcExpr& expr);
  bool plain_callee(const AbcExpr& expr) const;
  void statement(std::string& out, const AbcStmt& stmt, std::size_t depth);
  void if_statement(std::string& out, const AbcStmt& stmt, std::size_t depth);
  void switch_statement(std::string& out, const AbcStmt& stmt, std::size_t depth);
  void try_statement(std::string& out, const AbcStmt& stmt, std::size_t depth);
};

}  // namespace fo4::abc::detail
