#include <fo4/abc/summary.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc {

namespace {

constexpr std::size_t kSimulatedStackLimit = 16;
constexpr std::size_t kInstructionBudget = 4096;

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

std::size_t argument_count(const AbcInstruction& instruction) {
  std::size_t count = 0;
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Count) {
      count = operand.value;
    }
  }
  return count;
}

std::size_t argument_slots(const AbcInstruction& instruction) {
  return std::min<std::size_t>(argument_count(instruction), kSimulatedStackLimit);
}

struct StackValue {
  AbcArgumentKind kind{AbcArgumentKind::Unknown};
  std::string value;
};

using ValueStack = std::vector<StackValue>;

struct CallFrame {
  std::string receiver;
  std::vector<AbcCallArgument> arguments;
};

bool push_literal(const AbcFile& file, const AbcInstruction& instruction, StackValue& out) {
  const std::string_view mnemonic{instruction.mnemonic};
  if (mnemonic == "pushnan") {
    out.kind = AbcArgumentKind::Literal;
    out.value = "NaN";
    return true;
  }
  if (mnemonic == "pushbyte" || mnemonic == "pushshort") {
    if (instruction.operands.empty()) {
      return false;
    }
    out.kind = AbcArgumentKind::Literal;
    out.value = std::to_string(instruction.operands[0].immediate);
    return true;
  }
  AbcOptionalValue value;
  bool indexed = true;
  if (mnemonic == "pushstring") {
    value.kind = AbcValueKind::Utf8;
  } else if (mnemonic == "pushint") {
    value.kind = AbcValueKind::Int;
  } else if (mnemonic == "pushuint") {
    value.kind = AbcValueKind::UInt;
  } else if (mnemonic == "pushdouble") {
    value.kind = AbcValueKind::Double;
  } else if (mnemonic == "pushtrue") {
    value.kind = AbcValueKind::True;
    indexed = false;
  } else if (mnemonic == "pushfalse") {
    value.kind = AbcValueKind::False;
    indexed = false;
  } else if (mnemonic == "pushnull") {
    value.kind = AbcValueKind::Null;
    indexed = false;
  } else if (mnemonic == "pushundefined") {
    value.kind = AbcValueKind::Undefined;
    indexed = false;
  } else {
    return false;
  }
  if (indexed) {
    if (instruction.operands.empty()) {
      return false;
    }
    value.index = instruction.operands[0].value;
  }
  out.kind = AbcArgumentKind::Literal;
  out.value = file.optional_value_at(value);
  return true;
}

bool push_named(const AbcFile& file, const AbcInstruction& instruction, StackValue& out) {
  const std::string_view mnemonic{instruction.mnemonic};
  if (mnemonic != "getlex" && mnemonic != "findpropstrict") {
    return false;
  }
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role != AbcOperandRole::Multiname) {
      continue;
    }
    const std::string name = file.multiname_at(operand.value);
    if (name.empty()) {
      return false;
    }
    out.kind = AbcArgumentKind::Named;
    out.value = name;
    return true;
  }
  return false;
}

std::size_t fixed_pops(std::string_view mnemonic) {
  return mnemonic == "call" ? 2 : 1;
}

bool attributes_receiver(std::string_view mnemonic) {
  return mnemonic != "call";
}

bool pushes_result(std::string_view mnemonic) {
  return mnemonic != "callpropvoid" && mnemonic != "callsupervoid" &&
         mnemonic != "constructsuper";
}

std::size_t runtime_parts(const AbcFile& file, std::uint32_t index) {
  if (static_cast<std::size_t>(index) >= file.pool.multinames.size()) {
    return 0;
  }
  switch (file.pool.multinames[index].kind) {
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA:
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA:
      return 1;
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      return 2;
    default:
      return 0;
  }
}

std::size_t runtime_name_parts(const AbcFile& file, const AbcInstruction& instruction) {
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Multiname) {
      return runtime_parts(file, operand.value);
    }
  }
  return 0;
}

constexpr std::int32_t kUnknownStackDelta = -2;

std::int32_t stack_delta(std::string_view mnemonic) {
  if (starts_with(mnemonic, "getlocal") || mnemonic == "getglobalscope" ||
      mnemonic == "getscopeobject" || mnemonic == "findproperty") {
    return 1;
  }
  if (starts_with(mnemonic, "setlocal")) {
    return -1;
  }
  if (starts_with(mnemonic, "coerce") || starts_with(mnemonic, "convert_") ||
      mnemonic == "astype" || mnemonic == "getproperty" || mnemonic == "getslot" ||
      mnemonic == "negate" || mnemonic == "increment" || mnemonic == "not" ||
      mnemonic == "typeof") {
    return 0;
  }
  return kUnknownStackDelta;
}

void apply_call(const AbcFile& file, const AbcInstruction& instruction, ValueStack& stack,
                CallFrame& frame) {
  const std::string_view mnemonic{instruction.mnemonic};
  const std::size_t argc = argument_slots(instruction);
  frame.arguments.assign(argc, AbcCallArgument{});
  const std::size_t pops = argc + fixed_pops(mnemonic) + runtime_name_parts(file, instruction);
  if (stack.size() < pops) {
    stack.clear();
    return;
  }
  const std::size_t base = stack.size() - pops;
  const std::size_t args_base = stack.size() - argc;
  for (std::size_t slot = 0; slot < argc; ++slot) {
    frame.arguments[slot] = AbcCallArgument{stack[args_base + slot].kind,
                                            stack[args_base + slot].value};
  }
  if (attributes_receiver(mnemonic)) {
    frame.receiver = stack[base].value;
  }
  stack.resize(base);
  if (pushes_result(mnemonic)) {
    stack.push_back(StackValue{});
  }
}

void mark_offset(std::int32_t target, std::vector<bool>& targets) {
  if (target >= 0 && static_cast<std::size_t>(target) < targets.size()) {
    targets[static_cast<std::size_t>(target)] = true;
  }
}

void simulate_arguments(const AbcFile& file, const std::vector<AbcInstruction>& code,
                        std::vector<CallFrame>& frames) {
  frames.assign(code.size(), CallFrame{});
  const std::size_t byte_size = code.empty() ? 0 : code.back().offset + code.back().length;
  std::vector<bool> targets(byte_size + 1, false);
  for (const AbcInstruction& instruction : code) {
    for (const AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Offset) {
        mark_offset(operand.target, targets);
      } else if (operand.role == AbcOperandRole::Switch) {
        mark_offset(operand.target, targets);
        for (const std::int32_t case_target : operand.case_targets) {
          mark_offset(case_target, targets);
        }
      }
    }
  }
  ValueStack stack;
  std::size_t budget = kInstructionBudget;
  for (std::size_t i = 0; i < code.size() && budget > 0; ++i, --budget) {
    const AbcInstruction& instruction = code[i];
    if (instruction.offset < targets.size() && targets[instruction.offset]) {
      stack.clear();
    }
    const std::string_view mnemonic{instruction.mnemonic};
    if (is_call_like(mnemonic)) {
      apply_call(file, instruction, stack, frames[i]);
      continue;
    }
    const std::size_t runtime = runtime_name_parts(file, instruction);
    if (runtime > 0) {
      if (mnemonic != "getproperty" && mnemonic != "findproperty") {
        stack.clear();
        continue;
      }
      stack.resize(stack.size() - std::min(runtime, stack.size()));
    }
    if (mnemonic == "astypelate") {
      if (stack.size() < 2) {
        stack.clear();
      } else {
        stack.pop_back();
        stack.back() = StackValue{};
      }
      continue;
    }
    StackValue value;
    if (push_literal(file, instruction, value) || push_named(file, instruction, value)) {
      if (stack.size() < kSimulatedStackLimit) {
        stack.push_back(std::move(value));
      } else {
        stack.clear();
      }
      continue;
    }
    if (mnemonic == "pop") {
      if (stack.empty()) {
        stack.clear();
      } else {
        stack.pop_back();
      }
      continue;
    }
    if (mnemonic == "dup") {
      if (stack.empty() || stack.size() >= kSimulatedStackLimit) {
        stack.clear();
      } else {
        stack.push_back(stack.back());
      }
      continue;
    }
    if (mnemonic == "swap") {
      if (stack.size() < 2) {
        stack.clear();
      } else {
        std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
      }
      continue;
    }
    const std::int32_t delta = stack_delta(mnemonic);
    if (delta == kUnknownStackDelta) {
      stack.clear();
      continue;
    }
    if (delta > 0) {
      if (stack.size() < kSimulatedStackLimit) {
        stack.push_back(StackValue{});
      } else {
        stack.clear();
      }
      continue;
    }
    if (delta < 0) {
      if (!stack.empty()) {
        stack.pop_back();
      }
      continue;
    }
    if (!stack.empty()) {
      stack.back() = StackValue{};
    }
  }
}

std::string member_name(const std::string& multiname) {
  const std::size_t separator = multiname.rfind("::");
  return separator == std::string::npos ? multiname : multiname.substr(separator + 2);
}

std::vector<std::string> collect_literals(const std::vector<AbcCallArgument>& arguments) {
  std::vector<std::string> literals;
  for (const AbcCallArgument& argument : arguments) {
    if (argument.kind == AbcArgumentKind::Literal) {
      literals.push_back(argument.value);
    }
  }
  return literals;
}

void fill_event_slots(AbcCallSite& site) {
  const std::string member = member_name(site.target);
  if (member != "addEventListener" && member != "removeEventListener") {
    return;
  }
  if (!site.arguments.empty()) {
    site.event_name = site.arguments[0].value;
  }
  if (site.arguments.size() > 1) {
    site.handler = site.arguments[1].value;
  }
}

bool build_call_site(const AbcFile& file, const std::vector<AbcInstruction>& code,
                     std::size_t call_index, const CallFrame& frame, AbcCallSite& out) {
  const AbcInstruction& instruction = code[call_index];
  const std::string_view mnemonic{instruction.mnemonic};
  if (!is_call_like(mnemonic)) {
    return false;
  }
  const AbcDecodedOperand* name_operand = nullptr;
  for (const AbcDecodedOperand& operand : instruction.operands) {
    if (operand.role == AbcOperandRole::Multiname && name_operand == nullptr) {
      name_operand = &operand;
    }
  }
  if (name_operand == nullptr) {
    return false;
  }
  out.offset = instruction.offset;
  out.opcode = std::string{mnemonic};
  out.target = file.multiname_at(name_operand->value);
  out.arg_count = static_cast<std::uint32_t>(argument_count(instruction));
  out.receiver = frame.receiver;
  out.arguments.assign(argument_slots(instruction), AbcCallArgument{});
  for (std::size_t slot = 0; slot < out.arguments.size() && slot < frame.arguments.size(); ++slot) {
    out.arguments[slot] = frame.arguments[slot];
  }
  out.literals = collect_literals(out.arguments);
  out.literal_unambiguous = out.literals.size() == 1;
  fill_event_slots(out);
  return true;
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
  std::vector<CallFrame> frames;
  simulate_arguments(file, code, frames);
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
    if (build_call_site(file, code, i, frames[i], site)) {
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
