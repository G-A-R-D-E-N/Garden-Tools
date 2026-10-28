#pragma once

#include <fo4/abc/opcodes.hpp>
#include <fo4/avm2/name.hpp>
#include <fo4/avm2/object.hpp>
#include <fo4/avm2/value.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace fo4::avm2::detail {

namespace op {
  constexpr std::uint8_t k_bkpt = 0x01;
  constexpr std::uint8_t k_nop = 0x02;
  constexpr std::uint8_t k_throw = 0x03;
  constexpr std::uint8_t k_getsuper = 0x04;
  constexpr std::uint8_t k_setsuper = 0x05;
  constexpr std::uint8_t k_kill = 0x08;
  constexpr std::uint8_t k_label = 0x09;
  constexpr std::uint8_t k_ifnlt = 0x0C;
  constexpr std::uint8_t k_ifnle = 0x0D;
  constexpr std::uint8_t k_ifngt = 0x0E;
  constexpr std::uint8_t k_ifnge = 0x0F;
  constexpr std::uint8_t k_jump = 0x10;
  constexpr std::uint8_t k_iftrue = 0x11;
  constexpr std::uint8_t k_iffalse = 0x12;
  constexpr std::uint8_t k_ifeq = 0x13;
  constexpr std::uint8_t k_ifne = 0x14;
  constexpr std::uint8_t k_iflt = 0x15;
  constexpr std::uint8_t k_ifle = 0x16;
  constexpr std::uint8_t k_ifgt = 0x17;
  constexpr std::uint8_t k_ifge = 0x18;
  constexpr std::uint8_t k_ifstricteq = 0x19;
  constexpr std::uint8_t k_ifstrictne = 0x1A;
  constexpr std::uint8_t k_lookupswitch = 0x1B;
  constexpr std::uint8_t k_pushwith = 0x1C;
  constexpr std::uint8_t k_popscope = 0x1D;
  constexpr std::uint8_t k_nextname = 0x1E;
  constexpr std::uint8_t k_pushnull = 0x20;
  constexpr std::uint8_t k_pushundefined = 0x21;
  constexpr std::uint8_t k_nextvalue = 0x23;
  constexpr std::uint8_t k_pushbyte = 0x24;
  constexpr std::uint8_t k_pushshort = 0x25;
  constexpr std::uint8_t k_pushtrue = 0x26;
  constexpr std::uint8_t k_pushfalse = 0x27;
  constexpr std::uint8_t k_pushnan = 0x28;
  constexpr std::uint8_t k_pop = 0x29;
  constexpr std::uint8_t k_dup = 0x2A;
  constexpr std::uint8_t k_swap = 0x2B;
  constexpr std::uint8_t k_pushstring = 0x2C;
  constexpr std::uint8_t k_pushint = 0x2D;
  constexpr std::uint8_t k_pushuint = 0x2E;
  constexpr std::uint8_t k_pushdouble = 0x2F;
  constexpr std::uint8_t k_pushscope = 0x30;
  constexpr std::uint8_t k_hasnext2 = 0x32;
  constexpr std::uint8_t k_newfunction = 0x40;
  constexpr std::uint8_t k_call = 0x41;
  constexpr std::uint8_t k_construct = 0x42;
  constexpr std::uint8_t k_callmethod = 0x43;
  constexpr std::uint8_t k_callstatic = 0x44;
  constexpr std::uint8_t k_callsuper = 0x45;
  constexpr std::uint8_t k_callproperty = 0x46;
  constexpr std::uint8_t k_returnvoid = 0x47;
  constexpr std::uint8_t k_returnvalue = 0x48;
  constexpr std::uint8_t k_constructsuper = 0x49;
  constexpr std::uint8_t k_constructprop = 0x4A;
  constexpr std::uint8_t k_callproplex = 0x4C;
  constexpr std::uint8_t k_callsupervoid = 0x4E;
  constexpr std::uint8_t k_callpropvoid = 0x4F;
  constexpr std::uint8_t k_applytype = 0x53;
  constexpr std::uint8_t k_newobject = 0x55;
  constexpr std::uint8_t k_newarray = 0x56;
  constexpr std::uint8_t k_newactivation = 0x57;
  constexpr std::uint8_t k_newclass = 0x58;
  constexpr std::uint8_t k_newcatch = 0x5A;
  constexpr std::uint8_t k_findpropstrict = 0x5D;
  constexpr std::uint8_t k_findproperty = 0x5E;
  constexpr std::uint8_t k_getlex = 0x60;
  constexpr std::uint8_t k_setproperty = 0x61;
  constexpr std::uint8_t k_getlocal = 0x62;
  constexpr std::uint8_t k_setlocal = 0x63;
  constexpr std::uint8_t k_getglobalscope = 0x64;
  constexpr std::uint8_t k_getscopeobject = 0x65;
  constexpr std::uint8_t k_getproperty = 0x66;
  constexpr std::uint8_t k_getouterscope = 0x67;
  constexpr std::uint8_t k_initproperty = 0x68;
  constexpr std::uint8_t k_deleteproperty = 0x6A;
  constexpr std::uint8_t k_getslot = 0x6C;
  constexpr std::uint8_t k_setslot = 0x6D;
  constexpr std::uint8_t k_convert_s = 0x70;
  constexpr std::uint8_t k_convert_i = 0x73;
  constexpr std::uint8_t k_convert_u = 0x74;
  constexpr std::uint8_t k_convert_d = 0x75;
  constexpr std::uint8_t k_convert_b = 0x76;
  constexpr std::uint8_t k_convert_o = 0x77;
  constexpr std::uint8_t k_coerce = 0x80;
  constexpr std::uint8_t k_coerce_a = 0x82;
  constexpr std::uint8_t k_coerce_s = 0x85;
  constexpr std::uint8_t k_astype = 0x86;
  constexpr std::uint8_t k_astypelate = 0x87;
  constexpr std::uint8_t k_negate = 0x90;
  constexpr std::uint8_t k_increment = 0x91;
  constexpr std::uint8_t k_inclocal = 0x92;
  constexpr std::uint8_t k_decrement = 0x93;
  constexpr std::uint8_t k_declocal = 0x94;
  constexpr std::uint8_t k_typeof = 0x95;
  constexpr std::uint8_t k_not = 0x96;
  constexpr std::uint8_t k_bitnot = 0x97;
  constexpr std::uint8_t k_add = 0xA0;
  constexpr std::uint8_t k_subtract = 0xA1;
  constexpr std::uint8_t k_multiply = 0xA2;
  constexpr std::uint8_t k_divide = 0xA3;
  constexpr std::uint8_t k_modulo = 0xA4;
  constexpr std::uint8_t k_lshift = 0xA5;
  constexpr std::uint8_t k_rshift = 0xA6;
  constexpr std::uint8_t k_urshift = 0xA7;
  constexpr std::uint8_t k_bitand = 0xA8;
  constexpr std::uint8_t k_bitor = 0xA9;
  constexpr std::uint8_t k_bitxor = 0xAA;
  constexpr std::uint8_t k_equals = 0xAB;
  constexpr std::uint8_t k_strictequals = 0xAC;
  constexpr std::uint8_t k_lessthan = 0xAD;
  constexpr std::uint8_t k_lessequals = 0xAE;
  constexpr std::uint8_t k_greaterthan = 0xAF;
  constexpr std::uint8_t k_greaterequals = 0xB0;
  constexpr std::uint8_t k_instanceof = 0xB1;
  constexpr std::uint8_t k_istype = 0xB2;
  constexpr std::uint8_t k_istypelate = 0xB3;
  constexpr std::uint8_t k_in = 0xB4;
  constexpr std::uint8_t k_increment_i = 0xC0;
  constexpr std::uint8_t k_decrement_i = 0xC1;
  constexpr std::uint8_t k_negate_i = 0xC4;
  constexpr std::uint8_t k_add_i = 0xC5;
  constexpr std::uint8_t k_subtract_i = 0xC6;
  constexpr std::uint8_t k_multiply_i = 0xC7;
  constexpr std::uint8_t k_inclocal_i = 0xC2;
  constexpr std::uint8_t k_declocal_i = 0xC3;
  constexpr std::uint8_t k_getlocal0 = 0xD0;
  constexpr std::uint8_t k_getlocal1 = 0xD1;
  constexpr std::uint8_t k_getlocal2 = 0xD2;
  constexpr std::uint8_t k_getlocal3 = 0xD3;
  constexpr std::uint8_t k_setlocal0 = 0xD4;
  constexpr std::uint8_t k_setlocal1 = 0xD5;
  constexpr std::uint8_t k_setlocal2 = 0xD6;
  constexpr std::uint8_t k_setlocal3 = 0xD7;
  constexpr std::uint8_t k_debug = 0xEF;
  constexpr std::uint8_t k_debugline = 0xF0;
  constexpr std::uint8_t k_debugfile = 0xF1;
  constexpr std::uint8_t k_bkptline = 0xF2;
  constexpr std::uint8_t k_timestamp = 0xF3;
}  // namespace op

std::uint32_t operand_at(const abc::AbcInstruction& instruction, std::size_t index);
std::int32_t immediate_at(const abc::AbcInstruction& instruction, std::size_t index);
std::uint32_t count_operand(const abc::AbcInstruction& instruction);
std::uint32_t multiname_operand(const abc::AbcInstruction& instruction);
std::uint32_t string_operand(const abc::AbcInstruction& instruction);
std::uint32_t int_operand(const abc::AbcInstruction& instruction);
std::uint32_t uint_operand(const abc::AbcInstruction& instruction);
std::uint32_t double_operand(const abc::AbcInstruction& instruction);
std::int32_t offset_operand(const abc::AbcInstruction& instruction);
const abc::AbcDecodedOperand* switch_operand(const abc::AbcInstruction& instruction);

Value borrowed_object(Object& object);
std::string property_text(const Multiname& multiname);
bool is_array_index(const std::string& name, std::uint32_t& out_index);

Value numeric_result(double value);
Value add_values(const Value& left, const Value& right);
Value subtract_values(const Value& left, const Value& right);
Value multiply_values(const Value& left, const Value& right);
Value divide_values(const Value& left, const Value& right);
Value modulo_values(const Value& left, const Value& right);
Value negate_value(const Value& value);
Value increment_value(const Value& value);
Value decrement_value(const Value& value);
Value bitwise_and(const Value& left, const Value& right);
Value bitwise_or(const Value& left, const Value& right);
Value bitwise_xor(const Value& left, const Value& right);
Value shift_left(const Value& left, const Value& right);
Value shift_right(const Value& left, const Value& right);
Value shift_unsigned(const Value& left, const Value& right);

}  // namespace fo4::avm2::detail
