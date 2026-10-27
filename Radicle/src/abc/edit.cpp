#include <fo4/abc/edit.hpp>

#include <fo4/abc/flow.hpp>
#include <fo4/abc/opcodes.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fo4::abc {

namespace {

constexpr std::uint32_t kVerifierStackCeiling = 4096;

bool is_offset_operand(const AbcOperandSpec& spec) {
  return spec.role == AbcOperandRole::Offset || spec.role == AbcOperandRole::Switch;
}

// Constraint: a branch target must keep naming an instruction start, so a shift is applied only at or after it.
void shift_targets(std::vector<AbcInstruction>& code, std::size_t from_index, std::uint32_t offset,
                   std::uint32_t delta, bool is_insert) {
  for (std::size_t index = 0; index < code.size(); ++index) {
    if (index == from_index) {
      continue;
    }
    for (AbcDecodedOperand& operand : code[index].operands) {
      if (operand.role != AbcOperandRole::Offset && operand.role != AbcOperandRole::Switch) {
        continue;
      }
      if (operand.role == AbcOperandRole::Offset) {
        if (is_insert ? operand.target >= static_cast<std::int32_t>(offset)
                      : operand.target >= static_cast<std::int32_t>(offset + delta)) {
          operand.target += is_insert ? static_cast<std::int32_t>(delta)
                                      : -static_cast<std::int32_t>(delta);
        }
      } else {
        for (std::int32_t& target : operand.case_targets) {
          if (is_insert ? target >= static_cast<std::int32_t>(offset)
                        : target >= static_cast<std::int32_t>(offset + delta)) {
            target += is_insert ? static_cast<std::int32_t>(delta)
                                : -static_cast<std::int32_t>(delta);
          }
        }
        if (is_insert ? operand.target >= static_cast<std::int32_t>(offset)
                      : operand.target >= static_cast<std::int32_t>(offset + delta)) {
          operand.target += is_insert ? static_cast<std::int32_t>(delta)
                                      : -static_cast<std::int32_t>(delta);
        }
      }
    }
  }
}

bool target_deleted(std::int32_t target, std::uint32_t offset, std::uint32_t delta) {
  return target >= static_cast<std::int32_t>(offset) &&
         target < static_cast<std::int32_t>(offset + delta);
}

void recompute_offsets(std::vector<AbcInstruction>& code) {
  std::size_t running = 0;
  for (AbcInstruction& instruction : code) {
    instruction.offset = running;
    running += instruction.length;
  }
}

// Constraint: a local slot above the declared count would make the verifier refuse, so the count must rise.
std::uint32_t required_local_count(const std::vector<AbcInstruction>& code) {
  std::uint32_t needed = 0;
  for (const AbcInstruction& instruction : code) {
    const AbcOpcodeInfo* info = opcode_info(instruction.opcode);
    if (info != nullptr && info->mnemonic != nullptr && std::string(info->mnemonic) == "hasnext2" &&
        instruction.operands.size() == 2) {
      needed = std::max({needed, instruction.operands[0].value + 1, instruction.operands[1].value + 1});
      continue;
    }
    if (info == nullptr || info->operand_count != 1 || instruction.operands.size() != 1) {
      continue;
    }
    const std::string mnemonic = info->mnemonic == nullptr ? std::string() : info->mnemonic;
    const bool names_local = mnemonic == "getlocal" || mnemonic == "setlocal" ||
                             mnemonic == "inclocal" || mnemonic == "declocal" ||
                             mnemonic == "inclocal_i" || mnemonic == "declocal_i" ||
                             mnemonic == "kill";
    if (!names_local) {
      continue;
    }
    needed = std::max(needed, instruction.operands[0].value + 1);
  }
  return needed;
}

// Constraint: a refusal must restore the body it was given, so every check runs against a copy first.
AbcEditStatus verify_and_commit(AbcFile& file, std::uint32_t body_index,
                                const AbcMethodBody& original, std::string& message) {
  AbcMethodBody& body = file.bodies[body_index];
  std::vector<AbcInstruction> code;
  if (decode_code(body.code, code, body.exceptions) != AbcStatus::Ok) {
    body = original;
    message = "the edited code does not decode";
    return AbcEditStatus::DecodeFailed;
  }
  body.local_count = std::max(original.local_count, required_local_count(code));
  const std::uint32_t original_max_stack = body.max_stack;
  body.max_stack = std::max(original.max_stack, kVerifierStackCeiling);
  AbcMethodFlow flow;
  AbcFlowIssue issue;
  const AbcFlowStatus status = verify_method_flow(file, body_index, flow, issue);
  if (status != AbcFlowStatus::Ok || !flow.verified) {
    body = original;
    message = std::string("the verifier refused the edit: ") + flow_status_name(status);
    if (!issue.message.empty()) {
      message += ": ";
      message += issue.message;
    }
    return AbcEditStatus::VerifierRefused;
  }
  body.max_stack = std::max(original_max_stack, flow.observed_max_stack);
  return AbcEditStatus::Ok;
}



}  // namespace

const char* edit_status_name(AbcEditStatus status) noexcept {
  switch (status) {
    case AbcEditStatus::Ok:
      return "ok";
    case AbcEditStatus::NoSuchBody:
      return "no such method body";
    case AbcEditStatus::DecodeFailed:
      return "the method body code does not decode";
    case AbcEditStatus::NotAnInstructionBoundary:
      return "the offset is not an instruction boundary";
    case AbcEditStatus::BadInsertedInstruction:
      return "the inserted bytes are not exactly one instruction";
    case AbcEditStatus::InsertedInstructionBranches:
      return "an inserted instruction must not carry its own branch or switch";
    case AbcEditStatus::BranchTargetsDeletedInstruction:
      return "a branch or exception targets the deleted instruction";
    case AbcEditStatus::ExceptionRangeAffected:
      return "an exception range begins or ends inside the deleted instruction";
    case AbcEditStatus::EncodeFailed:
      return "the edited code does not encode";
    case AbcEditStatus::VerifierRefused:
      return "the verifier refused the edit";
  }
  return "unknown edit status";
}

AbcEditStatus insert_instruction(AbcFile& file, std::uint32_t body_index, std::size_t offset,
                                 std::span<const std::uint8_t> instruction, std::string& message) {
  message.clear();
  if (body_index >= file.bodies.size()) {
    message = "no such method body";
    return AbcEditStatus::NoSuchBody;
  }
  AbcMethodBody& body = file.bodies[body_index];
  const AbcMethodBody original = body;
  std::vector<AbcInstruction> code;
  if (decode_code(body.code, code, body.exceptions) != AbcStatus::Ok) {
    message = "the method body code does not decode";
    return AbcEditStatus::DecodeFailed;
  }
  std::size_t index = code.size();
  bool boundary = offset == body.code.size();
  for (std::size_t candidate = 0; candidate < code.size(); ++candidate) {
    if (code[candidate].offset == offset) {
      index = candidate;
      boundary = true;
      break;
    }
  }
  if (!boundary) {
    message = "the offset is not an instruction boundary";
    return AbcEditStatus::NotAnInstructionBoundary;
  }

  std::vector<AbcInstruction> inserted;
  if (instruction.empty() ||
      decode_code(instruction.data(), instruction.size(), inserted) != AbcStatus::Ok ||
      inserted.size() != 1 || inserted[0].length != instruction.size()) {
    message = "the inserted bytes must be exactly one decodable instruction";
    return AbcEditStatus::BadInsertedInstruction;
  }
  const AbcOpcodeInfo* info = opcode_info(inserted[0].opcode);
  if (info == nullptr) {
    message = "the inserted opcode is not modelled";
    return AbcEditStatus::BadInsertedInstruction;
  }
  for (std::size_t operand = 0; operand < info->operand_count; ++operand) {
    if (is_offset_operand(info->operands[operand])) {
      message = "an inserted instruction must not carry its own branch or switch";
      return AbcEditStatus::InsertedInstructionBranches;
    }
  }

  const std::uint32_t delta = static_cast<std::uint32_t>(inserted[0].length);
  std::vector<AbcInstruction> next = code;
  next.insert(next.begin() + static_cast<std::ptrdiff_t>(index), inserted[0]);
  shift_targets(next, index, static_cast<std::uint32_t>(offset), delta, true);
  recompute_offsets(next);
  std::vector<std::uint8_t> encoded;
  if (encode_code(next, encoded) != AbcStatus::Ok) {
    message = "the edited code does not encode";
    return AbcEditStatus::EncodeFailed;
  }
  body.code = std::move(encoded);
  for (AbcException& exception : body.exceptions) {
    if (exception.from >= offset) {
      exception.from += delta;
    }
    if (exception.to >= offset) {
      exception.to += delta;
    }
    if (exception.target >= offset) {
      exception.target += delta;
    }
  }
  return verify_and_commit(file, body_index, original, message);
}

AbcEditStatus delete_instruction(AbcFile& file, std::uint32_t body_index, std::size_t offset,
                                 std::string& message) {
  message.clear();
  if (body_index >= file.bodies.size()) {
    message = "no such method body";
    return AbcEditStatus::NoSuchBody;
  }
  AbcMethodBody& body = file.bodies[body_index];
  const AbcMethodBody original = body;
  std::vector<AbcInstruction> code;
  if (decode_code(body.code, code, body.exceptions) != AbcStatus::Ok) {
    message = "the method body code does not decode";
    return AbcEditStatus::DecodeFailed;
  }
  std::size_t index = code.size();
  for (std::size_t candidate = 0; candidate < code.size(); ++candidate) {
    if (code[candidate].offset == offset) {
      index = candidate;
      break;
    }
  }
  if (index == code.size()) {
    message = "the offset is not an instruction boundary";
    return AbcEditStatus::NotAnInstructionBoundary;
  }
  const std::uint32_t delta = static_cast<std::uint32_t>(code[index].length);
  for (const AbcInstruction& instruction : code) {
    for (const AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Offset && target_deleted(operand.target, offset, delta)) {
        message = "a branch targets the deleted instruction";
        return AbcEditStatus::BranchTargetsDeletedInstruction;
      }
      if (operand.role == AbcOperandRole::Switch) {
        if (target_deleted(operand.target, offset, delta)) {
          message = "a switch targets the deleted instruction";
          return AbcEditStatus::BranchTargetsDeletedInstruction;
        }
        for (const std::int32_t target : operand.case_targets) {
          if (target_deleted(target, offset, delta)) {
            message = "a switch case targets the deleted instruction";
            return AbcEditStatus::BranchTargetsDeletedInstruction;
          }
        }
      }
    }
  }
  for (const AbcException& exception : original.exceptions) {
    if (exception.target >= offset && exception.target < offset + delta) {
      message = "an exception target names the deleted instruction";
      return AbcEditStatus::BranchTargetsDeletedInstruction;
    }
    const auto inside = [&](std::uint32_t value) {
      return value > offset && value < offset + delta;
    };
    if (inside(exception.from) || inside(exception.to)) {
      message = "an exception range begins or ends inside the deleted instruction";
      return AbcEditStatus::ExceptionRangeAffected;
    }
  }

  std::vector<AbcInstruction> next = code;
  next.erase(next.begin() + static_cast<std::ptrdiff_t>(index));
  shift_targets(next, next.size(), static_cast<std::uint32_t>(offset), delta, false);
  recompute_offsets(next);
  std::vector<std::uint8_t> encoded;
  if (encode_code(next, encoded) != AbcStatus::Ok) {
    message = "the edited code does not encode";
    return AbcEditStatus::EncodeFailed;
  }
  body.code = std::move(encoded);
  for (AbcException& exception : body.exceptions) {
    if (exception.from >= offset + delta) {
      exception.from -= delta;
    }
    if (exception.to >= offset + delta) {
      exception.to -= delta;
    }
    if (exception.target >= offset + delta) {
      exception.target -= delta;
    }
  }
  return verify_and_commit(file, body_index, original, message);
}

}  // namespace fo4::abc
