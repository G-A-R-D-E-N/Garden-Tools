#include <fo4/abc/edit.hpp>

#include <fo4/abc/flow.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_internal.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace fo4::abc {

namespace {

bool is_offset_operand(const AbcOperandSpec& spec) {
  return spec.role == AbcOperandRole::Offset || spec.role == AbcOperandRole::Switch;
}

// Constraint: a branch target must keep naming an instruction start, so a shift is applied only at or after it.
void shift_code_targets(std::vector<AbcInstruction>& code, std::uint32_t boundary,
                        std::int32_t delta) {
  const auto moved = [boundary](std::int32_t target) {
    return target >= static_cast<std::int32_t>(boundary);
  };
  for (AbcInstruction& instruction : code) {
    for (AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Offset) {
        if (moved(operand.target)) {
          operand.target += delta;
        }
      } else if (operand.role == AbcOperandRole::Switch) {
        for (std::int32_t& target : operand.case_targets) {
          if (moved(target)) {
            target += delta;
          }
        }
        if (moved(operand.target)) {
          operand.target += delta;
        }
      }
    }
  }
}

void shift_exceptions(std::vector<AbcException>& exceptions, std::uint32_t boundary,
                      std::int32_t delta) {
  const auto moved = [boundary, delta](std::uint32_t& value) {
    if (value >= boundary) {
      value = static_cast<std::uint32_t>(static_cast<std::int64_t>(value) + delta);
    }
  };
  for (AbcException& exception : exceptions) {
    moved(exception.from);
    moved(exception.to);
    moved(exception.target);
  }
}

bool target_deleted(std::int32_t target, std::uint32_t offset, std::uint32_t delta) {
  return target >= static_cast<std::int32_t>(offset) &&
         target < static_cast<std::int32_t>(offset + delta);
}

std::size_t index_at(const std::vector<AbcInstruction>& code, std::size_t offset) {
  for (std::size_t candidate = 0; candidate < code.size(); ++candidate) {
    if (code[candidate].offset == offset) {
      return candidate;
    }
  }
  return code.size();
}

void recompute_offsets(std::vector<AbcInstruction>& code) {
  std::size_t running = 0;
  for (AbcInstruction& instruction : code) {
    instruction.offset = running;
    running += instruction.length;
  }
}

}  // namespace

namespace detail {

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

}  // namespace detail

namespace {

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
  body.local_count = std::max(original.local_count, detail::required_local_count(code));
  const std::uint32_t original_max_stack = body.max_stack;
  body.max_stack = std::max(original.max_stack, detail::kVerifierStackCeiling);
  body.max_scope_depth = std::max(original.max_scope_depth, detail::kVerifierStackCeiling);
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
  body.max_scope_depth = std::max(original.max_scope_depth, flow.observed_max_scope_depth);
  return AbcEditStatus::Ok;
}

AbcEditStatus decode_inserted(std::span<const std::uint8_t> instruction, AbcInstruction& out,
                              std::string& message) {
  std::vector<AbcInstruction> decoded;
  if (instruction.empty() ||
      decode_code(instruction.data(), instruction.size(), decoded) != AbcStatus::Ok ||
      decoded.size() != 1 || decoded[0].length != instruction.size()) {
    message = "the inserted bytes must be exactly one decodable instruction";
    return AbcEditStatus::BadInsertedInstruction;
  }
  const AbcOpcodeInfo* info = opcode_info(decoded[0].opcode);
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
  out = decoded[0];
  return AbcEditStatus::Ok;
}

// Invariant: index 0 is the implicit default entry, so an empty table must gain it before the first append.
template <typename T, typename Same = std::equal_to<>>
std::uint32_t intern_entry(std::vector<T>& entries, const T& value, Same same = Same{}) {
  if (entries.empty()) {
    entries.emplace_back();
  }
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (same(entries[index], value)) {
      return static_cast<std::uint32_t>(index);
    }
  }
  entries.push_back(value);
  return static_cast<std::uint32_t>(entries.size() - 1);
}

bool same_double(double a, double b) {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_namespace(const AbcNamespace& a, const AbcNamespace& b) {
  return a.kind == b.kind && a.name == b.name;
}

bool same_multiname(const AbcMultiname& a, const AbcMultiname& b) {
  return a.kind == b.kind && a.name == b.name && a.ns == b.ns && a.ns_set == b.ns_set &&
         a.params == b.params;
}

bool name_fits(std::uint32_t index, std::size_t limit) { return index == 0 || index < limit; }

bool multiname_fits(const AbcConstantPool& pool, const AbcMultiname& value) {
  const std::size_t multinames = pool.multinames.size();
  switch (value.kind) {
    case AbcMultinameKind::QName:
    case AbcMultinameKind::QNameA:
      return value.ns < pool.namespaces.size() && name_fits(value.name, pool.strings.size());
    case AbcMultinameKind::RTQName:
    case AbcMultinameKind::RTQNameA:
      return name_fits(value.name, pool.strings.size());
    case AbcMultinameKind::RTQNameL:
    case AbcMultinameKind::RTQNameLA:
      return true;
    case AbcMultinameKind::Multiname:
    case AbcMultinameKind::MultinameA:
      return value.ns_set < pool.ns_sets.size() && name_fits(value.name, pool.strings.size());
    case AbcMultinameKind::MultinameL:
    case AbcMultinameKind::MultinameLA:
      return value.ns_set < pool.ns_sets.size();
    case AbcMultinameKind::TypeName:
      return value.name < multinames &&
             std::all_of(value.params.begin(), value.params.end(),
                         [multinames](std::uint32_t param) { return param < multinames; });
  }
  return false;
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
    case AbcEditStatus::PoolEntryOutOfRange:
      return "a pool entry names an index outside its pool";
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
  const std::size_t index = index_at(code, offset);
  if (index == code.size() && offset != body.code.size()) {
    message = "the offset is not an instruction boundary";
    return AbcEditStatus::NotAnInstructionBoundary;
  }

  AbcInstruction inserted;
  if (const AbcEditStatus status = decode_inserted(instruction, inserted, message);
      status != AbcEditStatus::Ok) {
    return status;
  }
  const auto delta = static_cast<std::int32_t>(inserted.length);
  std::vector<AbcInstruction> next = code;
  next.insert(next.begin() + static_cast<std::ptrdiff_t>(index), inserted);
  shift_code_targets(next, static_cast<std::uint32_t>(offset), delta);
  recompute_offsets(next);
  std::vector<std::uint8_t> encoded;
  if (encode_code(next, encoded) != AbcStatus::Ok) {
    message = "the edited code does not encode";
    return AbcEditStatus::EncodeFailed;
  }
  body.code = std::move(encoded);
  shift_exceptions(body.exceptions, static_cast<std::uint32_t>(offset), delta);
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
  const std::size_t index = index_at(code, offset);
  if (index == code.size()) {
    message = "the offset is not an instruction boundary";
    return AbcEditStatus::NotAnInstructionBoundary;
  }
  const std::uint32_t delta = static_cast<std::uint32_t>(code[index].length);
  const auto start = static_cast<std::uint32_t>(offset);
  for (const AbcInstruction& instruction : code) {
    for (const AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Offset && target_deleted(operand.target, start, delta)) {
        message = "a branch targets the deleted instruction";
        return AbcEditStatus::BranchTargetsDeletedInstruction;
      }
      if (operand.role == AbcOperandRole::Switch) {
        if (target_deleted(operand.target, start, delta)) {
          message = "a switch targets the deleted instruction";
          return AbcEditStatus::BranchTargetsDeletedInstruction;
        }
        for (const std::int32_t target : operand.case_targets) {
          if (target_deleted(target, start, delta)) {
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
  const auto boundary = static_cast<std::uint32_t>(offset + delta);
  const auto shift = -static_cast<std::int32_t>(delta);
  shift_code_targets(next, boundary, shift);
  recompute_offsets(next);
  std::vector<std::uint8_t> encoded;
  if (encode_code(next, encoded) != AbcStatus::Ok) {
    message = "the edited code does not encode";
    return AbcEditStatus::EncodeFailed;
  }
  body.code = std::move(encoded);
  shift_exceptions(body.exceptions, boundary, shift);
  return verify_and_commit(file, body_index, original, message);
}

AbcEditStatus replace_instruction(AbcFile& file, std::uint32_t body_index, std::size_t offset,
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
  const std::size_t index = index_at(code, offset);
  if (index == code.size()) {
    message = "the offset is not an instruction boundary";
    return AbcEditStatus::NotAnInstructionBoundary;
  }

  AbcInstruction replacement;
  if (const AbcEditStatus status = decode_inserted(instruction, replacement, message);
      status != AbcEditStatus::Ok) {
    return status;
  }
  const auto old_length = static_cast<std::uint32_t>(code[index].length);
  const auto boundary = static_cast<std::uint32_t>(offset) + old_length;
  const auto new_length = static_cast<std::int32_t>(replacement.length);
  const auto delta = new_length - static_cast<std::int32_t>(old_length);
  std::vector<AbcInstruction> next = code;
  next[index] = replacement;
  shift_code_targets(next, boundary, delta);
  recompute_offsets(next);
  std::vector<std::uint8_t> encoded;
  if (encode_code(next, encoded) != AbcStatus::Ok) {
    message = "the edited code does not encode";
    return AbcEditStatus::EncodeFailed;
  }
  body.code = std::move(encoded);
  shift_exceptions(body.exceptions, boundary, delta);
  return verify_and_commit(file, body_index, original, message);
}

AbcEditStatus refresh_body_limits(AbcFile& file, std::uint32_t body_index, std::string& message) {
  message.clear();
  if (body_index >= file.bodies.size()) {
    message = "no such method body";
    return AbcEditStatus::NoSuchBody;
  }
  const AbcMethodBody original = file.bodies[body_index];
  return verify_and_commit(file, body_index, original, message);
}

std::uint32_t intern_int(AbcFile& file, std::int32_t value) {
  return intern_entry(file.pool.ints, value);
}

std::uint32_t intern_uint(AbcFile& file, std::uint32_t value) {
  return intern_entry(file.pool.uints, value);
}

std::uint32_t intern_double(AbcFile& file, double value) {
  return intern_entry(file.pool.doubles, value, same_double);
}

std::uint32_t intern_string(AbcFile& file, const std::string& value) {
  return intern_entry(file.pool.strings, value);
}

AbcEditStatus intern_namespace(AbcFile& file, const AbcNamespace& value, std::uint32_t& index,
                               std::string& message) {
  message.clear();
  index = 0;
  if (!name_fits(value.name, file.pool.strings.size())) {
    message = "the namespace names a string outside its pool";
    return AbcEditStatus::PoolEntryOutOfRange;
  }
  index = intern_entry(file.pool.namespaces, value, same_namespace);
  return AbcEditStatus::Ok;
}

AbcEditStatus intern_multiname(AbcFile& file, const AbcMultiname& value, std::uint32_t& index,
                               std::string& message) {
  message.clear();
  index = 0;
  if (!multiname_fits(file.pool, value)) {
    message = "the multiname names an index outside its pool";
    return AbcEditStatus::PoolEntryOutOfRange;
  }
  index = intern_entry(file.pool.multinames, value, same_multiname);
  return AbcEditStatus::Ok;
}

AbcEditStatus replace_body_code(AbcFile& file, std::uint32_t body_index,
                                std::span<const std::uint8_t> code, std::string& message) {
  message.clear();
  if (body_index >= file.bodies.size()) {
    message = "no such method body";
    return AbcEditStatus::NoSuchBody;
  }
  AbcMethodBody& body = file.bodies[body_index];
  const AbcMethodBody original = body;
  body.code.assign(code.begin(), code.end());
  return verify_and_commit(file, body_index, original, message);
}

AbcEditStatus add_method(AbcFile& file, const AbcMethodInfo& method,
                         std::span<const std::uint8_t> code, std::uint32_t& method_index,
                         std::string& message) {
  message.clear();
  method_index = 0;
  AbcMethodBody body;
  body.method = static_cast<std::uint32_t>(file.methods.size());
  body.local_count =
      1 + method.param_count + ((method.needs_arguments() || method.needs_rest()) ? 1u : 0u);
  body.code.assign(code.begin(), code.end());
  file.methods.push_back(method);
  file.bodies.push_back(body);
  const auto body_index = static_cast<std::uint32_t>(file.bodies.size() - 1);
  AbcEditStatus status = verify_and_commit(file, body_index, body, message);
  if (status == AbcEditStatus::Ok) {
    std::vector<std::uint8_t> encoded;
    const AbcStatus written = file.serialize(encoded);
    if (written != AbcStatus::Ok) {
      message = std::string("the new method does not encode: ") + status_name(written);
      status = AbcEditStatus::EncodeFailed;
    }
  }
  if (status != AbcEditStatus::Ok) {
    file.bodies.pop_back();
    file.methods.pop_back();
    return status;
  }
  method_index = body.method;
  return AbcEditStatus::Ok;
}

}  // namespace fo4::abc
