#include <fo4/abc/abc_asm.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_asm_internal.hpp"
#include "abc_internal.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::abc::detail::text {

namespace {

std::size_t u30_size(std::uint32_t value) {
  std::size_t size = 1;
  while (value >= 0x80u) {
    value >>= 7;
    ++size;
  }
  return size;
}

bool same_instruction(const AbcInstruction& parsed, const AbcInstruction& decoded) {
  if (parsed.opcode != decoded.opcode || parsed.operands.size() != decoded.operands.size()) {
    return false;
  }
  for (std::size_t index = 0; index < parsed.operands.size(); ++index) {
    const AbcDecodedOperand& from_text = parsed.operands[index];
    const AbcDecodedOperand& from_bytes = decoded.operands[index];
    if (from_text.immediate != from_bytes.immediate ||
        (from_text.role != AbcOperandRole::Immediate && from_text.value != from_bytes.value)) {
      return false;
    }
  }
  return true;
}

struct PendingTarget {
  std::string label;
  std::int64_t absolute{0};
  bool is_absolute{false};
};

/* An '=' token carries an absolute target offset and never a label. */
bool pending_target(std::string_view token, PendingTarget& out) {
  if (token.empty() || token.front() != '=') {
    out.label = std::string{token};
    return true;
  }
  std::int64_t absolute = 0;
  const auto parsed = std::from_chars(token.data() + 1, token.data() + token.size(), absolute);
  if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
      absolute != static_cast<std::int64_t>(static_cast<std::int32_t>(absolute))) {
    return false;
  }
  out.is_absolute = true;
  out.absolute = absolute;
  return true;
}

struct PendingBranch {
  std::size_t instruction{0};
  std::size_t operand{0};
  PendingTarget target;
};

struct PendingSwitch {
  std::size_t instruction{0};
  std::size_t operand{0};
  PendingTarget default_target;
  std::vector<PendingTarget> case_targets;
};

struct PendingException {
  PendingTarget from;
  PendingTarget to;
  PendingTarget target;
  std::uint32_t exc_type{0};
  std::uint32_t var_name{0};
};

struct BodyParser {
  AbcFile& file;
  std::string& error;
  std::size_t first_line{0};
  AbcMethodBody body;
  std::vector<AbcInstruction> instructions;
  std::vector<PendingBranch> branches;
  std::vector<PendingSwitch> switches;
  std::vector<PendingException> exceptions;
  std::map<std::string, std::size_t> labels;
  std::size_t code_size{0};
  bool in_code{false};
  bool saw_code{false};
  std::size_t expected_code_bytes{0};
  bool max_stack_given{false};
  bool local_count_given{false};

  bool fail(std::string_view message) {
    if (error.empty()) {
      std::string prefix;
      if (first_line != 0) {
        prefix = "line " + std::to_string(first_line) + ": ";
      }
      error = prefix + std::string{message};
    }
    return false;
  }

  bool parse_header(const std::vector<std::string>& tokens) {
    if (tokens.size() < 4 || tokens[0] != "body") {
      return fail("body header is malformed");
    }
    std::uint32_t index = 0;
    if (!parse_u32(tokens[1], index)) {
      return fail("body index is malformed");
    }
    std::size_t token = 2;
    const auto keyword_u32 = [&](std::string_view keyword, std::uint32_t& out) {
      if (token + 1 >= tokens.size() || tokens[token] != keyword ||
          !parse_u32(tokens[token + 1], out)) {
        return false;
      }
      token += 2;
      return true;
    };
    const auto keyword_ref = [&](std::string_view keyword, std::uint32_t& out) {
      if (token + 1 >= tokens.size() || tokens[token] != keyword ||
          !parse_ref(tokens[token + 1], out)) {
        return false;
      }
      token += 2;
      return true;
    };
    const auto optional_u32 = [&](std::string_view keyword, std::uint32_t& out, bool& given) {
      if (token >= tokens.size() || tokens[token] != keyword) {
        return true;
      }
      given = true;
      return keyword_u32(keyword, out);
    };
    if (!keyword_ref("method", body.method) ||
        !optional_u32("maxstack", body.max_stack, max_stack_given) ||
        !optional_u32("local_count", body.local_count, local_count_given) ||
        !keyword_u32("init_scope", body.init_scope_depth) ||
        !keyword_u32("max_scope", body.max_scope_depth)) {
      return fail("body header is malformed");
    }
    std::uint32_t code_bytes = 0;
    if (!keyword_u32("code_bytes", code_bytes)) {
      return fail("body code size is malformed");
    }
    expected_code_bytes = code_bytes;
    return true;
  }

  bool measure_omitted_limits(std::size_t index, const std::vector<AbcInstruction>& code) {
    if (max_stack_given && local_count_given) {
      return true;
    }
    AbcMethodBody& measured = file.bodies[index];
    if (!local_count_given) {
      std::uint32_t parameter_locals = 0;
      if (measured.method < file.methods.size()) {
        const AbcMethodInfo& method = file.methods[measured.method];
        const std::uint32_t variadic = (method.needs_arguments() || method.needs_rest()) ? 1u : 0u;
        parameter_locals = 1 + method.param_count + variadic;
      }
      measured.local_count = std::max(parameter_locals, required_local_count(code));
    }
    if (!max_stack_given) {
      measured.max_stack = kVerifierStackCeiling;
    }
    AbcMethodFlow flow;
    AbcFlowIssue issue;
    const AbcFlowStatus status =
        verify_method_flow(file, static_cast<std::uint32_t>(index), flow, issue);
    if (status != AbcFlowStatus::Ok || !flow.verified) {
      std::string message =
          std::string("the verifier refused the assembled body: ") + flow_status_name(status);
      if (!issue.message.empty()) {
        message += ": " + issue.message;
      }
      return fail(message);
    }
    if (!max_stack_given) {
      measured.max_stack = flow.observed_max_stack;
    }
    return true;
  }

  bool define_label(const std::string& name) {
    if (name.empty() || name[0] != 'L') {
      return fail("label name is malformed");
    }
    if (!labels.emplace(name, code_size).second) {
      return fail("label " + name + " is defined twice");
    }
    return true;
  }

  bool parse_instruction(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
      return fail("instruction line is malformed");
    }
    std::uint32_t offset = 0;
    if (!parse_u32(tokens[0], offset)) {
      return fail("instruction offset is malformed");
    }
    if (offset != code_size) {
      return fail("instruction offsets are not contiguous");
    }
    return parse_operation(tokens, 1);
  }

  bool parse_operation(const std::vector<std::string>& tokens, std::size_t mnemonic) {
    if (tokens.size() <= mnemonic) {
      return fail("instruction line is malformed");
    }
    const std::uint8_t opcode = opcode_for(tokens[mnemonic]);
    const AbcOpcodeInfo* info = opcode == 0xFF ? nullptr : opcode_info(opcode);
    if (info == nullptr) {
      return fail("unknown opcode " + tokens[mnemonic]);
    }
    AbcInstruction instruction;
    instruction.offset = code_size;
    instruction.opcode = opcode;
    instruction.mnemonic = info->mnemonic;
    const std::size_t instruction_index = instructions.size();
    std::size_t token = mnemonic + 1;
    std::size_t length = 1;
    for (std::size_t operand_index = 0; operand_index < info->operand_count; ++operand_index) {
      const AbcOperandSpec& spec = info->operands[operand_index];
      AbcDecodedOperand operand;
      operand.role = spec.role;
      switch (spec.encoding) {
        case AbcOperandEncoding::U30: {
          if (token >= tokens.size()) {
            return fail("instruction operand is missing");
          }
          if (spec.role == AbcOperandRole::Count) {
            if (!parse_u32(tokens[token], operand.value)) {
              return fail("instruction count operand is malformed");
            }
          } else if (spec.role == AbcOperandRole::Immediate) {
            std::uint32_t wide = 0;
            if (parse_u32(tokens[token], wide) && wide >= 0xFFFF8000u) {
              operand.value = wide;
              operand.immediate = static_cast<std::int16_t>(wide);
            } else {
              std::int32_t immediate = 0;
              if (!parse_i32(tokens[token], immediate)) {
                return fail("instruction immediate operand is malformed");
              }
              if (immediate != static_cast<std::int16_t>(immediate)) {
                return fail("instruction immediate operand does not fit 16 bits");
              }
              operand.immediate = immediate;
              operand.value = static_cast<std::uint16_t>(immediate);
            }
          } else if (!parse_ref(tokens[token], operand.value)) {
            return fail("instruction reference operand is malformed");
          }
          ++token;
          length += u30_size(operand.value);
          break;
        }
        case AbcOperandEncoding::S24: {
          if (token + 1 >= tokens.size() || tokens[token] != "->") {
            return fail("branch operand is missing a target");
          }
          PendingBranch branch;
          branch.instruction = instruction_index;
          branch.operand = operand_index;
          if (!pending_target(tokens[token + 1], branch.target)) {
            return fail("branch target is malformed");
          }
          branches.push_back(std::move(branch));
          token += 2;
          length += 3;
          break;
        }
        case AbcOperandEncoding::S8: {
          std::int32_t immediate = 0;
          if (token >= tokens.size() || !parse_i32(tokens[token], immediate)) {
            return fail("byte immediate operand is malformed");
          }
          operand.immediate = immediate;
          ++token;
          length += 1;
          break;
        }
        case AbcOperandEncoding::U8: {
          std::uint32_t value = 0;
          if (token >= tokens.size() || !parse_u32(tokens[token], value)) {
            return fail("byte operand is malformed");
          }
          operand.value = value;
          ++token;
          length += 1;
          break;
        }
        case AbcOperandEncoding::Switch: {
          if (token >= tokens.size() || tokens[token] != "switch") {
            return fail("lookupswitch operand is malformed");
          }
          ++token;
          if (token >= tokens.size()) {
            return fail("lookupswitch default target is missing");
          }
          PendingSwitch pending;
          pending.instruction = instruction_index;
          pending.operand = operand_index;
          if (!pending_target(tokens[token], pending.default_target)) {
            return fail("lookupswitch default target is malformed");
          }
          ++token;
          while (token < tokens.size()) {
            PendingTarget case_target;
            if (!pending_target(tokens[token], case_target)) {
              return fail("lookupswitch case target is malformed");
            }
            pending.case_targets.push_back(std::move(case_target));
            ++token;
          }
          const std::size_t cases = pending.case_targets.size();
          if (cases == 0) {
            return fail("lookupswitch needs at least one case");
          }
          switches.push_back(std::move(pending));
          length += 3 + u30_size(static_cast<std::uint32_t>(cases - 1)) + 3 * cases;
          break;
        }
        case AbcOperandEncoding::None:
          break;
      }
      instruction.operands.push_back(std::move(operand));
    }
    if (token != tokens.size()) {
      return fail("instruction has trailing tokens");
    }
    instruction.length = length;
    code_size += length;
    instructions.push_back(std::move(instruction));
    return true;
  }
};

}  // namespace

AbcStatus parse_body(AbcFile& file, std::size_t body_index,
                     const std::vector<std::string>& block, std::size_t first_line,
                     std::string& error) {
  BodyParser parser{file, error, first_line, {}, {}, {}, {}, {}, {}, 0, false, false, 0};
  if (!parser.parse_header(split_tokens(block.front()))) {
    return AbcStatus::InconsistentModel;
  }
  std::size_t line_index = 1;
  bool traits_follow = false;
  for (; line_index < block.size(); ++line_index) {
    const std::vector<std::string> tokens = split_tokens(block[line_index]);
    if (tokens.empty()) {
      continue;
    }
    if (tokens[0] == "code") {
      parser.in_code = true;
      parser.saw_code = true;
      continue;
    }
    if (tokens[0] == "end_code") {
      parser.in_code = false;
      continue;
    }
    if (tokens[0] == "exceptions") {
      parser.in_code = false;
      continue;
    }
    if (tokens[0] == "traits") {
      traits_follow = true;
      ++line_index;
      break;
    }
    if (tokens[0] == "exception") {
      PendingException pending;
      if (tokens.size() != 8 || tokens[4] != "type" || tokens[6] != "var" ||
          !parse_ref(tokens[5], pending.exc_type) || !parse_ref(tokens[7], pending.var_name)) {
        parser.fail("exception line is malformed");
        return AbcStatus::InconsistentModel;
      }
      if (!pending_target(tokens[1], pending.from) || !pending_target(tokens[2], pending.to) ||
          !pending_target(tokens[3], pending.target)) {
        parser.fail("exception target is malformed");
        return AbcStatus::InconsistentModel;
      }
      parser.exceptions.push_back(std::move(pending));
      continue;
    }
    if (tokens[0].size() > 1 && tokens[0].back() == ':') {
      if (!parser.define_label(tokens[0].substr(0, tokens[0].size() - 1))) {
        return AbcStatus::InconsistentModel;
      }
      continue;
    }
    if (parser.in_code) {
      if (!parser.parse_instruction(tokens)) {
        return AbcStatus::InconsistentModel;
      }
      continue;
    }
  }
  if (!parser.saw_code) {
    parser.fail("body has no code section");
    return AbcStatus::InconsistentModel;
  }
  if (traits_follow) {
    for (; line_index < block.size(); ++line_index) {
      Cursor trait_cursor(split_tokens(block[line_index]), error);
      AbcTrait trait;
      if (parse_trait(trait_cursor, trait) != AbcStatus::Ok) {
        if (error.empty()) {
          error = "body trait is malformed";
        }
        return AbcStatus::InconsistentModel;
      }
      parser.body.traits.push_back(std::move(trait));
    }
  }

  const auto resolve = [&parser](const PendingTarget& target, std::int32_t& out) {
    if (target.is_absolute) {
      out = static_cast<std::int32_t>(target.absolute);
      return true;
    }
    const auto found = parser.labels.find(target.label);
    if (found == parser.labels.end()) {
      parser.fail("label " + target.label + " is not defined");
      return false;
    }
    out = static_cast<std::int32_t>(found->second);
    return true;
  };
  const auto check_s24 = [&parser](std::int32_t target, std::size_t origin) {
    const std::int64_t relative =
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(origin);
    if (relative >= -(std::int64_t{1} << 23) && relative < (std::int64_t{1} << 23)) {
      return true;
    }
    return parser.fail("target offset is outside the signed 24-bit range");
  };
  for (const PendingBranch& branch : parser.branches) {
    const AbcInstruction& instruction = parser.instructions[branch.instruction];
    std::int32_t& target = parser.instructions[branch.instruction].operands[branch.operand].target;
    if (!resolve(branch.target, target) ||
        !check_s24(target, instruction.offset + instruction.length)) {
      return AbcStatus::InconsistentModel;
    }
  }
  for (const PendingSwitch& pending : parser.switches) {
    const std::size_t offset = parser.instructions[pending.instruction].offset;
    AbcDecodedOperand& operand =
        parser.instructions[pending.instruction].operands[pending.operand];
    if (!resolve(pending.default_target, operand.target) || !check_s24(operand.target, offset)) {
      return AbcStatus::InconsistentModel;
    }
    operand.case_targets.resize(pending.case_targets.size());
    for (std::size_t index = 0; index < pending.case_targets.size(); ++index) {
      if (!resolve(pending.case_targets[index], operand.case_targets[index]) ||
          !check_s24(operand.case_targets[index], offset)) {
        return AbcStatus::InconsistentModel;
      }
    }
  }

  if (parser.code_size != parser.expected_code_bytes) {
    parser.fail("body code size does not match code_bytes");
    return AbcStatus::InconsistentModel;
  }
  std::vector<std::uint8_t> code;
  if (encode_code(parser.instructions, code) != AbcStatus::Ok) {
    parser.fail("body instructions do not encode");
    return AbcStatus::InconsistentModel;
  }
  std::vector<AbcInstruction> verified;
  if (decode_code(code, verified, {}) != AbcStatus::Ok ||
      verified.size() != parser.instructions.size()) {
    parser.fail("body code does not decode back");
    return AbcStatus::InconsistentModel;
  }
  for (std::size_t index = 0; index < verified.size(); ++index) {
    if (verified[index].offset != parser.instructions[index].offset ||
        verified[index].opcode != parser.instructions[index].opcode) {
      parser.fail("body code does not round trip through the encoder");
      return AbcStatus::InconsistentModel;
    }
  }
  parser.body.code = std::move(code);
  for (const PendingException& pending : parser.exceptions) {
    AbcException exception;
    std::int32_t from = 0;
    std::int32_t to = 0;
    std::int32_t target = 0;
    if (!resolve(pending.from, from) || !resolve(pending.to, to) ||
        !resolve(pending.target, target)) {
      return AbcStatus::InconsistentModel;
    }
    if (from < 0 || to < 0 || target < 0) {
      parser.fail("exception offset is negative");
      return AbcStatus::InconsistentModel;
    }
    exception.from = static_cast<std::uint32_t>(from);
    exception.to = static_cast<std::uint32_t>(to);
    exception.target = static_cast<std::uint32_t>(target);
    exception.exc_type = pending.exc_type;
    exception.var_name = pending.var_name;
    parser.body.exceptions.push_back(exception);
  }
  if (body_index != file.bodies.size()) {
    parser.fail("body index is not sequential");
    return AbcStatus::InconsistentModel;
  }
  file.bodies.push_back(std::move(parser.body));
  if (!parser.measure_omitted_limits(body_index, verified)) {
    file.bodies.pop_back();
    return AbcStatus::InconsistentModel;
  }
  return AbcStatus::Ok;
}

AbcStatus parse_instruction_text(std::string_view text, std::vector<std::uint8_t>& out,
                                 std::string& error) {
  AbcFile scratch;
  BodyParser parser{scratch, error, 0, {}, {}, {}, {}, {}, {}, 0, false, false, 0};
  bool ok = parser.parse_operation(split_tokens(text), 0);
  if (ok && (!parser.branches.empty() || !parser.switches.empty())) {
    ok = parser.fail("branch operands need a method body");
  }
  if (ok && encode_code(parser.instructions, out) != AbcStatus::Ok) {
    ok = parser.fail("instruction does not encode");
  }
  std::vector<AbcInstruction> decoded;
  if (ok && (decode_code(out, decoded) != AbcStatus::Ok || decoded.size() != 1 ||
             !same_instruction(parser.instructions[0], decoded[0]))) {
    ok = parser.fail("instruction does not round trip through the decoder");
  }
  if (!ok) {
    out.clear();
    return AbcStatus::InconsistentModel;
  }
  return AbcStatus::Ok;
}

}  // namespace fo4::abc::detail::text

namespace fo4::abc {

AbcStatus assemble_instruction_text(std::string_view text, std::vector<std::uint8_t>& out,
                                    AbcTextError* error) {
  std::string message;
  const AbcStatus status = detail::text::parse_instruction_text(text, out, message);
  if (status != AbcStatus::Ok && error != nullptr) {
    error->line = 1;
    error->message = message;
  }
  return status;
}

}  // namespace fo4::abc
