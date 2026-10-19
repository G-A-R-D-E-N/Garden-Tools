#include "fo4/abc/abc.hpp"
#include "fo4/abc/opcodes.hpp"
#include "fo4/abc/summary.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using fo4::abc::AbcCallSite;
using fo4::abc::AbcFile;
using fo4::abc::AbcInstruction;
using fo4::abc::AbcOpcodeInfo;
using fo4::abc::AbcOperandEncoding;
using fo4::abc::AbcOperandRole;
using fo4::abc::AbcStatus;
using fo4::abc::AbcSummary;
using fo4::abc::ByteReader;

namespace {

constexpr std::uint32_t kStringFoo = 1;
constexpr std::uint32_t kStringExternalInterface = 2;
constexpr std::uint32_t kStringCall = 3;
constexpr std::uint32_t kStringTest = 4;
constexpr std::uint32_t kStringObject = 5;
constexpr std::uint32_t kStringFlashExternal = 6;
constexpr std::uint32_t kStringClick = 7;
constexpr std::uint32_t kStringAddEventListener = 8;
constexpr std::uint32_t kStringHandler = 9;
constexpr std::uint32_t kStringVoid = 10;
constexpr std::uint32_t kStringOnClick = 11;
constexpr std::uint32_t kStringCounter = 12;
constexpr std::uint32_t kStringInner = 13;
constexpr std::uint32_t kStringHelper = 14;

constexpr std::uint32_t kMultinameExternalInterface = 1;
constexpr std::uint32_t kMultinameCall = 2;
constexpr std::uint32_t kMultinameTest = 3;
constexpr std::uint32_t kMultinameObject = 4;
constexpr std::uint32_t kMultinameClick = 5;
constexpr std::uint32_t kMultinameAddEventListener = 6;
constexpr std::uint32_t kMultinameHandler = 7;
constexpr std::uint32_t kMultinameVoid = 8;

void put_u8(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
  put_u8(out, value & 0xFFu);
  put_u8(out, (value >> 8) & 0xFFu);
}

void put_u30(std::vector<std::uint8_t>& out, std::uint32_t value) {
  std::uint32_t remaining = value & 0x3FFFFFFFu;
  while (remaining >= 0x80u) {
    out.push_back(static_cast<std::uint8_t>((remaining & 0x7Fu) | 0x80u));
    remaining >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(remaining));
}

void put_s24(std::vector<std::uint8_t>& out, std::int32_t value) {
  const std::uint32_t raw = static_cast<std::uint32_t>(value) & 0xFFFFFFu;
  put_u8(out, raw & 0xFFu);
  put_u8(out, (raw >> 8) & 0xFFu);
  put_u8(out, (raw >> 16) & 0xFFu);
}

void put_string(std::vector<std::uint8_t>& out, const std::string& value) {
  put_u30(out, static_cast<std::uint32_t>(value.size()));
  for (const char ch : value) {
    out.push_back(static_cast<std::uint8_t>(ch));
  }
}

std::vector<std::uint8_t> tiny_constructor_code() {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 1);
  put_u8(code, 0x47);
  return code;
}

std::vector<std::uint8_t> tiny_listener_code() {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameAddEventListener);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x60);
  put_u30(code, kMultinameHandler);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameAddEventListener);
  put_u30(code, 2);
  put_u8(code, 0x47);
  return code;
}

std::vector<std::uint8_t> tiny_helper_code() {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x47);
  return code;
}

void put_constant_pool(std::vector<std::uint8_t>& out) {
  const std::vector<std::string> strings{
      "Foo",   "ExternalInterface", "call",   "Test",   "Object",
      "flash.external", "click", "addEventListener", "handler", "void",
      "onClick", "counter", "Inner", "helper"};
  put_u30(out, 2);
  put_u30(out, 7);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, static_cast<std::uint32_t>(strings.size()) + 1);
  for (const std::string& value : strings) {
    put_string(out, value);
  }
  put_u30(out, 3);
  put_u8(out, 0x16);
  put_u30(out, kStringFlashExternal);
  put_u8(out, 0x16);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 9);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, kStringExternalInterface);
  put_u8(out, 0x07);
  put_u30(out, 1);
  put_u30(out, kStringCall);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringTest);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringObject);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringClick);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringAddEventListener);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringHandler);
  put_u8(out, 0x07);
  put_u30(out, 2);
  put_u30(out, kStringVoid);
}

void put_method(std::vector<std::uint8_t>& out, std::uint32_t name) {
  put_u30(out, 0);
  put_u30(out, kMultinameVoid);
  put_u30(out, name);
  put_u8(out, 0);
}

void put_method_with_options(std::vector<std::uint8_t>& out, std::uint32_t name) {
  put_u30(out, 1);
  put_u30(out, kMultinameVoid);
  put_u30(out, kMultinameVoid);
  put_u30(out, name);
  put_u8(out, 0x88);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u8(out, 0x03);
  put_u30(out, kStringClick);
}

void put_instance_traits(std::vector<std::uint8_t>& out) {
  put_u30(out, 5);
  put_u30(out, kStringOnClick);
  put_u8(out, 0x41);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, kStringCounter);
  put_u8(out, 0x40);
  put_u30(out, 1);
  put_u30(out, kMultinameVoid);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, kStringCounter);
  put_u8(out, 0x06);
  put_u30(out, 2);
  put_u30(out, kMultinameVoid);
  put_u30(out, 1);
  put_u8(out, 0x03);
  put_u30(out, kStringInner);
  put_u8(out, 0x04);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, kStringHelper);
  put_u8(out, 0x45);
  put_u30(out, 3);
  put_u30(out, 2);
  put_u30(out, 1);
  put_u30(out, 0);
}

void put_body(std::vector<std::uint8_t>& out, std::uint32_t method,
              const std::vector<std::uint8_t>& code, bool with_exception) {
  put_u30(out, method);
  put_u30(out, 4);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, static_cast<std::uint32_t>(code.size()));
  out.insert(out.end(), code.begin(), code.end());
  put_u30(out, with_exception ? 1 : 0);
  if (with_exception) {
    put_u30(out, 0);
    put_u30(out, static_cast<std::uint32_t>(code.size()));
    put_u30(out, 0);
    put_u30(out, kMultinameTest);
    put_u30(out, kStringHelper);
  }
  put_u30(out, 0);
}

std::vector<std::uint8_t> build_abc(const std::vector<std::uint8_t>& constructor_code,
                                    const std::vector<std::uint8_t>& listener_code) {
  std::vector<std::uint8_t> out;
  put_u16(out, fo4::abc::kAbcMinorVersion);
  put_u16(out, fo4::abc::kAbcMajorVersion);
  put_constant_pool(out);
  put_u30(out, 3);
  put_method(out, kStringTest);
  put_method_with_options(out, kStringOnClick);
  put_method(out, kStringHelper);
  put_u30(out, 1);
  put_u30(out, kStringFoo);
  put_u30(out, 1);
  put_u30(out, kStringExternalInterface);
  put_u30(out, kStringCall);
  put_u30(out, 1);
  put_u30(out, kStringTest);
  put_u30(out, kMultinameObject);
  put_u8(out, 0x05);
  put_u30(out, 1);
  put_u30(out, kMultinameClick);
  put_u30(out, 0);
  put_instance_traits(out);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 3);
  put_body(out, 0, constructor_code, false);
  put_body(out, 1, listener_code, false);
  put_body(out, 2, tiny_helper_code(), true);
  return out;
}

std::vector<std::uint8_t> build_abc() {
  return build_abc(tiny_constructor_code(), tiny_listener_code());
}

std::vector<std::uint8_t> abc_header() {
  std::vector<std::uint8_t> out;
  put_u16(out, fo4::abc::kAbcMinorVersion);
  put_u16(out, fo4::abc::kAbcMajorVersion);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 1);
  return out;
}

std::vector<std::uint8_t> minimal_instance_file(std::uint8_t trait_tag,
                                               std::uint32_t metadata_index) {
  std::vector<std::uint8_t> out = abc_header();
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u8(out, 0x00);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, 0);
  put_u8(out, trait_tag);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 1);
  put_u30(out, metadata_index);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  put_u30(out, 0);
  return out;
}

AbcFile parse_or_fail(const std::vector<std::uint8_t>& bytes) {
  AbcFile file;
  EXPECT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  return file;
}

const AbcCallSite& only_call(const std::vector<AbcCallSite>& calls) {
  EXPECT_EQ(calls.size(), 1u);
  static const AbcCallSite missing;
  return calls.empty() ? missing : calls.front();
}

}

TEST(Abc, ParsesVersionConstantPoolAndStructure) {
  const std::vector<std::uint8_t> bytes = build_abc();
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  EXPECT_EQ(file.minor_version, 16u);
  EXPECT_EQ(file.major_version, 46u);
  EXPECT_EQ(file.pool.strings.size(), 15u);
  EXPECT_EQ(file.pool.namespaces.size(), 3u);
  EXPECT_EQ(file.pool.multinames.size(), 9u);
  EXPECT_EQ(file.pool.ints.size(), 2u);
  EXPECT_EQ(file.pool.ints[1], 7);
  EXPECT_EQ(file.pool.ns_sets.size(), 1u);
  EXPECT_EQ(file.methods.size(), 3u);
  EXPECT_EQ(file.instances.size(), 1u);
  EXPECT_EQ(file.classes.size(), 1u);
  EXPECT_EQ(file.bodies.size(), 3u);
  EXPECT_EQ(file.string_at(kStringFoo), "Foo");
  EXPECT_EQ(file.namespace_at(1), "flash.external");
  EXPECT_EQ(file.namespace_at(2), "");
  EXPECT_EQ(file.multiname_at(kMultinameExternalInterface), "flash.external::ExternalInterface");
  EXPECT_EQ(file.multiname_at(kMultinameCall), "flash.external::call");
  EXPECT_EQ(file.multiname_at(kMultinameTest), "Test");
  EXPECT_EQ(file.multiname_at(kMultinameVoid), "void");
  EXPECT_EQ(file.class_name(0), "Test");
  EXPECT_EQ(file.method_name(1), "onClick");
  ASSERT_NE(file.body_for(0), nullptr);
  EXPECT_EQ(file.body_for(0)->code.size(), 8u);
  EXPECT_EQ(file.body_for(0)->exceptions.size(), 0u);
  EXPECT_EQ(file.body_for(9), nullptr);
  EXPECT_EQ(file.status_name(AbcStatus::VarintTooLong), std::string{"varint too long"});
}

TEST(Abc, ParsesMethodOptionsParamNamesTraitsAndExceptions) {
  const AbcFile file = parse_or_fail(build_abc());
  const fo4::abc::AbcMethodInfo& handler = file.methods[1];
  EXPECT_EQ(handler.param_count, 1u);
  EXPECT_EQ(handler.param_types.size(), 1u);
  EXPECT_EQ(handler.return_type, kMultinameVoid);
  EXPECT_TRUE(handler.has_optional());
  EXPECT_TRUE(handler.has_param_names());
  EXPECT_FALSE(handler.needs_arguments());
  EXPECT_FALSE(handler.needs_activation());
  ASSERT_EQ(handler.options.size(), 1u);
  EXPECT_EQ(handler.options[0].kind, fo4::abc::AbcValueKind::Int);
  EXPECT_EQ(file.optional_value_at(handler.options[0]), "7");
  ASSERT_EQ(handler.param_names.size(), 1u);
  EXPECT_EQ(file.string_at(handler.param_names[0]), "click");

  const fo4::abc::AbcInstanceInfo& instance = file.instances[0];
  EXPECT_TRUE(instance.is_sealed());
  EXPECT_TRUE(instance.is_interface());
  EXPECT_FALSE(instance.is_final());
  ASSERT_EQ(instance.interfaces.size(), 1u);
  EXPECT_EQ(file.multiname_at(instance.interfaces[0]), "click");
  ASSERT_EQ(instance.traits.size(), 5u);
  EXPECT_EQ(instance.traits[0].kind, fo4::abc::AbcTraitKind::Method);
  EXPECT_EQ(instance.traits[0].method_index, 1u);
  EXPECT_EQ(instance.traits[0].disp_id, 0u);
  EXPECT_TRUE(instance.traits[0].has_metadata());
  EXPECT_FALSE(instance.traits[0].is_final());
  EXPECT_FALSE(instance.traits[0].is_override());
  ASSERT_EQ(instance.traits[0].metadata.size(), 1u);
  ASSERT_EQ(file.metadata.size(), 1u);
  EXPECT_EQ(instance.traits[0].metadata[0], 0u);
  EXPECT_EQ(file.string_at(file.metadata[0].name), "Foo");
  ASSERT_EQ(file.metadata[0].items.size(), 1u);
  EXPECT_EQ(file.string_at(file.metadata[0].items[0].key), "ExternalInterface");
  EXPECT_EQ(file.string_at(file.metadata[0].items[0].value), "call");
  EXPECT_EQ(instance.traits[1].kind, fo4::abc::AbcTraitKind::Slot);
  EXPECT_EQ(instance.traits[1].slot_id, 1u);
  EXPECT_EQ(instance.traits[1].type_name, kMultinameVoid);
  EXPECT_EQ(instance.traits[1].value_index, 0u);
  EXPECT_EQ(instance.traits[1].value_kind, fo4::abc::AbcValueKind::None);
  EXPECT_TRUE(instance.traits[1].has_metadata());
  ASSERT_EQ(instance.traits[1].metadata.size(), 1u);
  EXPECT_EQ(instance.traits[1].metadata[0], 0u);
  EXPECT_EQ(instance.traits[2].kind, fo4::abc::AbcTraitKind::Const);
  EXPECT_EQ(instance.traits[2].slot_id, 2u);
  EXPECT_EQ(instance.traits[2].value_kind, fo4::abc::AbcValueKind::Int);
  EXPECT_EQ(instance.traits[2].value_index, 1u);
  EXPECT_EQ(instance.traits[3].kind, fo4::abc::AbcTraitKind::Class);
  EXPECT_EQ(instance.traits[3].slot_id, 0u);
  EXPECT_EQ(instance.traits[3].class_index, 0u);
  EXPECT_FALSE(instance.traits[3].has_metadata());
  EXPECT_EQ(instance.traits[4].kind, fo4::abc::AbcTraitKind::Function);
  EXPECT_EQ(instance.traits[4].slot_id, 3u);
  EXPECT_EQ(instance.traits[4].class_index, 2u);
  EXPECT_TRUE(instance.traits[4].has_metadata());
  ASSERT_EQ(instance.traits[4].metadata.size(), 1u);

  const fo4::abc::AbcMethodBody* helper = file.body_for(2);
  ASSERT_NE(helper, nullptr);
  ASSERT_EQ(helper->exceptions.size(), 1u);
  EXPECT_EQ(helper->exceptions[0].from, 0u);
  EXPECT_EQ(helper->exceptions[0].to, 1u);
  EXPECT_EQ(helper->exceptions[0].exc_type, kMultinameTest);
  EXPECT_EQ(file.string_at(helper->exceptions[0].var_name), "helper");
}

TEST(Abc, ParsesEveryByteAndRejectsTrailingBytes) {
  const std::vector<std::uint8_t> bytes = build_abc();
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  std::vector<std::uint8_t> padded = bytes;
  padded.push_back(0x00);
  EXPECT_EQ(AbcFile::parse(padded, file), AbcStatus::TrailingBytes);
}

TEST(Abc, DisassemblesConstructorCallSite) {
  const AbcFile file = parse_or_fail(build_abc());
  const std::vector<std::uint8_t>& code = file.body_for(0)->code;
  std::vector<AbcInstruction> instructions;
  ASSERT_EQ(fo4::abc::decode_code(code, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 4u);
  EXPECT_EQ(instructions[0].offset, 0u);
  EXPECT_STREQ(instructions[0].mnemonic, "getlex");
  EXPECT_EQ(instructions[0].length, 2u);
  EXPECT_EQ(instructions[1].offset, 2u);
  EXPECT_STREQ(instructions[1].mnemonic, "pushstring");
  EXPECT_EQ(instructions[2].offset, 4u);
  EXPECT_STREQ(instructions[2].mnemonic, "callpropvoid");
  EXPECT_EQ(instructions[2].length, 3u);
  EXPECT_EQ(instructions[3].offset, 7u);
  EXPECT_STREQ(instructions[3].mnemonic, "returnvoid");
  ASSERT_EQ(instructions[2].operands.size(), 2u);
  EXPECT_EQ(instructions[2].operands[0].role, AbcOperandRole::Multiname);
  EXPECT_EQ(instructions[2].operands[0].value, kMultinameCall);
  EXPECT_EQ(instructions[2].operands[1].role, AbcOperandRole::Count);
  EXPECT_EQ(instructions[2].operands[1].value, 1u);
  EXPECT_EQ(fo4::abc::validate_operand_pools(file, instructions), AbcStatus::Ok);
  std::string listing;
  ASSERT_EQ(fo4::abc::disassemble(file, code, listing), AbcStatus::Ok);
  EXPECT_EQ(listing, "0: getlex flash.external::ExternalInterface\n"
                     "2: pushstring \"Foo\"\n"
                     "4: callpropvoid flash.external::call 1\n"
                     "7: returnvoid\n");
}

TEST(Abc, SummaryAnswersExternalInterfaceAndEventListenerQuestions) {
  const AbcFile file = parse_or_fail(build_abc());
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const auto& klass = summary.classes.front();
  EXPECT_EQ(klass.name, "Test");
  EXPECT_EQ(klass.super_name, "Object");
  ASSERT_EQ(klass.interfaces.size(), 1u);
  EXPECT_EQ(klass.interfaces[0], "click");
  ASSERT_EQ(klass.methods.size(), 2u);
  const auto& constructor = klass.methods[0];
  EXPECT_EQ(constructor.name, "<init>");
  EXPECT_EQ(constructor.return_type, "void");
  EXPECT_EQ(constructor.flags, 0u);
  EXPECT_EQ(constructor.instruction_count, 4u);
  ASSERT_EQ(constructor.multinames.size(), 2u);
  EXPECT_EQ(constructor.multinames[0], "flash.external::ExternalInterface");
  EXPECT_EQ(constructor.multinames[1], "flash.external::call");
  const AbcCallSite& external = only_call(constructor.calls);
  EXPECT_EQ(external.opcode, "callpropvoid");
  EXPECT_EQ(external.target, "flash.external::call");
  EXPECT_EQ(external.receiver, "flash.external::ExternalInterface");
  EXPECT_EQ(external.arg_count, 1u);
  EXPECT_TRUE(fo4::abc::is_external_interface_call(external));
  EXPECT_FALSE(fo4::abc::is_event_registration(external));
  ASSERT_EQ(external.literals.size(), 1u);
  EXPECT_EQ(external.literals[0], "Foo");
  EXPECT_TRUE(external.literal_unambiguous);

  const auto& listener = klass.methods[1];
  EXPECT_EQ(listener.name, "onClick");
  EXPECT_EQ(listener.return_type, "void");
  EXPECT_EQ(listener.instruction_count, 5u);
  const AbcCallSite& registration = only_call(listener.calls);
  EXPECT_EQ(registration.target, "addEventListener");
  EXPECT_EQ(registration.receiver, "handler");
  EXPECT_EQ(registration.arg_count, 2u);
  EXPECT_TRUE(fo4::abc::is_event_registration(registration));
  EXPECT_FALSE(fo4::abc::is_external_interface_call(registration));
  ASSERT_EQ(registration.literals.size(), 1u);
  EXPECT_EQ(registration.literals[0], "click");
  EXPECT_TRUE(registration.literal_unambiguous);

  ASSERT_EQ(summary.functions.size(), 1u);
  const auto& helper = summary.functions.front();
  EXPECT_EQ(helper.name, "helper");
  EXPECT_EQ(helper.instruction_count, 1u);
  EXPECT_TRUE(helper.calls.empty());
  EXPECT_TRUE(helper.multinames.empty());
  EXPECT_TRUE(summary.scripts.empty());
}

TEST(Abc, OpcodeTableMatchesAvm2Numbers) {
  const AbcOpcodeInfo* info = fo4::abc::opcode_info(0x02);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "nop");
  EXPECT_EQ(info->operand_count, 0u);
  info = fo4::abc::opcode_info(0x1B);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "lookupswitch");
  EXPECT_EQ(info->operands[0].encoding, AbcOperandEncoding::Switch);
  info = fo4::abc::opcode_info(0x24);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "pushbyte");
  EXPECT_EQ(info->operands[0].encoding, AbcOperandEncoding::S8);
  info = fo4::abc::opcode_info(0x40);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "newfunction");
  info = fo4::abc::opcode_info(0x4F);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "callpropvoid");
  EXPECT_EQ(info->operand_count, 2u);
  info = fo4::abc::opcode_info(0x80);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "coerce");
  info = fo4::abc::opcode_info(0xC0);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "increment_i");
  info = fo4::abc::opcode_info(0xD0);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "getlocal0");
  info = fo4::abc::opcode_info(0xEF);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "debug");
  EXPECT_EQ(info->operand_count, 4u);
  info = fo4::abc::opcode_info(0xF3);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "timestamp");
  EXPECT_EQ(fo4::abc::opcode_info(0x00), nullptr);
  EXPECT_EQ(fo4::abc::opcode_info(0x0A), nullptr);
  EXPECT_EQ(fo4::abc::opcode_info(0x0B), nullptr);
  EXPECT_EQ(fo4::abc::opcode_info(0x22), nullptr);
  EXPECT_EQ(fo4::abc::opcode_info(0x54), nullptr);
  EXPECT_EQ(fo4::abc::opcode_info(0x8F), nullptr);
  EXPECT_EQ(fo4::abc::opcode_info(0xF5), nullptr);
}

TEST(Abc, RejectsUnknownOpcode) {
  std::vector<AbcInstruction> instructions;
  EXPECT_EQ(fo4::abc::decode_code(std::vector<std::uint8_t>{0x0A}, instructions),
            AbcStatus::BadOpcode);
  EXPECT_EQ(fo4::abc::decode_code(std::vector<std::uint8_t>{0x47, 0x22}, instructions),
            AbcStatus::BadOpcode);
  EXPECT_EQ(fo4::abc::decode_code(std::vector<std::uint8_t>{0xF3, 0x47}, instructions),
            AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  EXPECT_STREQ(instructions[0].mnemonic, "timestamp");
}

TEST(Abc, RejectsTruncatedVarintAndTruncatedFile) {
  const std::vector<std::uint8_t> runaway{0x80, 0x80, 0x80, 0x80, 0x80};
  ByteReader reader(runaway);
  EXPECT_EQ(reader.u30(), 0u);
  EXPECT_EQ(reader.status(), AbcStatus::VarintTooLong);
  const std::vector<std::uint8_t> cut{0x80, 0x01};
  ByteReader short_reader(cut);
  EXPECT_EQ(short_reader.u30(), 0x80u);
  EXPECT_EQ(short_reader.status(), AbcStatus::Ok);
  const std::vector<std::uint8_t> dangling{0x80};
  ByteReader dangling_reader(dangling);
  EXPECT_EQ(dangling_reader.u30(), 0u);
  EXPECT_EQ(dangling_reader.status(), AbcStatus::Truncated);
  const std::vector<std::uint8_t> empty;
  ByteReader empty_reader(empty);
  EXPECT_EQ(empty_reader.status(), AbcStatus::Ok);
  EXPECT_EQ(empty_reader.u8(), 0u);
  EXPECT_EQ(empty_reader.status(), AbcStatus::Truncated);
  AbcFile file;
  const std::vector<std::uint8_t> truncated_file{0x10, 0x00, 0x2E, 0x00, 0x05};
  EXPECT_EQ(AbcFile::parse(truncated_file, file), AbcStatus::CountOutOfRange);
  std::vector<std::uint8_t> header = abc_header();
  EXPECT_EQ(AbcFile::parse(header, file), AbcStatus::Truncated);
}

TEST(Abc, RejectsConstantPoolIndexOutOfRange) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, 99);
  put_u8(code, 0x47);
  const AbcFile file = parse_or_fail(build_abc(code, tiny_listener_code()));
  std::vector<AbcInstruction> instructions;
  ASSERT_EQ(fo4::abc::decode_code(file.body_for(0)->code, instructions), AbcStatus::Ok);
  EXPECT_EQ(fo4::abc::validate_operand_pools(file, instructions), AbcStatus::IndexOutOfRange);
  std::string listing;
  EXPECT_EQ(fo4::abc::disassemble(file, file.body_for(0)->code, listing),
            AbcStatus::IndexOutOfRange);
  AbcSummary summary;
  EXPECT_EQ(fo4::abc::summarize(file, summary), AbcStatus::IndexOutOfRange);
}

TEST(Abc, RejectsBranchTargetOutOfRange) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x10);
  put_s24(code, 1000);
  put_u8(code, 0x47);
  std::vector<AbcInstruction> instructions;
  EXPECT_EQ(fo4::abc::decode_code(code, instructions), AbcStatus::BranchOutOfRange);
  std::vector<std::uint8_t> valid;
  put_u8(valid, 0x10);
  put_s24(valid, 0);
  put_u8(valid, 0x47);
  ASSERT_EQ(fo4::abc::decode_code(valid, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  EXPECT_EQ(instructions[0].operands[0].role, AbcOperandRole::Offset);
  EXPECT_EQ(instructions[0].operands[0].immediate, 0);
  EXPECT_EQ(instructions[0].operands[0].target, 4);
  std::vector<std::uint8_t> switch_code;
  put_u8(switch_code, 0x1B);
  put_s24(switch_code, 0);
  put_u30(switch_code, 1);
  put_s24(switch_code, 1);
  put_u8(switch_code, 0x47);
  ASSERT_EQ(fo4::abc::decode_code(switch_code, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  EXPECT_EQ(instructions[0].length, 8u);
  EXPECT_EQ(instructions[0].operands[0].target, 8);
  ASSERT_EQ(instructions[0].operands[0].case_targets.size(), 1u);
  EXPECT_EQ(instructions[0].operands[0].case_targets[0], 9);
  std::vector<std::uint8_t> bad_switch;
  put_u8(bad_switch, 0x1B);
  put_s24(bad_switch, 0);
  put_u30(bad_switch, 1);
  put_s24(bad_switch, 900);
  put_u8(bad_switch, 0x47);
  EXPECT_EQ(fo4::abc::decode_code(bad_switch, instructions), AbcStatus::BranchOutOfRange);
}

TEST(Abc, RejectsBadNamespaceAndMultinameKinds) {
  AbcFile file;
  std::vector<std::uint8_t> bad_namespace = abc_header();
  put_u30(bad_namespace, 2);
  put_u8(bad_namespace, 0x99);
  put_u30(bad_namespace, 0);
  EXPECT_EQ(AbcFile::parse(bad_namespace, file), AbcStatus::BadNamespaceKind);
  std::vector<std::uint8_t> bad_multiname = abc_header();
  put_u30(bad_multiname, 1);
  put_u30(bad_multiname, 0);
  put_u30(bad_multiname, 2);
  put_u8(bad_multiname, 0x99);
  EXPECT_EQ(AbcFile::parse(bad_multiname, file), AbcStatus::BadMultinameKind);
  std::vector<std::uint8_t> bad_multiname_index = abc_header();
  put_u30(bad_multiname_index, 1);
  put_u30(bad_multiname_index, 0);
  put_u30(bad_multiname_index, 2);
  put_u8(bad_multiname_index, 0x07);
  put_u30(bad_multiname_index, 7);
  put_u30(bad_multiname_index, 0);
  EXPECT_EQ(AbcFile::parse(bad_multiname_index, file), AbcStatus::IndexOutOfRange);
}

TEST(Abc, RejectsBadTraitKindAndTraitMetadataIndex) {
  AbcFile file;
  EXPECT_EQ(AbcFile::parse(minimal_instance_file(0x40, 0), file), AbcStatus::Ok);
  ASSERT_EQ(file.instances.size(), 1u);
  ASSERT_EQ(file.instances[0].traits.size(), 1u);
  EXPECT_EQ(file.instances[0].traits[0].kind, fo4::abc::AbcTraitKind::Slot);
  ASSERT_EQ(file.instances[0].traits[0].metadata.size(), 1u);
  EXPECT_EQ(file.instances[0].traits[0].metadata[0], 0u);
  EXPECT_EQ(AbcFile::parse(minimal_instance_file(0x40, 5), file), AbcStatus::IndexOutOfRange);
  EXPECT_EQ(AbcFile::parse(minimal_instance_file(0x0F, 0), file), AbcStatus::BadTraitKind);
}

TEST(AbcCorpus, EveryExtractedAbcParsesDisassemblesAndSummarizes) {
  const char* root = std::getenv("FO4_ABC_CORPUS");
  if (root == nullptr) {
    GTEST_SKIP() << "set FO4_ABC_CORPUS to a directory of extracted .abc files";
  }
  std::vector<std::filesystem::path> files;
  std::error_code error;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(root, error)) {
    if (entry.is_regular_file() && entry.path().extension() == ".abc") {
      files.push_back(entry.path());
    }
  }
  if (files.empty()) {
    GTEST_SKIP() << "no .abc files under " << root;
  }
  std::sort(files.begin(), files.end());
  std::size_t class_total = 0;
  for (const std::filesystem::path& path : files) {
    SCOPED_TRACE(path.string());
    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input.is_open());
    std::vector<std::uint8_t> bytes;
    char ch = 0;
    while (input.get(ch)) {
      bytes.push_back(static_cast<std::uint8_t>(ch));
    }
    AbcFile file;
    ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
    EXPECT_EQ(file.major_version, fo4::abc::kAbcMajorVersion);
    EXPECT_EQ(file.minor_version, fo4::abc::kAbcMinorVersion);
    for (const fo4::abc::AbcMethodBody& body : file.bodies) {
      std::vector<AbcInstruction> instructions;
      ASSERT_EQ(fo4::abc::decode_code(body.code, instructions), AbcStatus::Ok);
      ASSERT_EQ(fo4::abc::validate_operand_pools(file, instructions), AbcStatus::Ok);
    }
    AbcSummary summary;
    ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
    EXPECT_EQ(summary.classes.size(), file.instances.size());
    class_total += summary.classes.size();
  }
  EXPECT_GT(class_total, 0u);
}
