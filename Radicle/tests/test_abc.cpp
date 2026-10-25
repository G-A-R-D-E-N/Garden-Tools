#include "abc_test_support.hpp"
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
#include <utility>
#include <vector>

using fo4::abc::AbcException;
using fo4::abc::AbcFile;
using fo4::abc::AbcInstruction;
using fo4::abc::AbcMethodBody;
using fo4::abc::AbcOpcodeInfo;
using fo4::abc::AbcOperandEncoding;
using fo4::abc::AbcOperandRole;
using fo4::abc::AbcStatus;
using fo4::abc::AbcSummary;
using fo4::abc::ByteReader;
using namespace fo4::test;

namespace {

std::vector<std::uint8_t> empty_pools_header() {
  std::vector<std::uint8_t> out;
  put_u16(out, fo4::abc::kAbcMinorVersion);
  put_u16(out, fo4::abc::kAbcMajorVersion);
  for (int pool = 0; pool < 6; ++pool) {
    put_u30(out, 0);
  }
  put_u30(out, 1);
  return out;
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
  EXPECT_EQ(file.pool.multinames.size(), 15u);
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
  EXPECT_EQ(file.multiname_at(kMultinameTypeName), "Object<Test>");
  EXPECT_EQ(file.class_name(0), "Test");
  EXPECT_EQ(file.method_name(1), "onClick");
  ASSERT_NE(file.body_for(0), nullptr);
  EXPECT_EQ(file.body_for(0)->code.size(), 8u);
  EXPECT_EQ(file.body_for(0)->exceptions.size(), 0u);
  EXPECT_EQ(file.body_for(9), nullptr);
  EXPECT_EQ(status_name(AbcStatus::VarintTooLong), std::string{"varint too long"});
}

TEST(Abc, ParsesMethodOptionsParamNamesTraitsAndExceptions) {
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(), file), AbcStatus::Ok);
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
  EXPECT_EQ(instance.traits[1].value_kind, fo4::abc::AbcValueKind::Undefined);
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
  EXPECT_EQ(file.multiname_at(helper->exceptions[0].var_name), "helper");
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
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(), file), AbcStatus::Ok);
  ASSERT_NE(file.body_for(0), nullptr);
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
  std::vector<std::uint8_t> short_code;
  put_u8(short_code, 0x25);
  put_u30(short_code, 0xFFFBu);
  std::vector<AbcInstruction> short_instructions;
  ASSERT_EQ(fo4::abc::decode_code(short_code, short_instructions), AbcStatus::Ok);
  ASSERT_EQ(short_instructions.size(), 1u);
  ASSERT_EQ(short_instructions[0].operands.size(), 1u);
  EXPECT_EQ(short_instructions[0].operands[0].immediate, -5);
  std::string short_listing;
  ASSERT_EQ(fo4::abc::disassemble(file, short_code, short_listing), AbcStatus::Ok);
  EXPECT_EQ(short_listing, "0: pushshort -5\n");
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
  info = fo4::abc::opcode_info(0x25);
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->mnemonic, "pushshort");
  EXPECT_EQ(info->operands[0].encoding, AbcOperandEncoding::U30);
  EXPECT_EQ(info->operands[0].role, AbcOperandRole::Immediate);
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
  const std::pair<std::uint8_t, std::uint8_t> undefined[] = {
      {0x00, 0x00}, {0x0A, 0x0B}, {0x22, 0x22}, {0x33, 0x34}, {0x3F, 0x3F}, {0x4B, 0x4B},
      {0x4D, 0x4D}, {0x54, 0x54}, {0x5B, 0x5C}, {0x79, 0x7F}, {0x8A, 0x8F}, {0x98, 0x9F},
      {0xB5, 0xBF}, {0xC8, 0xCF}, {0xD8, 0xEE}, {0xF4, 0xFF}};
  for (const auto& [first, last] : undefined) {
    for (std::uint32_t opcode = first; opcode <= last; ++opcode) {
      EXPECT_EQ(fo4::abc::opcode_info(static_cast<std::uint8_t>(opcode)), nullptr)
          << "opcode 0x" << std::hex << opcode;
    }
  }
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
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  std::vector<AbcInstruction> instructions;
  ASSERT_NE(file.body_for(0), nullptr);
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
  put_s24(switch_code, 0);
  put_u8(switch_code, 0x47);
  ASSERT_EQ(fo4::abc::decode_code(switch_code, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  EXPECT_EQ(instructions[0].length, 11u);
  EXPECT_EQ(instructions[0].operands[0].target, 0);
  ASSERT_EQ(instructions[0].operands[0].case_targets.size(), 2u);
  EXPECT_EQ(instructions[0].operands[0].case_targets[0], 1);
  EXPECT_EQ(instructions[0].operands[0].case_targets[1], 0);
  std::vector<std::uint8_t> tail_switch;
  put_u8(tail_switch, 0x1B);
  put_s24(tail_switch, 8);
  put_u30(tail_switch, 0);
  put_s24(tail_switch, 8);
  ASSERT_EQ(fo4::abc::decode_code(tail_switch, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 1u);
  EXPECT_EQ(instructions[0].length, 8u);
  EXPECT_EQ(instructions[0].operands[0].target, 8);
  ASSERT_EQ(instructions[0].operands[0].case_targets.size(), 1u);
  EXPECT_EQ(instructions[0].operands[0].case_targets[0], 8);
  std::vector<std::uint8_t> short_switch;
  put_u8(short_switch, 0x1B);
  put_s24(short_switch, 0);
  put_u30(short_switch, 1);
  put_s24(short_switch, 1);
  EXPECT_EQ(fo4::abc::decode_code(short_switch, instructions), AbcStatus::CountOutOfRange);
  std::vector<std::uint8_t> bad_switch;
  put_u8(bad_switch, 0x1B);
  put_s24(bad_switch, 0);
  put_u30(bad_switch, 1);
  put_s24(bad_switch, 0);
  put_s24(bad_switch, 900);
  put_u8(bad_switch, 0x47);
  EXPECT_EQ(fo4::abc::decode_code(bad_switch, instructions), AbcStatus::BranchOutOfRange);
  std::vector<std::uint8_t> dead_jump;
  put_u8(dead_jump, 0x47);
  put_u8(dead_jump, 0x10);
  put_s24(dead_jump, 1000);
  ASSERT_EQ(fo4::abc::decode_code(dead_jump, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  ASSERT_EQ(instructions[1].operands.size(), 1u);
  EXPECT_EQ(instructions[1].operands[0].target, 1005);
}

TEST(Abc, HandlerTargetsAreReachabilityRootsAndValidated) {
  std::vector<AbcInstruction> instructions;
  std::vector<std::uint8_t> catch_only_jump;
  put_u8(catch_only_jump, 0x47);
  put_u8(catch_only_jump, 0x10);
  put_s24(catch_only_jump, 1000);
  const std::vector<AbcException> handler{
      AbcException{0, 1, 1, kMultinameTest, kStringHelper}};
  EXPECT_EQ(fo4::abc::decode_code(catch_only_jump, instructions, handler),
            AbcStatus::BranchOutOfRange);
  ASSERT_EQ(fo4::abc::decode_code(catch_only_jump, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  EXPECT_EQ(instructions[1].operands[0].target, 1005);
  std::vector<std::uint8_t> reachable_catch;
  put_u8(reachable_catch, 0x47);
  put_u8(reachable_catch, 0x10);
  put_s24(reachable_catch, 0);
  EXPECT_EQ(fo4::abc::decode_code(reachable_catch, instructions, handler), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 2u);
  EXPECT_EQ(instructions[1].operands[0].target, 5);
  const std::vector<AbcException> past_end{
      AbcException{0, 1, 99, kMultinameTest, kStringHelper}};
  EXPECT_EQ(fo4::abc::decode_code(catch_only_jump, instructions, past_end),
            AbcStatus::BranchOutOfRange);
  const std::vector<AbcException> at_end{
      AbcException{0, 1, 5, kMultinameTest, kStringHelper}};
  EXPECT_EQ(fo4::abc::decode_code(catch_only_jump, instructions, at_end),
            AbcStatus::BranchOutOfRange);
  std::vector<std::uint8_t> two_byte_head;
  put_u8(two_byte_head, 0x60);
  put_u30(two_byte_head, kMultinameExternalInterface);
  put_u8(two_byte_head, 0x47);
  const std::vector<AbcException> mid_instruction{
      AbcException{0, 1, 1, kMultinameTest, kStringHelper}};
  EXPECT_EQ(fo4::abc::decode_code(two_byte_head, instructions, mid_instruction),
            AbcStatus::BranchOutOfRange);
  const std::vector<AbcException> instruction_start{
      AbcException{0, 1, 2, kMultinameTest, kStringHelper}};
  EXPECT_EQ(fo4::abc::decode_code(two_byte_head, instructions, instruction_start),
            AbcStatus::Ok);
  AbcFile file;
  std::string listing;
  EXPECT_EQ(fo4::abc::disassemble(file, catch_only_jump, listing, handler),
            AbcStatus::BranchOutOfRange);
  EXPECT_EQ(fo4::abc::disassemble(file, reachable_catch, listing, handler), AbcStatus::Ok);
  EXPECT_NE(listing.find("1: jump 5"), std::string::npos);
  AbcMethodBody body;
  body.code = catch_only_jump;
  body.exceptions = handler;
  file.methods.resize(1);
  file.bodies.push_back(body);
  AbcSummary summary;
  EXPECT_EQ(fo4::abc::summarize(file, summary), AbcStatus::BranchOutOfRange);
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

TEST(Abc, ReadsProtectedNamespaceAndAlwaysPresentInterfaceCount) {
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(minimal_instance_file(0x40, 0, 0x08), file), AbcStatus::Ok);
  ASSERT_EQ(file.instances.size(), 1u);
  const fo4::abc::AbcInstanceInfo& instance = file.instances[0];
  EXPECT_TRUE(instance.has_protected_ns());
  EXPECT_EQ(instance.protected_ns, 1u);
  EXPECT_TRUE(instance.interfaces.empty());
  ASSERT_EQ(instance.traits.size(), 1u);
  EXPECT_EQ(instance.traits[0].kind, fo4::abc::AbcTraitKind::Slot);
}

TEST(Abc, RejectsEveryStructuralIndexPastItsPool) {
  const std::pair<AbcBadIndex, const char*> cases[] = {
      {AbcBadIndex::InstanceName, "instance name"},
      {AbcBadIndex::InstanceNameAtLimit, "instance name at its pool limit"},
      {AbcBadIndex::InstanceSuper, "instance super"},
      {AbcBadIndex::InstanceInterface, "instance interface"},
      {AbcBadIndex::InstanceIinit, "instance iinit"},
      {AbcBadIndex::InstanceIinitAtLimit, "instance iinit at its pool limit"},
      {AbcBadIndex::InstanceProtectedNs, "instance protected namespace"},
      {AbcBadIndex::ClassInit, "class cinit"},
      {AbcBadIndex::TraitName, "trait name"},
      {AbcBadIndex::TraitTypeName, "trait type name"},
      {AbcBadIndex::TraitMethodIndex, "trait method index"},
      {AbcBadIndex::TraitClassIndex, "trait class index"},
      {AbcBadIndex::TraitFunctionIndex, "trait function index"},
      {AbcBadIndex::MethodReturnType, "method return type"},
      {AbcBadIndex::MethodParamType, "method parameter type"},
      {AbcBadIndex::ExceptionType, "exception type"},
      {AbcBadIndex::ExceptionVarName, "exception variable name"},
      {AbcBadIndex::ScriptInit, "script init"},
      {AbcBadIndex::BodyMethod, "body method"},
      {AbcBadIndex::MethodNameAtLimit, "method name"},
      {AbcBadIndex::MethodParamNameAtLimit, "method parameter name"},
      {AbcBadIndex::MethodOptionIndexAtLimit, "method option index"},
      {AbcBadIndex::MetadataNameAtLimit, "metadata name"},
      {AbcBadIndex::MetadataItemKeyAtLimit, "metadata item key"},
      {AbcBadIndex::MetadataItemValueAtLimit, "metadata item value"},
      {AbcBadIndex::TraitValueIndexAtLimit, "trait value index"},
      {AbcBadIndex::TraitValueIndexStringAtLimit, "trait string value index"},
      {AbcBadIndex::TraitValueIndexUIntAtLimit, "trait uint value index"},
      {AbcBadIndex::TraitValueIndexDoubleAtLimit, "trait double value index"},
      {AbcBadIndex::TraitValueIndexNamespaceAtLimit, "trait namespace value index"},
  };
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(), file), AbcStatus::Ok);
  for (const auto& [bad, field] : cases) {
    SCOPED_TRACE(field);
    EXPECT_EQ(AbcFile::parse(build_abc(tiny_constructor_code(), tiny_listener_code(), bad), file),
              AbcStatus::IndexOutOfRange);
  }
}

TEST(Abc, AcceptsEveryStructuralIndexAtItsLastEntry) {
  const std::pair<AbcBadIndex, const char*> cases[] = {
      {AbcBadIndex::MethodNameAtLast, "method name"},
      {AbcBadIndex::MethodParamNameAtLast, "method parameter name"},
      {AbcBadIndex::MethodOptionIndexAtLast, "method option index"},
      {AbcBadIndex::MethodOptionIndexZero, "method option index 0"},
      {AbcBadIndex::MetadataNameAtLast, "metadata name"},
      {AbcBadIndex::MetadataItemKeyAtLast, "metadata item key"},
      {AbcBadIndex::MetadataItemValueAtLast, "metadata item value"},
      {AbcBadIndex::TraitValueIndexAtLast, "trait value index"},
      {AbcBadIndex::TraitValueIndexStringAtLast, "trait string value index"},
      {AbcBadIndex::TraitValueIndexNamespaceAtLast, "trait namespace value index"},
  };
  AbcFile file;
  for (const auto& [bad, field] : cases) {
    SCOPED_TRACE(field);
    ASSERT_EQ(AbcFile::parse(build_abc(tiny_constructor_code(), tiny_listener_code(), bad), file),
              AbcStatus::Ok);
    switch (bad) {
      case AbcBadIndex::MethodNameAtLast:
        EXPECT_EQ(file.methods[0].name, kStringCount - 1);
        EXPECT_EQ(file.methods[1].name, kStringCount - 1);
        break;
      case AbcBadIndex::MethodParamNameAtLast:
        ASSERT_EQ(file.methods[1].param_names.size(), 1u);
        EXPECT_EQ(file.methods[1].param_names[0], kStringCount - 1);
        break;
      case AbcBadIndex::MethodOptionIndexAtLast:
        ASSERT_EQ(file.methods[1].options.size(), 1u);
        EXPECT_EQ(file.methods[1].options[0].index, kIntCount - 1);
        break;
      case AbcBadIndex::MethodOptionIndexZero:
        ASSERT_EQ(file.methods[1].options.size(), 1u);
        EXPECT_EQ(file.methods[1].options[0].index, 0u);
        break;
      case AbcBadIndex::MetadataNameAtLast:
        ASSERT_EQ(file.metadata.size(), 1u);
        EXPECT_EQ(file.metadata[0].name, kStringCount - 1);
        break;
      case AbcBadIndex::MetadataItemKeyAtLast:
        ASSERT_EQ(file.metadata.size(), 1u);
        ASSERT_EQ(file.metadata[0].items.size(), 1u);
        EXPECT_EQ(file.metadata[0].items[0].key, kStringCount - 1);
        break;
      case AbcBadIndex::MetadataItemValueAtLast:
        ASSERT_EQ(file.metadata.size(), 1u);
        ASSERT_EQ(file.metadata[0].items.size(), 1u);
        EXPECT_EQ(file.metadata[0].items[0].value, kStringCount - 1);
        break;
      case AbcBadIndex::TraitValueIndexAtLast:
        EXPECT_EQ(file.instances[0].traits[2].value_index, kIntCount - 1);
        break;
      case AbcBadIndex::TraitValueIndexStringAtLast:
        EXPECT_EQ(file.instances[0].traits[2].value_index, kStringCount - 1);
        break;
      case AbcBadIndex::TraitValueIndexNamespaceAtLast:
        EXPECT_EQ(file.instances[0].traits[2].value_index, kNamespaceCount - 1);
        break;
      default:
        break;
    }
  }
}

TEST(Abc, AcceptsZeroIndicesIntoEmptyStringPool) {
  std::vector<std::uint8_t> bytes = empty_pools_header();
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u8(bytes, 0x80);
  put_u30(bytes, 0);
  put_u30(bytes, 1);
  put_u30(bytes, 0);
  put_u30(bytes, 1);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  ASSERT_EQ(file.methods.size(), 1u);
  EXPECT_EQ(file.methods[0].name, 0u);
  ASSERT_EQ(file.methods[0].param_names.size(), 1u);
  EXPECT_EQ(file.methods[0].param_names[0], 0u);
  ASSERT_EQ(file.metadata.size(), 1u);
  EXPECT_EQ(file.metadata[0].name, 0u);
  ASSERT_EQ(file.metadata[0].items.size(), 1u);
  EXPECT_EQ(file.metadata[0].items[0].key, 0u);
  EXPECT_EQ(file.metadata[0].items[0].value, 0u);
}

TEST(Abc, AcceptsZeroIndexOptionInEmptyIntPoolAndAnyIndexForPoollessKind) {
  std::vector<std::uint8_t> bytes = empty_pools_header();
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u8(bytes, 0x08);
  put_u30(bytes, 2);
  put_u30(bytes, 0);
  put_u8(bytes, 0x03);
  put_u30(bytes, kBadIndex);
  put_u8(bytes, 0x0B);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  ASSERT_EQ(file.methods.size(), 1u);
  ASSERT_EQ(file.methods[0].options.size(), 2u);
  EXPECT_EQ(file.methods[0].options[0].kind, fo4::abc::AbcValueKind::Int);
  EXPECT_EQ(file.methods[0].options[0].index, 0u);
  EXPECT_EQ(file.methods[0].options[1].kind, fo4::abc::AbcValueKind::True);
  EXPECT_EQ(file.methods[0].options[1].index, kBadIndex);
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
      ASSERT_EQ(fo4::abc::decode_code(body.code, instructions, body.exceptions), AbcStatus::Ok);
      ASSERT_EQ(fo4::abc::validate_operand_pools(file, instructions), AbcStatus::Ok);
    }
    AbcSummary summary;
    ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
    EXPECT_EQ(summary.classes.size(), file.instances.size());
    class_total += summary.classes.size();
  }
  EXPECT_GT(class_total, 0u);
}
