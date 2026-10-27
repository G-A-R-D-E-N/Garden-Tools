#include <fo4/abc/opcodes.hpp>

#include <array>
#include <span>

namespace fo4::abc {

namespace {

using Enc = AbcOperandEncoding;
using Role = AbcOperandRole;

constexpr AbcOperandSpec spec(Enc encoding, Role role) {
  return AbcOperandSpec{encoding, role};
}

constexpr AbcOperandSpec kNone = spec(Enc::None, Role::None);
constexpr AbcOperandSpec kMultiname = spec(Enc::U30, Role::Multiname);
constexpr AbcOperandSpec kString = spec(Enc::U30, Role::String);
constexpr AbcOperandSpec kNamespace = spec(Enc::U30, Role::Namespace);
constexpr AbcOperandSpec kMethod = spec(Enc::U30, Role::Method);
constexpr AbcOperandSpec kClass = spec(Enc::U30, Role::Class);
constexpr AbcOperandSpec kInt = spec(Enc::U30, Role::Int);
constexpr AbcOperandSpec kUInt = spec(Enc::U30, Role::UInt);
constexpr AbcOperandSpec kDouble = spec(Enc::U30, Role::Double);
constexpr AbcOperandSpec kCount = spec(Enc::U30, Role::Count);
constexpr AbcOperandSpec kImmediate = spec(Enc::U30, Role::Immediate);
constexpr AbcOperandSpec kByte = spec(Enc::S8, Role::Immediate);
constexpr AbcOperandSpec kU8Count = spec(Enc::U8, Role::Count);
constexpr AbcOperandSpec kOffset = spec(Enc::S24, Role::Offset);
constexpr AbcOperandSpec kSwitch = spec(Enc::Switch, Role::Switch);

const char* const kMnemonics[256] = {
    nullptr, "bkpt", "nop", "throw",
    "getsuper", "setsuper", "dxns", "dxnslate",
    "kill", "label", nullptr, nullptr,
    "ifnlt", "ifnle", "ifngt", "ifnge",
    "jump", "iftrue", "iffalse", "ifeq",
    "ifne", "iflt", "ifle", "ifgt",
    "ifge", "ifstricteq", "ifstrictne", "lookupswitch",
    "pushwith", "popscope", "nextname", "hasnext",
    "pushnull", "pushundefined", nullptr, "nextvalue",
    "pushbyte", "pushshort", "pushtrue", "pushfalse",
    "pushnan", "pop", "dup", "swap",
    "pushstring", "pushint", "pushuint", "pushdouble",
    "pushscope", "pushnamespace", "hasnext2", nullptr,
    nullptr, "li8", "li16", "li32",
    "lf32", "lf64", "si8", "si16",
    "si32", "sf32", "sf64", nullptr,
    "newfunction", "call", "construct", "callmethod",
    "callstatic", "callsuper", "callproperty", "returnvoid",
    "returnvalue", "constructsuper", "constructprop", nullptr,
    "callproplex", nullptr, "callsupervoid", "callpropvoid",
    "sxi1", "sxi8", "sxi16", "applytype",
    nullptr, "newobject", "newarray", "newactivation",
    "newclass", "getdescendants", "newcatch", nullptr,
    nullptr, "findpropstrict", "findproperty", "finddef",
    "getlex", "setproperty", "getlocal", "setlocal",
    "getglobalscope", "getscopeobject", "getproperty", "getouterscope",
    "initproperty", "setpropertylate", "deleteproperty", "deletepropertylate",
    "getslot", "setslot", "getglobalslot", "setglobalslot",
    "convert_s", "esc_xelem", "esc_xattr", "convert_i",
    "convert_u", "convert_d", "convert_b", "convert_o",
    "checkfilter", nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    "coerce", "coerce_b", "coerce_a", "coerce_i",
    "coerce_d", "coerce_s", "astype", "astypelate",
    "coerce_u", "coerce_o", nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    "negate", "increment", "inclocal", "decrement",
    "declocal", "typeof", "not", "bitnot",
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    "add", "subtract", "multiply", "divide",
    "modulo", "lshift", "rshift", "urshift",
    "bitand", "bitor", "bitxor", "equals",
    "strictequals", "lessthan", "lessequals", "greaterthan",
    "greaterequals", "instanceof", "istype", "istypelate",
    "in", nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    "increment_i", "decrement_i", "inclocal_i", "declocal_i",
    "negate_i", "add_i", "subtract_i", "multiply_i",
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    "getlocal0", "getlocal1", "getlocal2", "getlocal3",
    "setlocal0", "setlocal1", "setlocal2", "setlocal3",
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, "debug",
    "debugline", "debugfile", "bkptline", "timestamp",
};

std::uint8_t operand_specs(std::uint8_t opcode, AbcOperandSpec (&out)[4]) {
  out[0] = kNone;
  out[1] = kNone;
  out[2] = kNone;
  out[3] = kNone;
  switch (opcode) {
    case 0x04:
    case 0x05:
    case 0x59:
    case 0x5D:
    case 0x5E:
    case 0x5F:
    case 0x60:
    case 0x61:
    case 0x66:
    case 0x68:
    case 0x6A:
    case 0x80:
    case 0x86:
    case 0xB2:
      out[0] = kMultiname;
      return 1;
    case 0x45:
    case 0x46:
    case 0x4A:
    case 0x4C:
    case 0x4E:
    case 0x4F:
      out[0] = kMultiname;
      out[1] = kCount;
      return 2;
    case 0x06:
    case 0x2C:
    case 0xF1:
      out[0] = kString;
      return 1;
    case 0x31:
      out[0] = kNamespace;
      return 1;
    case 0x40:
      out[0] = kMethod;
      return 1;
    case 0x44:
      out[0] = kMethod;
      out[1] = kCount;
      return 2;
    case 0x58:
      out[0] = kClass;
      return 1;
    case 0x2D:
      out[0] = kInt;
      return 1;
    case 0x2E:
      out[0] = kUInt;
      return 1;
    case 0x2F:
      out[0] = kDouble;
      return 1;
    case 0x08:
    case 0x41:
    case 0x42:
    case 0x49:
    case 0x53:
    case 0x55:
    case 0x56:
    case 0x5A:
    case 0x62:
    case 0x63:
    case 0x67:
    case 0x6C:
    case 0x6D:
    case 0x6E:
    case 0x6F:
    case 0x92:
    case 0x94:
    case 0xC2:
    case 0xC3:
    case 0xF0:
    case 0xF2:
      out[0] = kCount;
      return 1;
    case 0x32:
    case 0x43:
      out[0] = kCount;
      out[1] = kCount;
      return 2;
    case 0x65:
      out[0] = kU8Count;
      return 1;
    case 0x24:
      out[0] = kByte;
      return 1;
    case 0x25:
      out[0] = kImmediate;
      return 1;
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1A:
      out[0] = kOffset;
      return 1;
    case 0x1B:
      out[0] = kSwitch;
      return 1;
    case 0xEF:
      out[0] = kU8Count;
      out[1] = kString;
      out[2] = kU8Count;
      out[3] = kCount;
      return 4;
    default:
      return 0;
  }
}

std::array<AbcOpcodeInfo, 256> build_table() {
  std::array<AbcOpcodeInfo, 256> table{};
  for (std::size_t i = 0; i < table.size(); ++i) {
    table[i].mnemonic = kMnemonics[i];
    table[i].operand_count = operand_specs(static_cast<std::uint8_t>(i), table[i].operands);
  }
  return table;
}

const std::array<AbcOpcodeInfo, 256>& opcode_table() {
  static const std::array<AbcOpcodeInfo, 256> table = build_table();
  return table;
}

void put_u8(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  while (value >= 0x80u) {
    put_u8(out, (value & 0x7Fu) | 0x80u);
    value >>= 7;
  }
  put_u8(out, value);
}

void put_u30(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u32(out, value & 0x3FFFFFFFu);
}

void put_s24(std::vector<std::uint8_t>& out, std::int32_t value) {
  const std::uint32_t raw = static_cast<std::uint32_t>(value) & 0xFFFFFFu;
  put_u8(out, raw & 0xFFu);
  put_u8(out, (raw >> 8) & 0xFFu);
  put_u8(out, (raw >> 16) & 0xFFu);
}

bool in_code_range(std::int64_t target, std::size_t size) {
  return target >= 0 && target <= static_cast<std::int64_t>(size);
}

bool ends_control_flow(std::uint8_t opcode) {
  switch (opcode) {
    case 0x03:
    case 0x10:
    case 0x1B:
    case 0x47:
    case 0x48:
      return true;
    default:
      return false;
  }
}

std::vector<bool> mark_reachable(const std::vector<AbcInstruction>& code,
                                 const std::vector<std::size_t>& index_at,
                                 std::span<const std::size_t> roots) {
  std::vector<bool> reachable(code.size(), false);
  std::vector<std::size_t> pending(roots.begin(), roots.end());
  while (!pending.empty()) {
    const std::size_t index = pending.back();
    pending.pop_back();
    if (reachable[index]) {
      continue;
    }
    reachable[index] = true;
    const AbcInstruction& instruction = code[index];
    const std::int64_t start = static_cast<std::int64_t>(instruction.offset);
    const std::int64_t next = start + static_cast<std::int64_t>(instruction.length);
    const auto push = [&pending, &index_at, &code](std::int64_t target) {
      if (target < 0 || target > static_cast<std::int64_t>(index_at.size() - 1)) {
        return;
      }
      const std::size_t target_index = index_at[static_cast<std::size_t>(target)];
      if (target_index < code.size()) {
        pending.push_back(target_index);
      }
    };
    if (!ends_control_flow(instruction.opcode)) {
      push(next);
    }
    for (const AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == AbcOperandRole::Offset) {
        push(next + operand.immediate);
      } else if (operand.role == AbcOperandRole::Switch) {
        push(start + operand.immediate);
        for (const std::int32_t case_target : operand.case_targets) {
          push(start + case_target);
        }
      }
    }
  }
  return reachable;
}

}

const AbcOpcodeInfo* opcode_info(std::uint8_t opcode) noexcept {
  const AbcOpcodeInfo& info = opcode_table()[opcode];
  return info.mnemonic == nullptr ? nullptr : &info;
}

AbcStatus decode_code(const std::uint8_t* code, std::size_t size,
                      std::vector<AbcInstruction>& out,
                      std::span<const AbcException> exceptions) {
  out.clear();
  ByteReader reader(code, size);
  if (!reader.ok()) {
    return reader.status();
  }
  while (reader.remaining() > 0) {
    AbcInstruction instruction;
    instruction.offset = reader.offset();
    const std::uint8_t opcode = reader.u8();
    const AbcOpcodeInfo* info = opcode_info(opcode);
    if (info == nullptr) {
      return AbcStatus::BadOpcode;
    }
    instruction.opcode = opcode;
    instruction.mnemonic = info->mnemonic;
    for (std::uint8_t i = 0; i < info->operand_count; ++i) {
      const AbcOperandSpec& operand_spec = info->operands[i];
      AbcDecodedOperand operand;
      operand.role = operand_spec.role;
      switch (operand_spec.encoding) {
        case Enc::U30:
          if (operand_spec.role == Role::Immediate) {
            operand.value = reader.u32();
            operand.immediate = static_cast<std::int16_t>(operand.value);
          } else {
            operand.value = reader.u30();
          }
          break;
        case Enc::S24:
          operand.immediate = reader.s24();
          break;
        case Enc::S8:
          operand.immediate = reader.s8();
          break;
        case Enc::U8:
          operand.value = reader.u8();
          break;
        case Enc::Switch: {
          operand.immediate = reader.s24();
          const std::uint32_t cases = reader.u30();
          if (!reader.ok()) {
            return reader.status();
          }
          if (static_cast<std::size_t>(cases) + 1 > reader.remaining() / 3) {
            return AbcStatus::CountOutOfRange;
          }
          operand.case_targets.resize(static_cast<std::size_t>(cases) + 1);
          for (std::size_t c = 0; c < operand.case_targets.size(); ++c) {
            operand.case_targets[c] = reader.s24();
          }
          break;
        }
        case Enc::None:
          break;
      }
      if (!reader.ok()) {
        return reader.status();
      }
      instruction.operands.push_back(operand);
    }
    instruction.length = reader.offset() - instruction.offset;
    out.push_back(instruction);
  }
  std::vector<std::size_t> index_at(size + 1, out.size());
  for (std::size_t index = 0; index < out.size(); ++index) {
    index_at[out[index].offset] = index;
  }
  std::vector<std::size_t> roots;
  if (!out.empty()) {
    roots.push_back(0);
  }
  for (const AbcException& exception : exceptions) {
    if (exception.target >= size || index_at[exception.target] >= out.size()) {
      return AbcStatus::BranchOutOfRange;
    }
    roots.push_back(index_at[exception.target]);
  }
  const std::vector<bool> reachable = mark_reachable(out, index_at, roots);
  for (std::size_t index = 0; index < out.size(); ++index) {
    AbcInstruction& instruction = out[index];
    const std::int64_t next = static_cast<std::int64_t>(instruction.offset + instruction.length);
    const std::int64_t start = static_cast<std::int64_t>(instruction.offset);
    for (AbcDecodedOperand& operand : instruction.operands) {
      if (operand.role == Role::Offset) {
        const std::int64_t target = next + operand.immediate;
        if (reachable[index] && !in_code_range(target, size)) {
          return AbcStatus::BranchOutOfRange;
        }
        operand.target = static_cast<std::int32_t>(target);
      } else if (operand.role == Role::Switch) {
        const std::int64_t default_target = start + operand.immediate;
        if (reachable[index] && !in_code_range(default_target, size)) {
          return AbcStatus::BranchOutOfRange;
        }
        operand.target = static_cast<std::int32_t>(default_target);
        for (std::int32_t& case_target : operand.case_targets) {
          const std::int64_t target = start + case_target;
          if (reachable[index] && !in_code_range(target, size)) {
            return AbcStatus::BranchOutOfRange;
          }
          case_target = static_cast<std::int32_t>(target);
        }
      }
    }
  }
  return AbcStatus::Ok;
}

AbcStatus decode_code(const std::vector<std::uint8_t>& code,
                      std::vector<AbcInstruction>& out,
                      std::span<const AbcException> exceptions) {
  return decode_code(code.empty() ? nullptr : code.data(), code.size(), out, exceptions);
}

AbcStatus encode_code(const std::vector<AbcInstruction>& code, std::vector<std::uint8_t>& out) {
  out.clear();
  for (const AbcInstruction& instruction : code) {
    const AbcOpcodeInfo* info = opcode_info(instruction.opcode);
    if (info == nullptr) {
      return AbcStatus::BadOpcode;
    }
    if (instruction.operands.size() != info->operand_count) {
      return AbcStatus::InconsistentModel;
    }
    const std::size_t start = out.size();
    out.push_back(instruction.opcode);
    const std::int64_t next = static_cast<std::int64_t>(instruction.offset) +
                              static_cast<std::int64_t>(instruction.length);
    const std::int64_t here = static_cast<std::int64_t>(instruction.offset);
    for (std::size_t index = 0; index < info->operand_count; ++index) {
      const AbcOperandSpec& spec = info->operands[index];
      const AbcDecodedOperand& operand = instruction.operands[index];
      switch (spec.encoding) {
        case AbcOperandEncoding::U30:
          if (spec.role == AbcOperandRole::Immediate) {
            put_u32(out, operand.value);
          } else {
            put_u30(out, operand.value);
          }
          break;
        case AbcOperandEncoding::S24:
          put_s24(out, static_cast<std::int32_t>(static_cast<std::int64_t>(operand.target) - next));
          break;
        case AbcOperandEncoding::S8:
          put_u8(out, static_cast<std::uint32_t>(operand.immediate) & 0xFFu);
          break;
        case AbcOperandEncoding::U8:
          put_u8(out, operand.value & 0xFFu);
          break;
        case AbcOperandEncoding::Switch: {
          put_s24(out, static_cast<std::int32_t>(static_cast<std::int64_t>(operand.target) - here));
          const std::size_t cases =
              operand.case_targets.empty() ? 0 : operand.case_targets.size() - 1;
          put_u30(out, static_cast<std::uint32_t>(cases));
          for (const std::int32_t target : operand.case_targets) {
            put_s24(out, static_cast<std::int32_t>(static_cast<std::int64_t>(target) - here));
          }
          break;
        }
        case AbcOperandEncoding::None:
          break;
      }
    }
    if (out.size() - start != instruction.length) {
      return AbcStatus::InconsistentModel;
    }
  }
  return AbcStatus::Ok;
}

AbcStatus validate_operand_pools(const AbcFile& file,
                                 const std::vector<AbcInstruction>& code) {
  for (const AbcInstruction& instruction : code) {
    for (const AbcDecodedOperand& operand : instruction.operands) {
      std::size_t limit = 0;
      switch (operand.role) {
        case Role::Multiname:
          limit = file.pool.multinames.size();
          break;
        case Role::String:
          limit = file.pool.strings.size();
          break;
        case Role::Namespace:
          limit = file.pool.namespaces.size();
          break;
        case Role::Method:
          limit = file.methods.size();
          break;
        case Role::Class:
          limit = file.instances.size();
          break;
        case Role::Int:
          limit = file.pool.ints.size();
          break;
        case Role::UInt:
          limit = file.pool.uints.size();
          break;
        case Role::Double:
          limit = file.pool.doubles.size();
          break;
        default:
          continue;
      }
      if (static_cast<std::size_t>(operand.value) >= limit) {
        return AbcStatus::IndexOutOfRange;
      }
    }
  }
  return AbcStatus::Ok;
}

}
