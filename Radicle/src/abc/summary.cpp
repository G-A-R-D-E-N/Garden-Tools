#include <fo4/abc/summary.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace fo4::abc {

namespace {

constexpr std::size_t kArgumentRunLimit = 16;

bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool has_suffix(const std::string& value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_call_like(std::string_view mnemonic) {
  return starts_with(mnemonic, "call") || starts_with(mnemonic, "construct");
}

bool is_receiver_source(std::string_view mnemonic) {
  return mnemonic == "getlex" || mnemonic == "getproperty" || mnemonic == "findproperty" ||
         mnemonic == "findpropstrict" || mnemonic == "getsuper";
}

bool ends_argument_run(const AbcInstruction& instruction) {
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Offset || operand.role == AbcOperandRole::Switch) {
      return true;
    }
  }
  const std::string_view mnemonic{instruction.mnemonic};
  if (is_call_like(mnemonic)) {
    return true;
  }
  return mnemonic == "returnvoid" || mnemonic == "returnvalue" || mnemonic == "throw" ||
         mnemonic == "label" || mnemonic == "newclass" || mnemonic == "newcatch" ||
         mnemonic == "pushscope" || mnemonic == "popscope" || mnemonic == "pushwith" ||
         mnemonic == "kill" || mnemonic == "debug";
}

std::vector<std::string> collect_literals(const AbcFile& file,
                                          const std::vector<AbcInstruction>& code,
                                          std::size_t call_index) {
  std::vector<std::string> literals;
  std::size_t examined = 0;
  for (std::size_t i = call_index; i > 0 && examined < kArgumentRunLimit;) {
    i -= 1;
    ++examined;
    const AbcInstruction& instruction = code[i];
    if (std::string_view{instruction.mnemonic} == "pushstring" && !instruction.operands.empty()) {
      literals.push_back(file.string_at(instruction.operands[0].value));
    }
    if (ends_argument_run(instruction)) {
      break;
    }
  }
  std::reverse(literals.begin(), literals.end());
  return literals;
}

std::string collect_receiver(const AbcFile& file, const std::vector<AbcInstruction>& code,
                             std::size_t call_index) {
  std::size_t examined = 0;
  for (std::size_t i = call_index; i > 0 && examined < kArgumentRunLimit;) {
    i -= 1;
    ++examined;
    const AbcInstruction& instruction = code[i];
    if (is_receiver_source(std::string_view{instruction.mnemonic})) {
      for (const AbcDecodedOperand& operand : instruction.operands) {
        if (operand.role == AbcOperandRole::Multiname) {
          return file.multiname_at(operand.value);
        }
      }
    }
    if (ends_argument_run(instruction)) {
      break;
    }
  }
  return std::string{};
}

bool build_call_site(const AbcFile& file, const std::vector<AbcInstruction>& code,
                     std::size_t call_index, AbcCallSite& out) {
  const AbcInstruction& instruction = code[call_index];
  const std::string_view mnemonic{instruction.mnemonic};
  if (!is_call_like(mnemonic)) {
    return false;
  }
  const AbcDecodedOperand* name_operand = nullptr;
  const AbcDecodedOperand* count_operand = nullptr;
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Multiname && name_operand == nullptr) {
      name_operand = &operand;
    } else if (operand.role == AbcOperandRole::Count && count_operand == nullptr) {
      count_operand = &operand;
    }
  }
  if (name_operand == nullptr) {
    return false;
  }
  out.offset = instruction.offset;
  out.opcode = std::string{mnemonic};
  out.target = file.multiname_at(name_operand->value);
  out.receiver = collect_receiver(file, code, call_index);
  out.arg_count = count_operand == nullptr ? 0u : count_operand->value;
  out.literals = collect_literals(file, code, call_index);
  out.literal_unambiguous = out.literals.size() == 1;
  return true;
}

std::string member_name(const std::string& multiname) {
  const std::size_t separator = multiname.rfind("::");
  return separator == std::string::npos ? multiname : multiname.substr(separator + 2);
}

std::string method_label(const AbcFile& file, std::uint32_t method_index,
                         const std::string& declared, const char* fallback = nullptr) {
  if (!declared.empty()) {
    return declared;
  }
  const std::string name = file.method_name(method_index);
  if (!name.empty()) {
    return name;
  }
  if (fallback != nullptr && fallback[0] != '\0') {
    return fallback;
  }
  return "method#" + std::to_string(method_index);
}

AbcStatus build_method_summary(const AbcFile& file, std::uint32_t method_index,
                               const std::string& name, AbcMethodSummary& out) {
  out = AbcMethodSummary{};
  out.name = name;
  if (static_cast<std::size_t>(method_index) < file.methods.size()) {
    const AbcMethodInfo& method = file.methods[method_index];
    out.return_type = file.multiname_at(method.return_type);
    out.flags = method.flags;
  }
  const AbcMethodBody* body = file.body_for(method_index);
  if (body == nullptr) {
    return AbcStatus::Ok;
  }
  std::vector<AbcInstruction> code;
  AbcStatus status = decode_code(body->code, code, body->exceptions);
  if (status != AbcStatus::Ok) {
    return status;
  }
  status = validate_operand_pools(file, code);
  if (status != AbcStatus::Ok) {
    return status;
  }
  out.instruction_count = code.size();
  for (std::size_t i = 0; i < code.size(); ++i) {
    for (const AbcDecodedOperand& operand : code[i].operands) {
      if (operand.role != AbcOperandRole::Multiname) {
        continue;
      }
      const std::string resolved = file.multiname_at(operand.value);
      if (std::find(out.multinames.begin(), out.multinames.end(), resolved) ==
          out.multinames.end()) {
        out.multinames.push_back(resolved);
      }
    }
    AbcCallSite site;
    if (build_call_site(file, code, i, site)) {
      out.calls.push_back(std::move(site));
    }
  }
  return AbcStatus::Ok;
}

bool body_index_for(const AbcFile& file, std::uint32_t method_index, std::size_t& out) {
  for (std::size_t i = 0; i < file.bodies.size(); ++i) {
    if (file.bodies[i].method == method_index) {
      out = i;
      return true;
    }
  }
  return false;
}

}

bool is_event_registration(const AbcCallSite& site) {
  const std::string member = member_name(site.target);
  return member == "addEventListener" || member == "removeEventListener";
}

bool is_external_interface_call(const AbcCallSite& site) {
  return member_name(site.target) == "call" && has_suffix(site.receiver, "ExternalInterface");
}

AbcStatus summarize(const AbcFile& file, AbcSummary& out) {
  out = AbcSummary{};
  std::vector<bool> consumed(file.bodies.size(), false);
  for (const AbcMethodBody& body : file.bodies) {
    if (static_cast<std::size_t>(body.method) >= file.methods.size()) {
      return AbcStatus::IndexOutOfRange;
    }
  }
  for (std::size_t i = 0; i < file.instances.size(); ++i) {
    const AbcInstanceInfo& instance = file.instances[i];
    AbcClassSummary klass;
    klass.name = file.multiname_at(instance.name);
    klass.super_name = file.multiname_at(instance.super_index);
    for (const std::uint32_t interface_index : instance.interfaces) {
      klass.interfaces.push_back(file.multiname_at(interface_index));
    }
    std::vector<std::pair<std::uint32_t, std::string>> methods;
    methods.emplace_back(instance.iinit, std::string{"<init>"});
    for (const AbcTrait& trait : instance.traits) {
      if (trait.kind != AbcTraitKind::Method && trait.kind != AbcTraitKind::Getter &&
          trait.kind != AbcTraitKind::Setter) {
        continue;
      }
      methods.emplace_back(trait.method_index, file.multiname_at(trait.name));
    }
    for (const auto& entry : methods) {
      AbcMethodSummary method;
      const AbcStatus status = build_method_summary(
          file, entry.first, method_label(file, entry.first, entry.second), method);
      if (status != AbcStatus::Ok) {
        return status;
      }
      std::size_t body_index = 0;
      if (body_index_for(file, entry.first, body_index)) {
        consumed[body_index] = true;
      }
      klass.methods.push_back(std::move(method));
    }
    out.classes.push_back(std::move(klass));
  }
  for (const AbcScriptInfo& script : file.scripts) {
    AbcMethodSummary method;
    const AbcStatus status = build_method_summary(
        file, script.init, method_label(file, script.init, std::string{}, "<script>"), method);
    if (status != AbcStatus::Ok) {
      return status;
    }
    std::size_t body_index = 0;
    if (body_index_for(file, script.init, body_index)) {
      consumed[body_index] = true;
    }
    out.scripts.push_back(std::move(method));
  }
  for (std::size_t i = 0; i < file.bodies.size(); ++i) {
    if (consumed[i]) {
      continue;
    }
    const std::uint32_t method_index = file.bodies[i].method;
    AbcMethodSummary method;
    const AbcStatus status = build_method_summary(
        file, method_index, method_label(file, method_index, std::string{}), method);
    if (status != AbcStatus::Ok) {
      return status;
    }
    out.functions.push_back(std::move(method));
  }
  return AbcStatus::Ok;
}

}
