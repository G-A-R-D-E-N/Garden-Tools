#include "abc_test_support.hpp"
#include "fo4/abc/abc.hpp"
#include "fo4/abc/opcodes.hpp"
#include "fo4/abc/summary.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using fo4::abc::AbcArgumentKind;
using fo4::abc::AbcCallSite;
using fo4::abc::AbcFile;
using fo4::abc::AbcInstruction;
using fo4::abc::AbcStatus;
using fo4::abc::AbcSummary;
using namespace fo4::test;

namespace {

const AbcCallSite& only_call(const std::vector<AbcCallSite>& calls) {
  EXPECT_EQ(calls.size(), 1u);
  static const AbcCallSite missing;
  return calls.empty() ? missing : calls.front();
}
}

TEST(Abc, SummaryAnswersExternalInterfaceAndEventListenerQuestions) {
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(), file), AbcStatus::Ok);
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
  ASSERT_EQ(external.arguments.size(), 1u);
  EXPECT_EQ(external.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(external.arguments[0].value, "Foo");
  EXPECT_TRUE(external.event_name.empty());
  EXPECT_TRUE(external.handler.empty());

  const auto& listener = klass.methods[1];
  EXPECT_EQ(listener.name, "onClick");
  EXPECT_EQ(listener.return_type, "void");
  EXPECT_EQ(listener.instruction_count, 5u);
  const AbcCallSite& registration = only_call(listener.calls);
  EXPECT_EQ(registration.target, "addEventListener");
  EXPECT_EQ(registration.receiver, "addEventListener");
  EXPECT_EQ(registration.arg_count, 2u);
  EXPECT_TRUE(fo4::abc::is_event_registration(registration));
  EXPECT_FALSE(fo4::abc::is_external_interface_call(registration));
  ASSERT_EQ(registration.literals.size(), 1u);
  EXPECT_EQ(registration.literals[0], "click");
  EXPECT_TRUE(registration.literal_unambiguous);
  ASSERT_EQ(registration.arguments.size(), 2u);
  EXPECT_EQ(registration.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(registration.arguments[0].value, "click");
  EXPECT_EQ(registration.arguments[1].kind, AbcArgumentKind::Named);
  EXPECT_EQ(registration.arguments[1].value, "handler");
  EXPECT_EQ(registration.event_name, "click");
  EXPECT_EQ(registration.handler, "handler");

  ASSERT_EQ(summary.functions.size(), 1u);
  const auto& helper = summary.functions.front();
  EXPECT_EQ(helper.name, "helper");
  EXPECT_EQ(helper.instruction_count, 1u);
  EXPECT_TRUE(helper.calls.empty());
  EXPECT_TRUE(helper.multinames.empty());
  EXPECT_TRUE(summary.scripts.empty());
}

TEST(Abc, MapsTwoStringArgumentsToTheirOwnSlots) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  ASSERT_EQ(summary.classes[0].methods.size(), 2u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.target, "flash.external::call");
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  ASSERT_EQ(site.arg_count, 2u);
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "Foo");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[1].value, "click");
  ASSERT_EQ(site.literals.size(), 2u);
  EXPECT_FALSE(site.literal_unambiguous);
}

TEST(Abc, LeavesBranchJoinedArgumentUnknown) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x12);
  put_s24(code, 6);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x10);
  put_s24(code, 2);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 1);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  std::vector<AbcInstruction> instructions;
  ASSERT_NE(file.body_for(0), nullptr);
  ASSERT_EQ(fo4::abc::decode_code(file.body_for(0)->code, instructions), AbcStatus::Ok);
  ASSERT_EQ(instructions.size(), 7u);
  ASSERT_STREQ(instructions[1].mnemonic, "iffalse");
  EXPECT_EQ(instructions[1].operands[0].target, 12);
  ASSERT_STREQ(instructions[3].mnemonic, "jump");
  EXPECT_EQ(instructions[3].operands[0].target, 14);
  EXPECT_EQ(instructions[5].offset, 14u);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  ASSERT_EQ(site.arg_count, 1u);
  ASSERT_EQ(site.arguments.size(), 1u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Unknown);
  EXPECT_TRUE(site.arguments[0].value.empty());
  EXPECT_TRUE(site.literals.empty());
  EXPECT_FALSE(site.literal_unambiguous);

  std::vector<std::uint8_t> no_jump_code;
  put_u8(no_jump_code, 0x60);
  put_u30(no_jump_code, kMultinameExternalInterface);
  put_u8(no_jump_code, 0x13);
  put_s24(no_jump_code, 2);
  put_u8(no_jump_code, 0x60);
  put_u30(no_jump_code, kMultinameHandler);
  put_u8(no_jump_code, 0x2C);
  put_u30(no_jump_code, kStringFoo);
  put_u8(no_jump_code, 0x4F);
  put_u30(no_jump_code, kMultinameCall);
  put_u30(no_jump_code, 1);
  put_u8(no_jump_code, 0x47);
  AbcFile no_jump_file;
  ASSERT_EQ(AbcFile::parse(build_abc(no_jump_code, tiny_listener_code()), no_jump_file),
            AbcStatus::Ok);
  std::vector<AbcInstruction> no_jump_instructions;
  ASSERT_NE(no_jump_file.body_for(0), nullptr);
  ASSERT_EQ(fo4::abc::decode_code(no_jump_file.body_for(0)->code, no_jump_instructions),
            AbcStatus::Ok);
  ASSERT_EQ(no_jump_instructions.size(), 6u);
  ASSERT_STREQ(no_jump_instructions[1].mnemonic, "ifeq");
  EXPECT_EQ(no_jump_instructions[1].operands[0].target, 8);
  EXPECT_EQ(no_jump_instructions[3].offset, 8u);
  AbcSummary no_jump_summary;
  ASSERT_EQ(fo4::abc::summarize(no_jump_file, no_jump_summary), AbcStatus::Ok);
  ASSERT_EQ(no_jump_summary.classes.size(), 1u);
  const AbcCallSite& no_jump_site = only_call(no_jump_summary.classes[0].methods[0].calls);
  EXPECT_TRUE(no_jump_site.receiver.empty());
  ASSERT_EQ(no_jump_site.arguments.size(), 1u);
  EXPECT_EQ(no_jump_site.arguments[0].kind, AbcArgumentKind::Unknown);
}

TEST(Abc, LeavesNestedCallResultArgumentUnknown) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameAddEventListener);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x46);
  put_u30(code, kMultinameCall);
  put_u30(code, 1);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameAddEventListener);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const std::vector<AbcCallSite>& calls = summary.classes[0].methods[0].calls;
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[0].target, "flash.external::call");
  EXPECT_EQ(calls[0].receiver, "flash.external::ExternalInterface");
  ASSERT_EQ(calls[0].arg_count, 1u);
  ASSERT_EQ(calls[0].arguments.size(), 1u);
  EXPECT_EQ(calls[0].arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(calls[0].arguments[0].value, "Foo");
  const AbcCallSite& registration = calls[1];
  EXPECT_EQ(registration.target, "addEventListener");
  EXPECT_EQ(registration.receiver, "addEventListener");
  ASSERT_EQ(registration.arg_count, 2u);
  ASSERT_EQ(registration.arguments.size(), 2u);
  EXPECT_EQ(registration.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(registration.arguments[0].value, "click");
  EXPECT_EQ(registration.arguments[1].kind, AbcArgumentKind::Unknown);
  EXPECT_TRUE(registration.arguments[1].value.empty());
  EXPECT_EQ(registration.event_name, "click");
  EXPECT_TRUE(registration.handler.empty());
  ASSERT_EQ(registration.literals.size(), 1u);
  EXPECT_EQ(registration.literals[0], "click");
  EXPECT_TRUE(registration.literal_unambiguous);
}

TEST(Abc, PopsCallStaticReceiverWithItsArgument) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x44);
  put_u30(code, 1);
  put_u30(code, 1);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 1);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.opcode, "callpropvoid");
  EXPECT_EQ(site.target, "flash.external::call");
  EXPECT_EQ(site.receiver, "Foo");
  EXPECT_FALSE(fo4::abc::is_external_interface_call(site));
  ASSERT_EQ(site.arguments.size(), 1u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Unknown);
}

TEST(Abc, KeepsLiteralArgumentsAcrossPlainOpcodes) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0xD0);
  put_u8(code, 0x66);
  put_u30(code, kMultinameHandler);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x60);
  put_u30(code, kMultinameHandler);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameAddEventListener);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.target, "addEventListener");
  EXPECT_TRUE(site.receiver.empty());
  EXPECT_TRUE(fo4::abc::is_event_registration(site));
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "click");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Named);
  EXPECT_EQ(site.arguments[1].value, "handler");
  EXPECT_EQ(site.event_name, "click");
  EXPECT_EQ(site.handler, "handler");
  ASSERT_EQ(site.literals.size(), 1u);
  EXPECT_EQ(site.literals[0], "click");
  EXPECT_TRUE(site.literal_unambiguous);
}

TEST(Abc, MapsEveryPushFormToItsLiteralKind) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2D);
  put_u30(code, 1);
  put_u8(code, 0x2E);
  put_u30(code, 1);
  put_u8(code, 0x2F);
  put_u30(code, 1);
  put_u8(code, 0x26);
  put_u8(code, 0x27);
  put_u8(code, 0x20);
  put_u8(code, 0x21);
  put_u8(code, 0x28);
  put_u8(code, 0x25);
  put_u30(code, 0xFFFBu);
  put_u8(code, 0x24);
  put_u8(code, 0xFDu);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 10);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  EXPECT_EQ(site.arg_count, 10u);
  const std::vector<std::string> expected{"7",    "9",         "1.500000", "true", "false",
                                          "null", "undefined", "NaN",      "-5",   "-3"};
  ASSERT_EQ(site.arguments.size(), expected.size());
  for (std::size_t slot = 0; slot < expected.size(); ++slot) {
    EXPECT_EQ(site.arguments[slot].kind, AbcArgumentKind::Literal) << "slot " << slot;
    EXPECT_EQ(site.arguments[slot].value, expected[slot]) << "slot " << slot;
  }
  EXPECT_EQ(site.literals, expected);
}

TEST(Abc, AsCastInsideExternalInterfaceArgumentKeepsReceiver) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0xD1);
  put_u8(code, 0x60);
  put_u30(code, kMultinameObject);
  put_u8(code, 0x87);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.target, "flash.external::call");
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  EXPECT_TRUE(fo4::abc::is_external_interface_call(site));
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "Foo");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Unknown);
}

TEST(Abc, PlainAstypeInsideExternalInterfaceArgumentStaysOneSlot) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0xD1);
  put_u8(code, 0x86);
  put_u30(code, kMultinameObject);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "Foo");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Unknown);
}

TEST(Abc, AsTypeLateOfLiteralInsideExternalInterfaceArgumentLeavesItUnknown) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0x2C);
  put_u30(code, kStringClick);
  put_u8(code, 0x60);
  put_u30(code, kMultinameObject);
  put_u8(code, 0x87);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "Foo");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Unknown);
}

TEST(Abc, RuntimeNameLookupInsideExternalInterfaceArgumentKeepsReceiver) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0xD1);
  put_u8(code, 0xD2);
  put_u8(code, 0x66);
  put_u30(code, kMultinameRuntimeName);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const AbcCallSite& site = only_call(summary.classes[0].methods[0].calls);
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  EXPECT_TRUE(fo4::abc::is_external_interface_call(site));
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "Foo");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Unknown);
}

TEST(Abc, RuntimeNameCallInsideExternalInterfaceArgumentKeepsReceiver) {
  std::vector<std::uint8_t> code;
  put_u8(code, 0x60);
  put_u30(code, kMultinameExternalInterface);
  put_u8(code, 0x2C);
  put_u30(code, kStringFoo);
  put_u8(code, 0xD1);
  put_u8(code, 0xD2);
  put_u8(code, 0xD3);
  put_u8(code, 0x46);
  put_u30(code, kMultinameRuntimeName);
  put_u30(code, 1);
  put_u8(code, 0x4F);
  put_u30(code, kMultinameCall);
  put_u30(code, 2);
  put_u8(code, 0x47);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(build_abc(code, tiny_listener_code()), file), AbcStatus::Ok);
  AbcSummary summary;
  ASSERT_EQ(fo4::abc::summarize(file, summary), AbcStatus::Ok);
  ASSERT_EQ(summary.classes.size(), 1u);
  const auto& calls = summary.classes[0].methods[0].calls;
  ASSERT_EQ(calls.size(), 2u);
  EXPECT_EQ(calls[0].opcode, "callproperty");
  EXPECT_TRUE(calls[0].receiver.empty());
  const AbcCallSite& site = calls[1];
  EXPECT_EQ(site.target, "flash.external::call");
  EXPECT_EQ(site.receiver, "flash.external::ExternalInterface");
  EXPECT_TRUE(fo4::abc::is_external_interface_call(site));
  ASSERT_EQ(site.arguments.size(), 2u);
  EXPECT_EQ(site.arguments[0].kind, AbcArgumentKind::Literal);
  EXPECT_EQ(site.arguments[0].value, "Foo");
  EXPECT_EQ(site.arguments[1].kind, AbcArgumentKind::Unknown);
}
