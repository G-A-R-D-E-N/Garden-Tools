#include <fo4/abc/flow.hpp>

#include "abc_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fo4::abc {

namespace {

using detail::mnemonic_of;
using detail::set_issue;
using detail::u30_operand;

bool is_attribute_multiname_kind(AbcMultinameKind kind) {
  switch (kind) {
    case AbcMultinameKind::QNameA:
    case AbcMultinameKind::MultinameA:
    case AbcMultinameKind::RTQNameA:
    case AbcMultinameKind::RTQNameLA:
    case AbcMultinameKind::MultinameLA:
      return true;
    default:
      return false;
  }
}

AbcFlowStatus verify_method_context(const AbcFile& file, const AbcMethodBody& body,
                                    const AbcMethodFlow& flow, AbcFlowIssue& issue) {
  if (body.code.empty()) {
    return set_issue(issue, AbcFlowStatus::EmptyCode, kNoFlowIndex,
                     "method body contains no code");
  }
  if (static_cast<std::size_t>(body.method) >= file.methods.size()) {
    return set_issue(issue, AbcFlowStatus::MethodIndexOutOfRange, kNoFlowIndex,
                     "body names method " + std::to_string(body.method) + " outside " +
                         std::to_string(file.methods.size()) + " method(s)");
  }
  const AbcMethodInfo& method = file.methods[body.method];
  const std::uint64_t variadic = (method.needs_arguments() || method.needs_rest()) ? 1 : 0;
  const std::uint64_t parameter_locals = 1 + static_cast<std::uint64_t>(method.param_count);
  if (body.local_count < parameter_locals) {
    return set_issue(issue, AbcFlowStatus::LocalCountTooSmall, kNoFlowIndex,
                     "declares " + std::to_string(body.local_count) +
                         " local(s) but needs at least " + std::to_string(parameter_locals) +
                         " for this and " + std::to_string(method.param_count) + " parameter(s)");
  }
  if (body.local_count < parameter_locals + variadic) {
    return set_issue(issue, AbcFlowStatus::LocalCountTooSmall, kNoFlowIndex,
                     "declares " + std::to_string(body.local_count) +
                         " local(s) but needs one more for the arguments/rest register");
  }
  for (const AbcBasicBlock& block : flow.graph.blocks) {
    if (!block.reachable) {
      continue;
    }
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      if (instruction.opcode == 0x57 && !method.needs_activation()) {
        return set_issue(issue, AbcFlowStatus::MethodFlagMissing,
                         static_cast<std::uint32_t>(instruction.offset),
                         "newactivation needs the method NEED_ACTIVATION flag");
      }
      if ((instruction.opcode == 0x06 || instruction.opcode == 0x07) && !method.sets_dxns()) {
        return set_issue(issue, AbcFlowStatus::MethodFlagMissing,
                         static_cast<std::uint32_t>(instruction.offset),
                         std::string{mnemonic_of(instruction)} +
                             " needs the method SET_DXNS flag");
      }
    }
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_exception_metadata(const AbcFile& file, const AbcMethodBody& body,
                                        AbcFlowIssue& issue) {
  for (std::size_t index = 0; index < body.exceptions.size(); ++index) {
    const AbcException& exception = body.exceptions[index];
    if (exception.exc_type != 0) {
      const std::uint32_t runtime = multiname_runtime_pops(file, exception.exc_type);
      if (runtime == kNoFlowIndex) {
        return set_issue(issue, AbcFlowStatus::MultinameIndexOutOfRange, kNoFlowIndex,
                         "exception " + std::to_string(index) + " catch type multiname " +
                             std::to_string(exception.exc_type) + " is outside the pool");
      }
      if (runtime != 0) {
        return set_issue(issue, AbcFlowStatus::ExceptionTypeUnresolved, kNoFlowIndex,
                         "exception " + std::to_string(index) + " catch type multiname " +
                             std::to_string(exception.exc_type) + " is runtime resolved");
      }
    }
    if (exception.var_name == 0) {
      continue;
    }
    if (static_cast<std::size_t>(exception.var_name) >= file.pool.multinames.size()) {
      return set_issue(issue, AbcFlowStatus::MultinameIndexOutOfRange, kNoFlowIndex,
                       "exception " + std::to_string(index) + " catch variable multiname " +
                           std::to_string(exception.var_name) + " is outside the pool");
    }
    if (multiname_runtime_pops(file, exception.var_name) != 0) {
      return set_issue(issue, AbcFlowStatus::ExceptionTypeUnresolved, kNoFlowIndex,
                       "exception " + std::to_string(index) + " catch variable multiname " +
                           std::to_string(exception.var_name) + " is runtime resolved");
    }
    if (is_attribute_multiname_kind(file.pool.multinames[exception.var_name].kind)) {
      return set_issue(issue, AbcFlowStatus::ExceptionVariableInvalid, kNoFlowIndex,
                       "exception " + std::to_string(index) +
                           " catch variable is an attribute name");
    }
    if (multiname_local_name_is_wildcard(file, exception.var_name)) {
      return set_issue(issue, AbcFlowStatus::ExceptionVariableInvalid, kNoFlowIndex,
                       "exception " + std::to_string(index) +
                           " catch variable multiname is the wildcard local name");
    }
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus require_local(const AbcMethodBody& body, const AbcInstruction& instruction,
                            std::uint32_t index, AbcFlowIssue& issue) {
  if (index >= body.local_count) {
    return set_issue(issue, AbcFlowStatus::LocalIndexOutOfRange,
                     static_cast<std::uint32_t>(instruction.offset),
                     "local register " + std::to_string(index) + " is outside " +
                         std::to_string(body.local_count) + " declared local(s)");
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_local_registers(const AbcMethodBody& body, const AbcMethodFlow& flow,
                                     AbcFlowIssue& issue) {
  for (const AbcBasicBlock& block : flow.graph.blocks) {
    if (!block.reachable) {
      continue;
    }
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      const std::uint32_t opcode = instruction.opcode;
      std::uint32_t first = 0;
      switch (opcode) {
        case 0x08:
        case 0x62:
        case 0x63:
        case 0x92:
        case 0x94:
        case 0xC2:
        case 0xC3:
          if (!u30_operand(instruction, 0, AbcOperandRole::Count, first)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} + " has no local operand");
          }
          if (const AbcFlowStatus status = require_local(body, instruction, first, issue);
              status != AbcFlowStatus::Ok) {
            return status;
          }
          break;
        case 0x32: {
          std::uint32_t second = 0;
          if (!u30_operand(instruction, 0, AbcOperandRole::Count, first) ||
              !u30_operand(instruction, 1, AbcOperandRole::Count, second)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             "hasnext2 has no register pair");
          }
          if (const AbcFlowStatus status = require_local(body, instruction, first, issue);
              status != AbcFlowStatus::Ok) {
            return status;
          }
          if (const AbcFlowStatus status = require_local(body, instruction, second, issue);
              status != AbcFlowStatus::Ok) {
            return status;
          }
          if (first == second) {
            return set_issue(issue, AbcFlowStatus::LocalIndexOutOfRange,
                             static_cast<std::uint32_t>(instruction.offset),
                             "hasnext2 uses local " + std::to_string(first) +
                                 " for both object and index registers");
          }
          break;
        }
        case 0xD0:
        case 0xD1:
        case 0xD2:
        case 0xD3:
        case 0xD4:
        case 0xD5:
        case 0xD6:
        case 0xD7:
          first = static_cast<std::uint32_t>(opcode >= 0xD4 ? opcode - 0xD4 : opcode - 0xD0);
          if (const AbcFlowStatus status = require_local(body, instruction, first, issue);
              status != AbcFlowStatus::Ok) {
            return status;
          }
          break;
        default:
          break;
      }
    }
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_scalar_pool_indices(const AbcFile& file, const AbcMethodFlow& flow,
                                         AbcFlowIssue& issue) {
  for (const AbcBasicBlock& block : flow.graph.blocks) {
    if (!block.reachable) {
      continue;
    }
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      std::uint32_t index = 0;
      std::size_t limit = 0;
      const char* kind = nullptr;
      std::size_t operand = 0;
      switch (instruction.opcode) {
        case 0x2C:
        case 0x06:
        case 0xF1:
          operand = 0;
          limit = file.pool.strings.size();
          kind = "string";
          break;
        case 0x2D:
          operand = 0;
          limit = file.pool.ints.size();
          kind = "integer";
          break;
        case 0x2E:
          operand = 0;
          limit = file.pool.uints.size();
          kind = "unsigned integer";
          break;
        case 0x2F:
          operand = 0;
          limit = file.pool.doubles.size();
          kind = "double";
          break;
        case 0x31:
          operand = 0;
          limit = file.pool.namespaces.size();
          kind = "namespace";
          break;
        case 0xEF:
          operand = 1;
          limit = file.pool.strings.size();
          kind = "string";
          break;
        default:
          continue;
      }
      if (!u30_operand(instruction, operand, AbcOperandRole::String, index) &&
          !u30_operand(instruction, operand, AbcOperandRole::Int, index) &&
          !u30_operand(instruction, operand, AbcOperandRole::UInt, index) &&
          !u30_operand(instruction, operand, AbcOperandRole::Double, index) &&
          !u30_operand(instruction, operand, AbcOperandRole::Namespace, index)) {
        return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                         static_cast<std::uint32_t>(instruction.offset),
                         std::string{mnemonic_of(instruction)} + " has no " + kind + " operand");
      }
      if (index == 0 || static_cast<std::size_t>(index) >= limit) {
        return set_issue(issue, AbcFlowStatus::PoolIndexInvalid,
                         static_cast<std::uint32_t>(instruction.offset),
                         "references " + std::string{kind} + " pool index " +
                             std::to_string(index) + " with valid indices 1.." +
                             std::to_string(limit == 0 ? 0 : limit - 1));
      }
    }
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_table_indices(const AbcFile& file, const AbcMethodFlow& flow,
                                   AbcFlowIssue& issue) {
  for (const AbcBasicBlock& block : flow.graph.blocks) {
    if (!block.reachable) {
      continue;
    }
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      std::uint32_t index = 0;
      switch (instruction.opcode) {
        case 0x40:
        case 0x44:
          if (!u30_operand(instruction, 0, AbcOperandRole::Method, index)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} + " has no method operand");
          }
          if (static_cast<std::size_t>(index) >= file.methods.size()) {
            return set_issue(issue, AbcFlowStatus::MethodIndexOutOfRange,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} + " references method " +
                                 std::to_string(index) + " of " +
                                 std::to_string(file.methods.size()));
          }
          break;
        case 0x58:
          if (!u30_operand(instruction, 0, AbcOperandRole::Class, index)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             "newclass has no class operand");
          }
          if (static_cast<std::size_t>(index) >= file.instances.size()) {
            return set_issue(issue, AbcFlowStatus::ClassIndexOutOfRange,
                             static_cast<std::uint32_t>(instruction.offset),
                             "newclass references class " + std::to_string(index) + " of " +
                                 std::to_string(file.instances.size()));
          }
          break;
        default:
          break;
      }
    }
  }
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_body_opcodes(const AbcFile& file, const AbcMethodBody& body,
                                  const AbcMethodFlow& flow, AbcFlowIssue& issue) {
  for (const AbcBasicBlock& block : flow.graph.blocks) {
    if (!block.reachable) {
      continue;
    }
    const std::size_t end = static_cast<std::size_t>(block.first_instruction) +
                            static_cast<std::size_t>(block.instruction_count);
    for (std::size_t at = block.first_instruction; at < end; ++at) {
      const AbcInstruction& instruction = flow.instructions[at];
      std::uint32_t index = 0;
      switch (instruction.opcode) {
        case 0x43:
          return set_issue(issue, AbcFlowStatus::OpcodeNotPermitted,
                           static_cast<std::uint32_t>(instruction.offset),
                           "callmethod is not permitted in user bytecode");
        case 0x5A:
          if (!u30_operand(instruction, 0, AbcOperandRole::Count, index)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             "newcatch has no exception index");
          }
          if (index >= body.exceptions.size()) {
            return set_issue(issue, AbcFlowStatus::PoolIndexInvalid,
                             static_cast<std::uint32_t>(instruction.offset),
                             "newcatch index " + std::to_string(index) + " is outside " +
                                 std::to_string(body.exceptions.size()) + " exception(s)");
          }
          break;
        case 0x5F:
        case 0x60:
        case 0x80:
        case 0x86:
        case 0xB2: {
          if (!u30_operand(instruction, 0, AbcOperandRole::Multiname, index)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             mnemonic_of(instruction));
          }
          const std::uint32_t runtime = multiname_runtime_pops(file, index);
          if (runtime == kNoFlowIndex) {
            return set_issue(issue, AbcFlowStatus::MultinameIndexOutOfRange,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} + " reads multiname " +
                                 std::to_string(index) + " outside the pool");
          }
          if (runtime != 0) {
            return set_issue(issue, AbcFlowStatus::MultinameIndexOutOfRange,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} +
                                 " needs a non-runtime multiname; pool entry " +
                                 std::to_string(index) + " needs " + std::to_string(runtime) +
                                 " runtime stack operand(s)");
          }
          break;
        }
        case 0x6C:
        case 0x6D:
        case 0x6E:
        case 0x6F:
          if (!u30_operand(instruction, 0, AbcOperandRole::Count, index)) {
            return set_issue(issue, AbcFlowStatus::UnmodelledStackEffect,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} + " has no slot operand");
          }
          if (index == 0) {
            return set_issue(issue, AbcFlowStatus::SlotIndexInvalid,
                             static_cast<std::uint32_t>(instruction.offset),
                             std::string{mnemonic_of(instruction)} +
                                 " uses invalid slot index 0");
          }
          break;
        default:
          break;
      }
    }
  }
  return AbcFlowStatus::Ok;
}

}  // namespace

AbcFlowStatus verify_method_flow(const AbcFile& file, std::uint32_t body_index, AbcMethodFlow& out,
                                 AbcFlowIssue& issue) {
  issue = {};
  out = {};
  if (static_cast<std::size_t>(body_index) >= file.bodies.size()) {
    issue.status = AbcFlowStatus::PoolIndexInvalid;
    issue.body = body_index;
    issue.message = "method body " + std::to_string(body_index) + " is outside " +
                    std::to_string(file.bodies.size()) + " body/bodies";
    return issue.status;
  }
  const AbcMethodBody& body = file.bodies[body_index];
  out.body = body_index;
  out.method = body.method;
  issue.body = body_index;
  issue.method = body.method;

  const AbcStatus decode_status = decode_code(body.code, out.instructions, body.exceptions);
  if (decode_status != AbcStatus::Ok) {
    issue.status = AbcFlowStatus::DecodeFailed;
    issue.decode = decode_status;
    issue.offset = kNoFlowIndex;
    issue.message =
        std::string{"the instruction stream did not decode: "} + status_name(decode_status);
    return issue.status;
  }
  std::vector<std::uint32_t> handler_entries;
  AbcFlowStatus status = detail::build_exception_graph(body, out.instructions, out.graph,
                                                       out.exceptions, handler_entries, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = verify_method_context(file, body, out, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = verify_exception_metadata(file, body, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = verify_local_registers(body, out, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = verify_table_indices(file, out, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = verify_scalar_pool_indices(file, out, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = verify_body_opcodes(file, body, out, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  if (body.init_scope_depth > body.max_scope_depth) {
    return set_issue(issue, AbcFlowStatus::ScopeLimitExceeded, kNoFlowIndex,
                     "init_scope_depth " + std::to_string(body.init_scope_depth) +
                         " exceeds max_scope_depth " + std::to_string(body.max_scope_depth));
  }
  status = detail::verify_stack(file, body, out, handler_entries, out.entry_stack_depth,
                                out.observed_max_stack, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  status = detail::verify_scope(body, out, handler_entries, out.entry_scope_depth,
                                out.observed_max_scope_depth, issue);
  if (status != AbcFlowStatus::Ok) {
    return status;
  }
  out.verified = true;
  return AbcFlowStatus::Ok;
}

AbcFlowStatus verify_all_method_flows(const AbcFile& file, std::vector<AbcMethodFlow>& flows,
                                      std::vector<AbcFlowIssue>& issues) {
  flows.assign(file.bodies.size(), AbcMethodFlow{});
  issues.clear();
  AbcFlowStatus worst = AbcFlowStatus::Ok;
  for (std::uint32_t index = 0; index < file.bodies.size(); ++index) {
    AbcFlowIssue issue;
    const AbcFlowStatus status = verify_method_flow(file, index, flows[index], issue);
    if (status != AbcFlowStatus::Ok) {
      issues.push_back(std::move(issue));
      if (worst == AbcFlowStatus::Ok) {
        worst = status;
      }
    }
  }
  return worst;
}

}  // namespace fo4::abc
