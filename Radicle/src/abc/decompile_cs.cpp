#include "decompile_write.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc {

namespace {

using detail::CsNameStatus;
using detail::CsRenderer;

std::string pad(std::size_t depth) { return std::string(depth * 2, ' '); }

std::string after_scope(const std::string& full) {
  const std::size_t at = full.rfind("::");
  return at == std::string::npos ? full : full.substr(at + 2);
}

struct Body {
  bool present{false};
  std::string inner;
  std::string init;
};

class CsWriter {
public:
  CsWriter(const AbcFile& file, AbcAs3Output& out)
      : file_(file), out_(out), packages_(detail::package_namespaces(file)), written_(file.instances.size(), false) {}

  void run() {
    for (std::uint32_t i = 0; i < file_.scripts.size(); ++i) {
      write_script(i);
    }
    for (std::uint32_t i = 0; i < file_.instances.size(); ++i) {
      if (!written_[i]) {
        write_class_top(i);
        text_ += "\n";
      }
    }
    finish();
  }

  void run_class(std::uint32_t index) {
    if (index < file_.instances.size()) {
      write_class_top(index);
    }
    finish();
  }

  void run_method(std::uint32_t method) {
    if (method < file_.methods.size()) {
      write_owned(detail::find_method_owner(file_, method), method);
    }
    finish();
  }

private:
  const AbcFile& file_;
  AbcAs3Output& out_;
  detail::PackageSet packages_;
  std::vector<bool> written_;
  std::string text_;

  void finish() {
    out_.text = std::move(text_);
    out_.hash = as3_text_hash(out_.text);
  }

  void note_unmapped(const std::vector<std::string>& names, const std::string& where) {
    for (const std::string& name : names) {
      AbcDecompileIssue issue;
      issue.status = AbcDecompileStatus::UnmappedName;
      issue.message = "'" + name + "' in a " + where + " has no C# mapping";
      out_.refused.push_back(issue);
    }
  }

  std::string type_of(std::uint32_t index, std::vector<std::string>& unmapped) const {
    return detail::cs_type_text(index == 0 ? std::string{"*"} : file_.multiname_at(index), packages_, unmapped);
  }

  std::string member_name(std::uint32_t index) const {
    const std::string full = file_.multiname_at(index);
    const detail::CsName name = detail::cs_resolve_member(full);
    return name.status == CsNameStatus::Unmapped ? detail::cs_identifier(after_scope(full)) : name.text;
  }

  std::string access_of(std::uint32_t name) const {
    if (name < file_.pool.multinames.size()) {
      const AbcMultiname& multiname = file_.pool.multinames[name];
      const bool qualified = multiname.kind == AbcMultinameKind::QName || multiname.kind == AbcMultinameKind::QNameA;
      if (qualified && multiname.ns < file_.pool.namespaces.size()) {
        switch (file_.pool.namespaces[multiname.ns].kind) {
          case AbcNamespaceKind::Private:
            return "private";
          case AbcNamespaceKind::Protected:
          case AbcNamespaceKind::StaticProtected:
            return "protected";
          case AbcNamespaceKind::PackageInternal:
            return "internal";
          default:
            break;
        }
      }
    }
    return "public";
  }

  std::string value_cs(AbcValueKind kind, std::uint32_t index) const {
    switch (kind) {
      case AbcValueKind::Utf8:
        return detail::cs_literal(detail::quote_string(file_.string_at(index)));
      case AbcValueKind::Double:
        return index < file_.pool.doubles.size() ? detail::cs_literal(detail::format_double(file_.pool.doubles[index]))
                                                 : std::string{"0"};
      case AbcValueKind::Int:
      case AbcValueKind::UInt:
      case AbcValueKind::True:
      case AbcValueKind::False:
      case AbcValueKind::Null:
      case AbcValueKind::Undefined:
        return detail::cs_literal(file_.optional_value_at(AbcOptionalValue{kind, index}));
      default:
        return "null";
    }
  }

  std::string param_name(const AbcMethodInfo& info, std::uint32_t i) const {
    std::string name;
    if (info.has_param_names() && i < info.param_names.size()) {
      name = file_.string_at(info.param_names[i]);
    }
    return name.empty() ? "arg" + std::to_string(i + 1) : name;
  }

  std::string params_text(std::uint32_t method, std::vector<std::string>& unmapped) const {
    if (method >= file_.methods.size()) {
      return {};
    }
    const AbcMethodInfo& info = file_.methods[method];
    const std::size_t optional = info.has_optional() ? info.options.size() : 0;
    const std::size_t first_optional =
        optional <= info.param_count ? info.param_count - optional : info.param_count;
    std::string out;
    for (std::uint32_t i = 0; i < info.param_count; ++i) {
      out += (i == 0 ? "" : ", ") +
             type_of(i < info.param_types.size() ? info.param_types[i] : 0, unmapped) + " " +
             detail::cs_identifier(param_name(info, i));
      if (i >= first_optional && i - first_optional < optional) {
        const AbcOptionalValue& value = info.options[i - first_optional];
        out += " = " + value_cs(value.kind, value.index);
      }
    }
    if (info.needs_rest()) {
      out += (info.param_count == 0 ? "" : ", ") + std::string("params object[] rest");
    }
    return out;
  }

  std::string return_text(std::uint32_t method, std::vector<std::string>& unmapped) const {
    return type_of(method < file_.methods.size() ? file_.methods[method].return_type : 0, unmapped);
  }

  void append_indented(const std::string& block, std::size_t depth) {
    std::size_t start = 0;
    while (start < block.size()) {
      std::size_t end = block.find('\n', start);
      if (end == std::string::npos) {
        end = block.size();
      }
      text_ += pad(depth) + block.substr(start, end - start) + "\n";
      start = end + 1;
    }
  }

  Body make_body(std::uint32_t method, bool ctor, bool setter, const std::vector<std::string>& unmapped) {
    Body body;
    const AbcMethodBody* raw = file_.body_for(method);
    if (raw == nullptr) {
      return body;
    }
    body.present = true;
    ++out_.functions;
    AbcFunction function;
    AbcDecompileIssue issue;
    issue.method = method;
    issue.body = static_cast<std::uint32_t>(raw - file_.bodies.data());
    if (!unmapped.empty()) {
      issue.status = AbcDecompileStatus::UnmappedName;
      issue.message = "'" + unmapped[0] + "' in the signature has no C# mapping";
    } else if (decompile_body(file_, issue.body, function, issue) == AbcDecompileStatus::Ok) {
      CsRenderer renderer(packages_);
      if (setter && !function.param_names.empty()) {
        renderer.rename_local(function.param_names[0], "value");
      }
      if (ctor && !function.statements.empty()) {
        const AbcStmt& first = function.statements.front();
        if (first.kind == AbcStmtKind::Expr && first.expr.kind == AbcExprKind::Call &&
            first.expr.args[0].kind == AbcExprKind::Super) {
          std::string arguments;
          for (std::size_t i = 1; i < first.expr.args.size(); ++i) {
            arguments += (i == 1 ? "" : ", ") + renderer.expr(first.expr.args[i]);
          }
          if (!arguments.empty()) {
            body.init = " : base(" + arguments + ")";
          }
          function.statements.erase(function.statements.begin());
        }
      }
      std::string inner;
      for (const std::string& name : function.locals) {
        inner += "dynamic " + detail::cs_identifier(name) + " = null;\n";
      }
      renderer.statements(inner, function.statements, 0);
      if (!renderer.failed()) {
        body.inner = std::move(inner);
        return body;
      }
      issue.status = renderer.status();
      issue.offset = kNoFlowIndex;
      issue.message = renderer.message();
    }
    std::string marker = std::string("/* refused: ") + decompile_status_name(issue.status);
    if (issue.offset != kNoFlowIndex) {
      marker += " at offset " + std::to_string(issue.offset);
    }
    marker += ": " + issue.message + " */";
    out_.refused.push_back(issue);
    body.init.clear();
    body.inner = marker + "\n";
    return body;
  }

  void emit_block(std::size_t depth, const std::string& header, const Body& body, bool skip_empty) {
    if (skip_empty && (!body.present || body.inner.empty())) {
      return;
    }
    if (!body.present) {
      text_ += pad(depth) + header + ";\n";
      return;
    }
    text_ += pad(depth) + header + body.init + " {\n";
    append_indented(body.inner, depth + 1);
    text_ += pad(depth) + "}\n";
  }

  void write_callable(std::size_t depth, const std::string& lead, const std::string& name, std::uint32_t method,
                      bool ctor, bool skip_empty) {
    std::vector<std::string> unmapped;
    const std::string ret = ctor ? std::string{} : return_text(method, unmapped) + " ";
    const std::string params = params_text(method, unmapped);
    const bool native = method < file_.methods.size() && file_.methods[method].is_native();
    const Body body = make_body(method, ctor, false, unmapped);
    if (!body.present && !unmapped.empty()) {
      note_unmapped(unmapped, "method signature");
    }
    emit_block(depth, lead + (native ? "extern " : "") + ret + name + "(" + params + ")", body, skip_empty);
  }

  std::string method_lead(const AbcTrait& trait, bool is_static, bool iface) const {
    if (iface) {
      return {};
    }
    const std::string access = access_of(trait.name);
    if (is_static) {
      return access + " static ";
    }
    if (trait.is_override()) {
      return access + (trait.is_final() ? " sealed override " : " override ");
    }
    return trait.is_final() || access == "private" ? access + " " : access + " virtual ";
  }

  void write_field(std::size_t depth, const AbcTrait& trait, bool is_static) {
    std::vector<std::string> unmapped;
    const std::string type = type_of(trait.type_name, unmapped);
    note_unmapped(unmapped, "field type");
    const bool has_value = trait.value_index != 0;
    const bool primitive = type == "int" || type == "uint" || type == "double" || type == "string" || type == "bool";
    const bool is_const = trait.kind == AbcTraitKind::Const;
    std::string lead = access_of(trait.name) + " ";
    if (is_const && has_value && primitive) {
      lead += "const ";
    } else {
      lead += (is_static ? "static " : "") + std::string(is_const ? "readonly " : "");
    }
    text_ += pad(depth) + lead + type + " " + member_name(trait.name);
    if (has_value) {
      text_ += " = " + value_cs(trait.value_kind, trait.value_index);
    }
    text_ += ";\n";
  }

  void write_property(std::size_t depth, const AbcTrait& first, bool is_static, bool iface, const AbcTrait* getter,
                      const AbcTrait* setter) {
    std::vector<std::string> unmapped;
    std::string type;
    if (getter != nullptr) {
      type = return_text(getter->method_index, unmapped);
    } else {
      const std::uint32_t method = setter->method_index;
      const bool typed = method < file_.methods.size() && !file_.methods[method].param_types.empty();
      type = type_of(typed ? file_.methods[method].param_types[0] : 0, unmapped);
    }
    note_unmapped(unmapped, "property type");
    text_ += pad(depth) + method_lead(first, is_static, iface) + type + " " + member_name(first.name) + " {\n";
    const AbcTrait* accessors[2] = {getter, setter};
    for (int i = 0; i < 2; ++i) {
      if (accessors[i] == nullptr) {
        continue;
      }
      const std::string keyword = i == 0 ? "get" : "set";
      const Body body = make_body(accessors[i]->method_index, false, i == 1, {});
      if (iface || !body.present) {
        text_ += pad(depth + 1) + keyword + ";\n";
        continue;
      }
      text_ += pad(depth + 1) + keyword + " {\n";
      append_indented(body.inner, depth + 2);
      text_ += pad(depth + 1) + "}\n";
    }
    text_ += pad(depth) + "}\n";
  }

  void write_traits(std::size_t depth, const std::vector<const AbcTrait*>& traits, bool is_static, bool iface) {
    std::vector<bool> done(traits.size(), false);
    for (std::size_t i = 0; i < traits.size(); ++i) {
      if (done[i]) {
        continue;
      }
      const AbcTrait& trait = *traits[i];
      switch (trait.kind) {
        case AbcTraitKind::Slot:
        case AbcTraitKind::Const:
          write_field(depth, trait, is_static);
          break;
        case AbcTraitKind::Method:
        case AbcTraitKind::Function:
          write_callable(depth, method_lead(trait, is_static, iface), member_name(trait.name),
                         trait.kind == AbcTraitKind::Function ? trait.class_index : trait.method_index, false,
                         false);
          break;
        case AbcTraitKind::Getter:
        case AbcTraitKind::Setter: {
          const bool is_getter = trait.kind == AbcTraitKind::Getter;
          const AbcTrait* partner = nullptr;
          for (std::size_t j = i + 1; j < traits.size() && partner == nullptr; ++j) {
            const bool other = traits[j]->kind == (is_getter ? AbcTraitKind::Setter : AbcTraitKind::Getter);
            if (!done[j] && other && file_.multiname_at(traits[j]->name) == file_.multiname_at(trait.name)) {
              partner = traits[j];
              done[j] = true;
            }
          }
          write_property(depth, trait, is_static, iface, is_getter ? &trait : partner, is_getter ? partner : &trait);
          break;
        }
        case AbcTraitKind::Class:
          text_ += pad(depth) + "/* class " + file_.multiname_at(trait.name) + " -> " +
                   file_.class_name(trait.class_index) + " */\n";
          break;
      }
    }
  }

  void write_class(std::uint32_t index, std::size_t depth) {
    written_[index] = true;
    const AbcInstanceInfo& instance = file_.instances[index];
    const std::string name = detail::cs_identifier(after_scope(file_.multiname_at(instance.name)));
    const bool iface = instance.is_interface();
    std::string head = pad(depth) + access_of(instance.name) + " ";
    head += iface ? "interface " : (instance.is_final() ? "sealed class " : "class ");
    head += name;
    std::string bases;
    std::string marks;
    std::vector<std::uint32_t> parents;
    if (!iface && instance.super_index != 0 && file_.multiname_at(instance.super_index) != "Object") {
      parents.push_back(instance.super_index);
    }
    for (const std::uint32_t parent : instance.interfaces) {
      parents.push_back(parent);
    }
    for (const std::uint32_t parent : parents) {
      std::vector<std::string> unmapped;
      const std::string text = type_of(parent, unmapped);
      note_unmapped(unmapped, "class header");
      for (const std::string& missing : unmapped) {
        marks += " /* unmapped " + missing + " */";
      }
      if (unmapped.empty()) {
        bases += (bases.empty() ? " : " : ", ") + text;
      }
    }
    text_ += head + bases + marks + " {\n";
    if (!iface) {
      write_callable(depth + 1, "public ", name, instance.iinit, true, false);
    }
    std::vector<const AbcTrait*> members;
    for (const AbcTrait& trait : instance.traits) {
      members.push_back(&trait);
    }
    write_traits(depth + 1, members, false, iface);
    if (index < file_.classes.size()) {
      members.clear();
      for (const AbcTrait& trait : file_.classes[index].traits) {
        members.push_back(&trait);
      }
      write_traits(depth + 1, members, true, false);
      write_callable(depth + 1, "static ", "__class_init", file_.classes[index].cinit, false, true);
    }
    text_ += pad(depth) + "}\n";
  }

  void write_class_top(std::uint32_t index) {
    const std::string full = file_.multiname_at(file_.instances[index].name);
    const std::size_t at = full.rfind("::");
    if (at == std::string::npos) {
      write_class(index, 0);
      return;
    }
    text_ += "namespace " + detail::cs_dotted(full.substr(0, at)) + " {\n";
    write_class(index, 1);
    text_ += "}\n";
  }

  void write_script(std::uint32_t index) {
    const AbcScriptInfo& script = file_.scripts[index];
    std::vector<const AbcTrait*> members;
    for (const AbcTrait& trait : script.traits) {
      if (trait.kind != AbcTraitKind::Class) {
        members.push_back(&trait);
      } else if (trait.class_index < file_.instances.size() && !written_[trait.class_index]) {
        write_class_top(trait.class_index);
        text_ += "\n";
      }
    }
    std::string outer = std::move(text_);
    text_.clear();
    write_traits(1, members, true, false);
    write_callable(1, "public static ", "__script_init", script.init, false, true);
    std::string inner = std::move(text_);
    text_ = std::move(outer);
    if (!inner.empty()) {
      text_ += "public static class Script" + std::to_string(index) + " {\n" + inner + "}\n\n";
    }
  }

  void write_owned(const detail::MethodOwner& owner, std::uint32_t method) {
    using Kind = detail::MethodOwner::Kind;
    std::vector<const AbcTrait*> one;
    if (owner.trait != nullptr) {
      one.push_back(owner.trait);
    }
    switch (owner.kind) {
      case Kind::None:
        write_callable(0, "public static ", "__method" + std::to_string(method), method, false, false);
        return;
      case Kind::Script:
        if (owner.trait != nullptr) {
          write_traits(0, one, true, false);
        } else {
          write_callable(0, "public static ", "__script_init", method, false, false);
        }
        return;
      case Kind::Instance:
        if (owner.trait != nullptr) {
          write_traits(0, one, false, file_.instances[owner.index].is_interface());
        } else {
          write_callable(0, "public ", detail::cs_identifier(after_scope(file_.class_name(owner.index))), method,
                         true, false);
        }
        return;
      case Kind::Class:
        if (owner.trait != nullptr) {
          write_traits(0, one, true, false);
        } else {
          write_callable(0, "static ", "__class_init", method, false, false);
        }
        return;
    }
  }
};

}  // namespace

AbcCsOutput write_cs(const AbcFile& file) {
  AbcCsOutput out;
  CsWriter writer(file, out);
  writer.run();
  return out;
}

AbcCsOutput write_cs_class(const AbcFile& file, std::uint32_t class_index) {
  AbcCsOutput out;
  CsWriter writer(file, out);
  writer.run_class(class_index);
  return out;
}

AbcCsOutput write_cs_method(const AbcFile& file, std::uint32_t method_index) {
  AbcCsOutput out;
  CsWriter writer(file, out);
  writer.run_method(method_index);
  return out;
}

}  // namespace fo4::abc
