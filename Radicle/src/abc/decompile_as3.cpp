#include "decompile_write.hpp"
#include "fo4/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc {

std::uint64_t as3_text_hash(const std::string& text) {
  return fnv1a64(text);
}

std::string as3_hash_hex(std::uint64_t hash) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
  return buffer;
}

namespace {

std::string pad(std::size_t depth) { return std::string(depth * 2, ' '); }

std::string simple_name(const std::string& full) {
  const std::size_t at = full.rfind("::");
  return at == std::string::npos ? full : full.substr(at + 2);
}

class As3Writer {
public:
  As3Writer(const AbcFile& file, AbcAs3Output& out)
      : file_(file), out_(out), written_(file.instances.size(), false) {}

  void run() {
    for (const AbcScriptInfo& script : file_.scripts) {
      for (const AbcTrait& trait : script.traits) {
        write_trait(0, trait, false);
        text_ += "\n";
      }
      write_function(0, "", "$init", script.init, true, true);
    }
    for (std::uint32_t i = 0; i < file_.instances.size(); ++i) {
      if (!written_[i]) {
        write_class(i, 0);
        text_ += "\n";
      }
    }
    finish();
  }

  void run_class(std::uint32_t index) {
    if (index < file_.instances.size()) {
      write_class(index, 0);
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
  std::vector<bool> written_;
  std::string text_;

  void finish() {
    out_.text = std::move(text_);
    out_.hash = as3_text_hash(out_.text);
  }

  void write_owned(const detail::MethodOwner& owner, std::uint32_t method) {
    using Kind = detail::MethodOwner::Kind;
    switch (owner.kind) {
      case Kind::None:
        write_function(0, "", "$method" + std::to_string(method), method, true, false);
        return;
      case Kind::Script:
        if (owner.trait != nullptr) {
          write_trait(0, *owner.trait, false);
        } else {
          write_function(0, "", "$init", method, true, false);
        }
        return;
      case Kind::Instance:
        if (owner.trait != nullptr) {
          write_trait(0, *owner.trait, false);
        } else {
          write_function(0, "", simple_name(file_.class_name(owner.index)), method, false, false);
        }
        return;
      case Kind::Class:
        if (owner.trait != nullptr) {
          write_trait(0, *owner.trait, true);
        } else {
          write_function(0, "static ", "$cinit", method, true, false);
        }
        return;
    }
  }

  std::string type_text(std::uint32_t index) const {
    return index == 0 ? std::string{"*"} : file_.multiname_at(index);
  }

  std::string value_text(AbcValueKind kind, std::uint32_t index) const {
    if (kind == AbcValueKind::Utf8) {
      return detail::quote_string(file_.string_at(index));
    }
    if (kind == AbcValueKind::Double && index < file_.pool.doubles.size()) {
      return detail::format_double(file_.pool.doubles[index]);
    }
    return file_.optional_value_at(AbcOptionalValue{kind, index});
  }

  std::string signature(std::uint32_t method, bool with_return) const {
    if (method >= file_.methods.size()) {
      return with_return ? "():*" : "()";
    }
    const AbcMethodInfo& info = file_.methods[method];
    const std::size_t optional = info.has_optional() ? info.options.size() : 0;
    const std::size_t first_optional =
        optional <= info.param_count ? info.param_count - optional : info.param_count;
    std::string out = "(";
    for (std::uint32_t i = 0; i < info.param_count; ++i) {
      if (i != 0) {
        out += ", ";
      }
      std::string name;
      if (info.has_param_names() && i < info.param_names.size()) {
        name = file_.string_at(info.param_names[i]);
      }
      out += name.empty() ? "arg" + std::to_string(i + 1) : name;
      out += ":" + type_text(i < info.param_types.size() ? info.param_types[i] : 0);
      if (i >= first_optional && i - first_optional < optional) {
        const AbcOptionalValue& value = info.options[i - first_optional];
        out += " = " + value_text(value.kind, value.index);
      }
    }
    if (info.needs_rest()) {
      out += info.param_count == 0 ? "...rest" : ", ...rest";
    }
    out += ")";
    return with_return ? out + ":" + type_text(info.return_type) : out;
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

  void write_function(std::size_t depth, const std::string& modifiers, const std::string& head,
                      std::uint32_t method, bool with_return, bool skip_empty) {
    const bool native = method < file_.methods.size() && file_.methods[method].is_native();
    const std::string line = pad(depth) + modifiers + (native ? "native " : "") + "function " + head +
                             signature(method, with_return);
    const AbcMethodBody* body = file_.body_for(method);
    if (body == nullptr) {
      if (!skip_empty) {
        text_ += line + ";\n";
      }
      return;
    }
    ++out_.functions;
    AbcFunction function;
    AbcDecompileIssue issue;
    const std::uint32_t index = static_cast<std::uint32_t>(body - file_.bodies.data());
    if (decompile_body(file_, index, function, issue) == AbcDecompileStatus::Ok) {
      const std::string inner = dump_function(function);
      if (inner.empty() && skip_empty) {
        return;
      }
      text_ += line + " {\n";
      append_indented(inner, depth + 1);
      text_ += pad(depth) + "}\n";
      return;
    }
    std::string marker = std::string("/* refused: ") + decompile_status_name(issue.status);
    if (issue.offset != kNoFlowIndex) {
      marker += " at offset " + std::to_string(issue.offset);
    }
    marker += ": " + issue.message + " */";
    out_.refused.push_back(issue);
    text_ += line + " {\n" + pad(depth + 1) + marker + "\n" + pad(depth) + "}\n";
  }

  void write_trait(std::size_t depth, const AbcTrait& trait, bool is_static) {
    const std::string name = file_.multiname_at(trait.name);
    std::string modifiers;
    if (trait.is_final()) {
      modifiers += "final ";
    }
    if (trait.is_override()) {
      modifiers += "override ";
    }
    if (is_static) {
      modifiers += "static ";
    }
    switch (trait.kind) {
      case AbcTraitKind::Slot:
      case AbcTraitKind::Const: {
        text_ += pad(depth) + modifiers + (trait.kind == AbcTraitKind::Const ? "const " : "var ") + name +
                 ":" + type_text(trait.type_name);
        if (trait.value_index != 0) {
          text_ += " = " + value_text(trait.value_kind, trait.value_index);
        }
        text_ += ";\n";
        return;
      }
      case AbcTraitKind::Method:
        write_function(depth, modifiers, name, trait.method_index, true, false);
        return;
      case AbcTraitKind::Getter:
        write_function(depth, modifiers, "get " + name, trait.method_index, true, false);
        return;
      case AbcTraitKind::Setter:
        write_function(depth, modifiers, "set " + name, trait.method_index, true, false);
        return;
      case AbcTraitKind::Function:
        write_function(depth, modifiers, name, trait.class_index, true, false);
        return;
      case AbcTraitKind::Class:
        if (trait.class_index < file_.instances.size() && !written_[trait.class_index]) {
          write_class(trait.class_index, depth);
        } else {
          text_ += pad(depth) + "/* class " + name + " -> " + file_.class_name(trait.class_index) + " */\n";
        }
        return;
    }
  }

  void write_class(std::uint32_t index, std::size_t depth) {
    written_[index] = true;
    const AbcInstanceInfo& instance = file_.instances[index];
    const std::string name = file_.multiname_at(instance.name);
    std::string head = pad(depth);
    std::string relation = " implements ";
    if (instance.is_interface()) {
      head += "interface ";
      relation = " extends ";
    } else {
      head += instance.is_final() ? "final " : "";
      head += instance.is_sealed() ? "" : "dynamic ";
      head += "class ";
    }
    head += name;
    if (!instance.is_interface() && instance.super_index != 0) {
      head += " extends " + type_text(instance.super_index);
    }
    for (std::size_t i = 0; i < instance.interfaces.size(); ++i) {
      head += (i == 0 ? relation : ", ") + type_text(instance.interfaces[i]);
    }
    text_ += head + " {\n";
    if (!instance.is_interface()) {
      write_function(depth + 1, "", simple_name(name), instance.iinit, false, false);
    }
    for (const AbcTrait& trait : instance.traits) {
      write_trait(depth + 1, trait, false);
    }
    if (index < file_.classes.size()) {
      for (const AbcTrait& trait : file_.classes[index].traits) {
        write_trait(depth + 1, trait, true);
      }
      write_function(depth + 1, "static ", "$cinit", file_.classes[index].cinit, true, true);
    }
    text_ += pad(depth) + "}\n";
  }
};

}  // namespace

AbcAs3Output write_as3(const AbcFile& file) {
  AbcAs3Output out;
  As3Writer writer(file, out);
  writer.run();
  return out;
}

AbcAs3Output write_as3_class(const AbcFile& file, std::uint32_t class_index) {
  AbcAs3Output out;
  As3Writer writer(file, out);
  writer.run_class(class_index);
  return out;
}

AbcAs3Output write_as3_method(const AbcFile& file, std::uint32_t method_index) {
  AbcAs3Output out;
  As3Writer writer(file, out);
  writer.run_method(method_index);
  return out;
}

namespace detail {

MethodOwner find_method_owner(const AbcFile& file, std::uint32_t method) {
  const auto names = [method](const AbcTrait& trait) {
    switch (trait.kind) {
      case AbcTraitKind::Method:
      case AbcTraitKind::Getter:
      case AbcTraitKind::Setter:
        return trait.method_index == method;
      case AbcTraitKind::Function:
        return trait.class_index == method;
      default:
        return false;
    }
  };
  const auto scan = [&](MethodOwner::Kind kind, std::uint32_t index, std::uint32_t init,
                        const std::vector<AbcTrait>& traits, MethodOwner& found) {
    if (init == method) {
      found = MethodOwner{kind, index, nullptr};
      return true;
    }
    for (const AbcTrait& trait : traits) {
      if (names(trait)) {
        found = MethodOwner{kind, index, &trait};
        return true;
      }
    }
    return false;
  };
  MethodOwner found;
  for (std::uint32_t i = 0; i < file.scripts.size(); ++i) {
    if (scan(MethodOwner::Kind::Script, i, file.scripts[i].init, file.scripts[i].traits, found)) {
      return found;
    }
  }
  for (std::uint32_t i = 0; i < file.instances.size(); ++i) {
    if (scan(MethodOwner::Kind::Instance, i, file.instances[i].iinit, file.instances[i].traits, found)) {
      return found;
    }
  }
  for (std::uint32_t i = 0; i < file.classes.size(); ++i) {
    if (scan(MethodOwner::Kind::Class, i, file.classes[i].cinit, file.classes[i].traits, found)) {
      return found;
    }
  }
  return MethodOwner{};
}

}  // namespace detail

}  // namespace fo4::abc
