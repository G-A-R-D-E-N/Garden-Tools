#include <fo4/abc/abc_text.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_text_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc::detail::text {

namespace {

/* Invariant: only an instruction start or the code size is a labelable offset. */
std::vector<std::size_t> code_boundaries(const std::vector<AbcInstruction>& instructions,
                                         std::size_t code_size) {
  std::vector<std::size_t> out;
  out.reserve(instructions.size() + 1);
  for (const AbcInstruction& instruction : instructions) {
    out.push_back(instruction.offset);
  }
  out.push_back(code_size);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void collect_label_offsets(const std::vector<AbcInstruction>& instructions,
                           const std::vector<AbcException>& exceptions,
                           const std::vector<std::size_t>& boundaries,
                           std::vector<std::size_t>& out) {
  out.clear();
  const auto take = [&boundaries, &out](std::int64_t target) {
    if (target < 0) {
      return;
    }
    const auto offset = static_cast<std::size_t>(target);
    if (std::binary_search(boundaries.begin(), boundaries.end(), offset)) {
      out.push_back(offset);
    }
  };
  for (const AbcInstruction& instruction : instructions) {
    for (const AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Offset) {
        take(operand.target);
      } else if (operand.role == AbcOperandRole::Switch) {
        take(operand.target);
        for (const std::int32_t target : operand.case_targets) {
          take(target);
        }
      }
    }
  }
  for (const AbcException& exception : exceptions) {
    take(exception.from);
    take(exception.to);
    take(exception.target);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

std::string target_ref(const std::map<std::size_t, std::string>& labels, std::int64_t offset) {
  if (offset >= 0) {
    const auto found = labels.find(static_cast<std::size_t>(offset));
    if (found != labels.end()) {
      return found->second;
    }
  }
  return "=" + std::to_string(offset);
}

std::string render_operand(const AbcFile& file, const AbcDecodedOperand& operand,
                           const std::map<std::size_t, std::string>& labels) {
  switch (operand.role) {
    case AbcOperandRole::None:
      return {};
    case AbcOperandRole::Multiname:
      return multiname_ref(file, operand.value);
    case AbcOperandRole::String:
      return string_ref(file, operand.value);
    case AbcOperandRole::Namespace:
      return namespace_ref(file, operand.value);
    case AbcOperandRole::Method:
      return method_ref(file, operand.value);
    case AbcOperandRole::Class:
      return class_ref(file, operand.value);
    case AbcOperandRole::Int:
      return int_ref(file, operand.value);
    case AbcOperandRole::UInt:
      return uint_ref(file, operand.value);
    case AbcOperandRole::Double:
      return double_ref(file, operand.value);
    case AbcOperandRole::Count:
      return std::to_string(operand.value);
    case AbcOperandRole::Immediate:
      if (operand.value > 0xFFFFu) {
        return std::to_string(operand.value);
      }
      return std::to_string(operand.immediate);
    case AbcOperandRole::Offset:
      return "-> " + target_ref(labels, operand.target);
    case AbcOperandRole::Switch: {
      std::string out = "switch " + target_ref(labels, operand.target);
      for (const std::int32_t target : operand.case_targets) {
        out += " " + target_ref(labels, target);
      }
      return out;
    }
  }
  return {};
}

}  // namespace

std::string render_body(const AbcFile& file, const AbcMethodBody& body, std::size_t index) {
  std::string out;
  out += "  body " + std::to_string(index) + " method " + method_ref(file, body.method);
  out += " maxstack " + std::to_string(body.max_stack);
  out += " local_count " + std::to_string(body.local_count);
  out += " init_scope " + std::to_string(body.init_scope_depth);
  out += " max_scope " + std::to_string(body.max_scope_depth);
  out += " code_bytes " + std::to_string(body.code.size()) + "\n";

  std::vector<AbcInstruction> instructions;
  const AbcStatus status = decode_code(body.code, instructions, body.exceptions);
  const std::vector<std::size_t> boundaries = code_boundaries(instructions, body.code.size());
  std::vector<std::size_t> offsets;
  collect_label_offsets(instructions, body.exceptions, boundaries, offsets);
  std::map<std::size_t, std::string> labels;
  for (std::size_t label_index = 0; label_index < offsets.size(); ++label_index) {
    labels[offsets[label_index]] = "L" + std::to_string(label_index);
  }
  if (status != AbcStatus::Ok) {
    out += "    ; decode failed: " + std::string{status_name(status)} + "\n";
    out += "    ; raw";
    for (const std::uint8_t byte : body.code) {
      out += " " + std::to_string(static_cast<unsigned>(byte));
    }
    out += "\n";
  } else {
    out += "    code\n";
    for (const AbcInstruction& instruction : instructions) {
      const auto label = labels.find(instruction.offset);
      if (label != labels.end()) {
        out += "      " + label->second + ":\n";
      }
      out += "      " + std::to_string(instruction.offset) + " " + instruction.mnemonic;
      for (const AbcDecodedOperand& operand : instruction.operands) {
        out += " " + render_operand(file, operand, labels);
      }
      out += "\n";
    }
    const auto end_label = labels.find(body.code.size());
    if (end_label != labels.end()) {
      out += "      " + end_label->second + ":\n";
    }
    out += "    end_code\n";
  }

  out += "    exceptions " + std::to_string(body.exceptions.size()) + "\n";
  for (const AbcException& exception : body.exceptions) {
    out += "      exception " + target_ref(labels, exception.from) + " " +
           target_ref(labels, exception.to) + " " + target_ref(labels, exception.target);
    out += " type " + multiname_ref(file, exception.exc_type);
    out += " var " + string_ref(file, exception.var_name) + "\n";
  }

  out += "    traits " + std::to_string(body.traits.size()) + "\n";
  out += render_traits(file, body.traits);
  return out;
}

}  // namespace fo4::abc::detail::text
