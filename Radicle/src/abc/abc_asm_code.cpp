#include <fo4/abc/abc_asm.hpp>

#include <fo4/abc/abc.hpp>
#include <fo4/abc/opcodes.hpp>

#include "abc_asm_internal.hpp"

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

struct PendingBranch {
  std::size_t instruction{0};
  std::size_t operand{0};
  std::string label;
};

struct PendingSwitch {
  std::size_t instruction{0};
  std::size_t operand{0};
  std::string default_label;
  std::vector<std::string> case_labels;
};

struct PendingException {
  std::string from;
  std::string to;
  std::string target;
  std::uint32_t exc_type{0};
  std::uint32_t var_name{0};
};

struct BodyParser {
  AbcFile& file;
  std::string& error;
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

  bool fail(std::string_view message) {
    if (error.empty()) {
      error = std::string{message};
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
    if (!keyword_ref("method", body.method) || !keyword_u32("maxstack", body.max_stack) ||
        !keyword_u32("local_count", body.local_count) ||
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
            std::int32_t immediate = 0;
            if (!parse_i32(tokens[token], immediate)) {
              return fail("instruction immediate operand is malformed");
            }
            if (immediate != static_cast<std::int16_t>(immediate)) {
              return fail("instruction immediate operand does not fit 16 bits");
            }
            operand.immediate = immediate;
            operand.value = static_cast<std::uint16_t>(immediate);
          } else if (!parse_ref(tokens[token], operand.value)) {
            return fail("instruction reference operand is malformed");
          }
          ++token;
          length += u30_size(operand.value);
          break;
        }
        case AbcOperandEncoding::S24: {
          if (token + 1 >= tokens.size() || tokens[token] != "->") {
            return fail("branch operand is missing a label");
          }
          PendingBranch branch;
          branch.instruction = instruction_index;
          branch.operand = operand_index;
          branch.label = tokens[token + 1];
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
            return fail("lookupswitch default label is missing");
          }
          PendingSwitch pending;
          pending.instruction = instruction_index;
          pending.operand = operand_index;
          pending.default_label = tokens[token];
          ++token;
          while (token < tokens.size()) {
            pending.case_labels.push_back(tokens[token]);
            ++token;
          }
          const std::size_t cases = pending.case_labels.size();
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
                     const std::vector<std::string>& block, std::string& error) {
  BodyParser parser{file, error, {}, {}, {}, {}, {}, {}, 0, false, false, 0};
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
      pending.from = tokens[1];
      pending.to = tokens[2];
      pending.target = tokens[3];
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

  const auto resolve = [&parser](const std::string& label, std::int32_t& out) {
    const auto found = parser.labels.find(label);
    if (found == parser.labels.end()) {
      parser.fail("label " + label + " is not defined");
      return false;
    }
    out = static_cast<std::int32_t>(found->second);
    return true;
  };
  for (const PendingBranch& branch : parser.branches) {
    if (!resolve(branch.label,
                 parser.instructions[branch.instruction].operands[branch.operand].target)) {
      return AbcStatus::InconsistentModel;
    }
  }
  for (const PendingSwitch& pending : parser.switches) {
    AbcDecodedOperand& operand =
        parser.instructions[pending.instruction].operands[pending.operand];
    if (!resolve(pending.default_label, operand.target)) {
      return AbcStatus::InconsistentModel;
    }
    operand.case_targets.resize(pending.case_labels.size());
    for (std::size_t index = 0; index < pending.case_labels.size(); ++index) {
      if (!resolve(pending.case_labels[index], operand.case_targets[index])) {
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
  return AbcStatus::Ok;
}

AbcStatus parse_instruction_text(std::string_view text, std::vector<std::uint8_t>& out,
                                 std::string& error) {
  AbcFile scratch;
  BodyParser parser{scratch, error, {}, {}, {}, {}, {}, {}, 0, false, false, 0};
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
