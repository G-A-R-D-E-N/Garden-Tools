#include <fo4/abc/abc.hpp>
#include <fo4/abc/edit.hpp>
#include <fo4/abc/flow.hpp>
#include <fo4/abc/opcodes.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "abc_test_support.hpp"

namespace {

using fo4::abc::AbcEditStatus;
using fo4::abc::AbcException;

fo4::abc::AbcFile fixture_file() {
  fo4::abc::AbcFile file;
  EXPECT_EQ(fo4::abc::AbcFile::parse(fo4::test::build_abc(), file), fo4::abc::AbcStatus::Ok);
  return file;
}

fo4::abc::AbcFile round_trip(const fo4::abc::AbcFile& file) {
  std::vector<std::uint8_t> bytes;
  EXPECT_EQ(file.serialize(bytes), fo4::abc::AbcStatus::Ok);
  fo4::abc::AbcFile reparsed;
  EXPECT_EQ(fo4::abc::AbcFile::parse(bytes, reparsed), fo4::abc::AbcStatus::Ok);
  return reparsed;
}

fo4::abc::AbcFlowStatus verify_body(const fo4::abc::AbcFile& file, std::uint32_t body) {
  fo4::abc::AbcMethodFlow flow;
  fo4::abc::AbcFlowIssue issue;
  return fo4::abc::verify_method_flow(file, body, flow, issue);
}

fo4::abc::AbcNamespace package_ns(std::uint32_t name) {
  fo4::abc::AbcNamespace value;
  value.kind = fo4::abc::AbcNamespaceKind::Package;
  value.name = name;
  return value;
}

fo4::abc::AbcMultiname qname(std::uint32_t ns, std::uint32_t name) {
  fo4::abc::AbcMultiname value;
  value.kind = fo4::abc::AbcMultinameKind::QName;
  value.ns = ns;
  value.name = name;
  return value;
}

void set_body(fo4::abc::AbcFile& file, const std::vector<std::uint8_t>& code,
              const std::vector<AbcException>& exceptions = {}) {
  file.bodies[0].code = code;
  file.bodies[0].exceptions = exceptions;
  file.bodies[0].max_stack = 4;
  file.bodies[0].local_count = 1;
}

std::vector<std::uint8_t> code_of(const fo4::abc::AbcFile& file) { return file.bodies[0].code; }

std::size_t body_code_size(const fo4::abc::AbcFile& file) { return file.bodies[0].code.size(); }

}  // namespace

TEST(AbcEdit, AnUnchangedBodyDecodesBackToTheSameBytes) {
  const auto bytes = fo4::test::build_abc();
  fo4::abc::AbcFile file;
  ASSERT_EQ(fo4::abc::AbcFile::parse(bytes, file), fo4::abc::AbcStatus::Ok);
  std::vector<std::uint8_t> serialized;
  ASSERT_EQ(file.serialize(serialized), fo4::abc::AbcStatus::Ok);
  EXPECT_EQ(serialized, bytes);

  std::vector<fo4::abc::AbcInstruction> code;
  ASSERT_EQ(fo4::abc::decode_code(file.bodies[0].code, code, file.bodies[0].exceptions),
            fo4::abc::AbcStatus::Ok);
  std::vector<std::uint8_t> encoded;
  ASSERT_EQ(fo4::abc::encode_code(code, encoded), fo4::abc::AbcStatus::Ok);
  EXPECT_EQ(encoded, file.bodies[0].code);
}

TEST(AbcEdit, InsertingANopShiftsTheBranchAndDeletingItRestoresTheBody) {
  const std::vector<std::uint8_t> original = {0x10, 0x00, 0x00, 0x00, 0x47};
  auto file = fixture_file();
  set_body(file, original);
  const auto original_max_stack = file.bodies[0].max_stack;
  const auto original_local_count = file.bodies[0].local_count;

  std::string message;
  const std::uint8_t nop = 0x02;
  ASSERT_EQ(fo4::abc::insert_instruction(file, 0, 4, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::Ok)
      << message;
  ASSERT_EQ(body_code_size(file), 6u);
  std::vector<fo4::abc::AbcInstruction> code;
  ASSERT_EQ(fo4::abc::decode_code(file.bodies[0].code, code), fo4::abc::AbcStatus::Ok);
  ASSERT_EQ(code.size(), 3u);
  EXPECT_EQ(code[0].offset, 0u);
  EXPECT_EQ(code[0].operands[0].target, 5);
  EXPECT_EQ(code[1].offset, 4u);
  EXPECT_EQ(code[1].opcode, 0x02);
  EXPECT_EQ(code[2].offset, 5u);
  EXPECT_EQ(file.bodies[0].max_stack, original_max_stack);

  ASSERT_EQ(fo4::abc::delete_instruction(file, 0, 4, message), AbcEditStatus::Ok) << message;
  EXPECT_EQ(code_of(file), original);
  EXPECT_EQ(file.bodies[0].max_stack, original_max_stack);
  EXPECT_EQ(file.bodies[0].local_count, original_local_count);
}

TEST(AbcEdit, InsertingBeforeTheFirstInstructionMovesEveryBranchTarget) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x00, 0x00, 0x00, 0x47});
  std::string message;
  const std::uint8_t nop = 0x02;
  ASSERT_EQ(fo4::abc::insert_instruction(file, 0, 0, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::Ok)
      << message;
  std::vector<fo4::abc::AbcInstruction> code;
  ASSERT_EQ(fo4::abc::decode_code(file.bodies[0].code, code), fo4::abc::AbcStatus::Ok);
  ASSERT_EQ(code.size(), 3u);
  EXPECT_EQ(code[0].opcode, 0x02);
  EXPECT_EQ(code[1].offset, 1u);
  EXPECT_EQ(code[1].operands[0].target, 5);
}

TEST(AbcEdit, RefusesAnOffsetInsideAnInstruction) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x00, 0x00, 0x00, 0x47});
  const auto before = code_of(file);
  std::string message;
  const std::uint8_t nop = 0x02;
  EXPECT_EQ(fo4::abc::insert_instruction(file, 0, 1, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::NotAnInstructionBoundary);
  EXPECT_EQ(fo4::abc::delete_instruction(file, 0, 2, message),
            AbcEditStatus::NotAnInstructionBoundary);
  EXPECT_EQ(code_of(file), before);
}

TEST(AbcEdit, RefusesInsertedBytesThatAreNotOnePlainInstruction) {
  auto file = fixture_file();
  set_body(file, {0x47});
  std::string message;
  const std::uint8_t two[2] = {0x02, 0x02};
  EXPECT_EQ(fo4::abc::insert_instruction(file, 0, 0, std::span<const std::uint8_t>(two, 2), message),
            AbcEditStatus::BadInsertedInstruction);
  const std::uint8_t branch[4] = {0x10, 0x00, 0x00, 0x00};
  EXPECT_EQ(fo4::abc::insert_instruction(file, 0, 0, std::span<const std::uint8_t>(branch, 4),
                                         message),
            AbcEditStatus::InsertedInstructionBranches);
  EXPECT_EQ(fo4::abc::insert_instruction(file, 0, 0, std::span<const std::uint8_t>(), message),
            AbcEditStatus::BadInsertedInstruction);
}

TEST(AbcEdit, RefusesToDeleteAnInstructionABranchTargets) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x00, 0x00, 0x00, 0x47});
  const auto before = code_of(file);
  std::string message;
  EXPECT_EQ(fo4::abc::delete_instruction(file, 0, 4, message),
            AbcEditStatus::BranchTargetsDeletedInstruction);
  EXPECT_EQ(code_of(file), before);
}

TEST(AbcEdit, InsertingShiftsAndRestoresAnExceptionRange) {
  auto file = fixture_file();
  set_body(file, {0x20, 0x03, 0x47}, {AbcException{0, 2, 2, 0, 0}});
  std::string message;
  const std::uint8_t nop = 0x02;
  ASSERT_EQ(fo4::abc::insert_instruction(file, 0, 0, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::Ok)
      << message;
  ASSERT_EQ(file.bodies[0].exceptions.size(), 1u);
  EXPECT_EQ(file.bodies[0].exceptions[0].from, 1u);
  EXPECT_EQ(file.bodies[0].exceptions[0].to, 3u);
  EXPECT_EQ(file.bodies[0].exceptions[0].target, 3u);
  ASSERT_EQ(fo4::abc::delete_instruction(file, 0, 0, message), AbcEditStatus::Ok) << message;
  EXPECT_EQ(file.bodies[0].exceptions[0].from, 0u);
  EXPECT_EQ(file.bodies[0].exceptions[0].to, 2u);
  EXPECT_EQ(file.bodies[0].exceptions[0].target, 2u);
}

TEST(AbcEdit, RaisesTheLocalCountWhenTheInsertedInstructionNamesALocal) {
  auto file = fixture_file();
  set_body(file, {0x62, 0x05, 0x29, 0x47});
  file.bodies[0].local_count = 1;
  std::string message;
  const std::uint8_t nop = 0x02;
  ASSERT_EQ(fo4::abc::insert_instruction(file, 0, 0, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::Ok)
      << message;
  EXPECT_EQ(file.bodies[0].local_count, 6u);
}

TEST(AbcEdit, AVerifierRefusalRestoresTheBody) {
  auto file = fixture_file();
  set_body(file, {0x47});
  const auto before = code_of(file);
  const auto max_stack = file.bodies[0].max_stack;
  const auto local_count = file.bodies[0].local_count;
  std::string message;
  const std::uint8_t return_value = 0x48;
  EXPECT_EQ(fo4::abc::insert_instruction(file, 0, 0,
                                         std::span<const std::uint8_t>(&return_value, 1), message),
            AbcEditStatus::VerifierRefused);
  EXPECT_NE(message.find("verifier refused"), std::string::npos) << message;
  EXPECT_EQ(code_of(file), before);
  EXPECT_EQ(file.bodies[0].max_stack, max_stack);
  EXPECT_EQ(file.bodies[0].local_count, local_count);
}

TEST(AbcEdit, RefusesAMissingBody) {
  auto file = fixture_file();
  std::string message;
  const std::uint8_t nop = 0x02;
  EXPECT_EQ(fo4::abc::insert_instruction(file, 99, 0, std::span<const std::uint8_t>(&nop, 1),
                                         message),
            AbcEditStatus::NoSuchBody);
  EXPECT_EQ(fo4::abc::delete_instruction(file, 99, 0, message), AbcEditStatus::NoSuchBody);
  EXPECT_EQ(fo4::abc::replace_instruction(file, 99, 0, std::span<const std::uint8_t>(&nop, 1),
                                          message),
            AbcEditStatus::NoSuchBody);
  EXPECT_EQ(fo4::abc::refresh_body_limits(file, 99, message), AbcEditStatus::NoSuchBody);
}

TEST(AbcEdit, RefreshRaisesMaxStackToTheMeasuredDepth) {
  auto file = fixture_file();
  set_body(file, {0x24, 0x01, 0x24, 0x02, 0x29, 0x29, 0x47});
  file.bodies[0].max_stack = 1;
  std::string message;
  ASSERT_EQ(fo4::abc::refresh_body_limits(file, 0, message), AbcEditStatus::Ok) << message;
  EXPECT_EQ(file.bodies[0].max_stack, 2u);
  EXPECT_EQ(file.bodies[0].local_count, 1u);
  EXPECT_EQ(body_code_size(file), 7u);
}

TEST(AbcEdit, RefreshNeverLowersADeclaredMaxStack) {
  auto file = fixture_file();
  set_body(file, {0x24, 0x01, 0x29, 0x47});
  std::string message;
  ASSERT_EQ(fo4::abc::refresh_body_limits(file, 0, message), AbcEditStatus::Ok) << message;
  EXPECT_EQ(file.bodies[0].max_stack, 4u);
}

TEST(AbcEdit, RefreshRaisesLocalCountForALocalSlot) {
  auto file = fixture_file();
  set_body(file, {0x62, 0x05, 0x29, 0x47});
  std::string message;
  ASSERT_EQ(fo4::abc::refresh_body_limits(file, 0, message), AbcEditStatus::Ok) << message;
  EXPECT_EQ(file.bodies[0].local_count, 6u);
  EXPECT_EQ(file.bodies[0].max_stack, 4u);
}

TEST(AbcEdit, RefreshRefusesABranchThatMergesWithDifferentDepthsAndKeepsTheBody) {
  auto file = fixture_file();
  set_body(file, {0x26, 0x11, 0x02, 0x00, 0x00, 0x24, 0x01, 0x47});
  const auto before = code_of(file);
  std::string message;
  EXPECT_EQ(fo4::abc::refresh_body_limits(file, 0, message), AbcEditStatus::VerifierRefused);
  EXPECT_NE(message.find("verifier refused"), std::string::npos) << message;
  EXPECT_EQ(code_of(file), before);
  EXPECT_EQ(file.bodies[0].max_stack, 4u);
  EXPECT_EQ(file.bodies[0].local_count, 1u);
}

TEST(AbcEdit, ReplacingAnInstructionKeepsABranchOnTheReplacedOffset) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x00, 0x00, 0x00, 0x02, 0x47});
  std::string message;
  const std::uint8_t push_one[2] = {0x24, 0x01};
  ASSERT_EQ(fo4::abc::replace_instruction(file, 0, 4, std::span<const std::uint8_t>(push_one, 2),
                                          message),
            AbcEditStatus::Ok)
      << message;
  EXPECT_EQ(code_of(file), (std::vector<std::uint8_t>{0x10, 0x00, 0x00, 0x00, 0x24, 0x01, 0x47}));
  std::vector<fo4::abc::AbcInstruction> code;
  ASSERT_EQ(fo4::abc::decode_code(file.bodies[0].code, code), fo4::abc::AbcStatus::Ok);
  ASSERT_EQ(code.size(), 3u);
  EXPECT_EQ(code[0].operands[0].target, 4);
  EXPECT_EQ(code[1].offset, 4u);
  EXPECT_EQ(code[2].offset, 6u);
}

TEST(AbcEdit, ReplacingAnInstructionMovesALaterBranchTargetByTheLengthChange) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x01, 0x00, 0x00, 0x02, 0x47});
  std::string message;
  const std::uint8_t push_one[2] = {0x24, 0x01};
  ASSERT_EQ(fo4::abc::replace_instruction(file, 0, 4, std::span<const std::uint8_t>(push_one, 2),
                                          message),
            AbcEditStatus::Ok)
      << message;
  EXPECT_EQ(code_of(file), (std::vector<std::uint8_t>{0x10, 0x02, 0x00, 0x00, 0x24, 0x01, 0x47}));
  std::vector<fo4::abc::AbcInstruction> code;
  ASSERT_EQ(fo4::abc::decode_code(file.bodies[0].code, code), fo4::abc::AbcStatus::Ok);
  ASSERT_EQ(code.size(), 3u);
  EXPECT_EQ(code[0].operands[0].target, 6);
  EXPECT_EQ(code[2].offset, 6u);
}

TEST(AbcEdit, ReplaceRefusesABranchAndAnOffsetThatIsNotAnInstructionStart) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x00, 0x00, 0x00, 0x02, 0x47});
  const auto before = code_of(file);
  std::string message;
  const std::uint8_t nop = 0x02;
  const std::uint8_t jump[4] = {0x10, 0x00, 0x00, 0x00};
  const std::uint8_t two_nops[2] = {0x02, 0x02};
  EXPECT_EQ(fo4::abc::replace_instruction(file, 0, 4, std::span<const std::uint8_t>(jump, 4), message),
            AbcEditStatus::InsertedInstructionBranches);
  EXPECT_EQ(fo4::abc::replace_instruction(file, 0, 4, std::span<const std::uint8_t>(two_nops, 2),
                                          message),
            AbcEditStatus::BadInsertedInstruction);
  EXPECT_EQ(fo4::abc::replace_instruction(file, 0, 1, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::NotAnInstructionBoundary);
  EXPECT_EQ(fo4::abc::replace_instruction(file, 0, 6, std::span<const std::uint8_t>(&nop, 1), message),
            AbcEditStatus::NotAnInstructionBoundary);
  EXPECT_EQ(code_of(file), before);
}

TEST(AbcEdit, ReplaceRestoresTheBodyWhenTheVerifierRefuses) {
  auto file = fixture_file();
  set_body(file, {0x10, 0x00, 0x00, 0x00, 0x02, 0x47});
  const auto before = code_of(file);
  std::string message;
  const std::uint8_t return_value = 0x48;
  EXPECT_EQ(fo4::abc::replace_instruction(file, 0, 4,
                                          std::span<const std::uint8_t>(&return_value, 1), message),
            AbcEditStatus::VerifierRefused);
  EXPECT_NE(message.find("verifier refused"), std::string::npos) << message;
  EXPECT_EQ(code_of(file), before);
  EXPECT_EQ(file.bodies[0].max_stack, 4u);
}

TEST(AbcEdit, InterningAStringReusesAnIdenticalEntryAndAppendsANewOne) {
  // Invariant: an identical string keeps its existing index, so a fixture name never gains a duplicate slot.
  auto file = fixture_file();
  set_body(file, {0x47});
  EXPECT_EQ(fo4::abc::intern_string(file, "call"), 3u);
  EXPECT_EQ(fo4::abc::intern_string(file, "brand new"), 15u);
  EXPECT_EQ(fo4::abc::intern_string(file, "brand new"), 15u);
  ASSERT_EQ(file.pool.strings.size(), 16u);
  const auto reparsed = round_trip(file);
  ASSERT_EQ(reparsed.pool.strings.size(), 16u);
  EXPECT_EQ(reparsed.pool.strings[15], "brand new");
  EXPECT_EQ(verify_body(reparsed, 0), fo4::abc::AbcFlowStatus::Ok);
}

TEST(AbcEdit, InterningNumbersNamespacesAndMultinamesReusesIdenticalEntries) {
  // Invariant: -0.0 is not merged into the 0.0 default, and a repeated namespace or multiname keeps its index.
  auto file = fixture_file();
  set_body(file, {0x47});
  std::uint32_t index = 99;
  std::string message;
  EXPECT_EQ(fo4::abc::intern_int(file, 7), 1u);
  EXPECT_EQ(fo4::abc::intern_int(file, -3), 2u);
  EXPECT_EQ(fo4::abc::intern_uint(file, 9), 1u);
  EXPECT_EQ(fo4::abc::intern_double(file, 1.5), 1u);
  EXPECT_EQ(fo4::abc::intern_double(file, -0.0), 2u);
  EXPECT_EQ(fo4::abc::intern_namespace(file, package_ns(6), index, message), AbcEditStatus::Ok);
  EXPECT_EQ(index, 1u);
  EXPECT_EQ(fo4::abc::intern_namespace(file, package_ns(9), index, message), AbcEditStatus::Ok);
  EXPECT_EQ(index, 3u);
  EXPECT_EQ(fo4::abc::intern_multiname(file, qname(1, 2), index, message), AbcEditStatus::Ok);
  EXPECT_EQ(index, 1u);
  EXPECT_EQ(fo4::abc::intern_multiname(file, qname(2, 2), index, message), AbcEditStatus::Ok);
  EXPECT_EQ(index, 15u);
  EXPECT_EQ(file.pool.ints.size(), 3u);
  EXPECT_EQ(file.pool.doubles.size(), 3u);

  const auto reparsed = round_trip(file);
  ASSERT_EQ(reparsed.pool.ints.size(), 3u);
  EXPECT_EQ(reparsed.pool.ints[2], -3);
  ASSERT_EQ(reparsed.pool.doubles.size(), 3u);
  EXPECT_TRUE(std::signbit(reparsed.pool.doubles[2]));
  ASSERT_EQ(reparsed.pool.namespaces.size(), 4u);
  EXPECT_EQ(reparsed.pool.namespaces[3].name, 9u);
  ASSERT_EQ(reparsed.pool.multinames.size(), 16u);
  EXPECT_EQ(reparsed.pool.multinames[15].ns, 2u);
  EXPECT_EQ(reparsed.pool.multinames[15].name, 2u);
  EXPECT_EQ(verify_body(reparsed, 0), fo4::abc::AbcFlowStatus::Ok);
}

TEST(AbcEdit, RefusesANamespaceOrMultinameNamingAnIndexOutsideItsPool) {
  // Invariant: an entry naming a string or namespace past its table is refused before any append.
  auto file = fixture_file();
  set_body(file, {0x47});
  const auto before_multinames = file.pool.multinames.size();
  const auto before_namespaces = file.pool.namespaces.size();
  const auto before_strings = file.pool.strings.size();
  std::uint32_t index = 7;
  std::string message;
  EXPECT_EQ(fo4::abc::intern_multiname(file, qname(1, 99), index, message),
            AbcEditStatus::PoolEntryOutOfRange);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(fo4::abc::intern_multiname(file, qname(3, 2), index, message),
            AbcEditStatus::PoolEntryOutOfRange);
  EXPECT_EQ(fo4::abc::intern_namespace(file, package_ns(15), index, message),
            AbcEditStatus::PoolEntryOutOfRange);
  EXPECT_NE(message.find("outside its pool"), std::string::npos) << message;
  EXPECT_EQ(file.pool.multinames.size(), before_multinames);
  EXPECT_EQ(file.pool.namespaces.size(), before_namespaces);
  EXPECT_EQ(file.pool.strings.size(), before_strings);
}

TEST(AbcEdit, ReplacingTheBodyCodeRaisesMaxScopeDepthToTheMeasuredDepth) {
  // Invariant: a replaced body gets max_scope_depth from measurement, not a refusal against the stale limit.
  auto file = fixture_file();
  set_body(file, {0x47});
  file.bodies[0].max_scope_depth = 0;
  std::string message;
  const std::vector<std::uint8_t> code = {0x20, 0x30, 0x20, 0x30, 0x47};
  ASSERT_EQ(fo4::abc::replace_body_code(file, 0, code, message), AbcEditStatus::Ok) << message;
  EXPECT_EQ(code_of(file), code);
  EXPECT_EQ(file.bodies[0].max_scope_depth, 2u);
  EXPECT_EQ(file.bodies[0].max_stack, 4u);
  EXPECT_EQ(file.bodies[0].local_count, 1u);
  const auto reparsed = round_trip(file);
  ASSERT_EQ(reparsed.bodies.size(), 3u);
  EXPECT_EQ(reparsed.bodies[0].max_scope_depth, 2u);
  EXPECT_EQ(verify_body(reparsed, 0), fo4::abc::AbcFlowStatus::Ok);
}

TEST(AbcEdit, ReplacingTheBodyCodeRefusesUnverifiedCodeAndKeepsTheBody) {
  // Invariant: replacement code that fails verification restores the body byte for byte.
  auto file = fixture_file();
  set_body(file, {0x47});
  const auto before = code_of(file);
  const auto max_scope_depth = file.bodies[0].max_scope_depth;
  std::string message;
  const std::vector<std::uint8_t> code = {0x48};
  EXPECT_EQ(fo4::abc::replace_body_code(file, 0, code, message), AbcEditStatus::VerifierRefused);
  EXPECT_NE(message.find("verifier refused"), std::string::npos) << message;
  EXPECT_EQ(code_of(file), before);
  EXPECT_EQ(file.bodies[0].max_stack, 4u);
  EXPECT_EQ(file.bodies[0].max_scope_depth, max_scope_depth);
}

TEST(AbcEdit, AddingAMethodAppendsItsInfoAndBodyAndReturnsTheNewIndex) {
  // Invariant: the appended body names the appended method and verifies at that index after a round trip.
  auto file = fixture_file();
  fo4::abc::AbcMethodInfo method;
  method.return_type = fo4::test::kMultinameVoid;
  const std::vector<std::uint8_t> code = {0x47};
  std::uint32_t index = 99;
  std::string message;
  ASSERT_EQ(fo4::abc::add_method(file, method, code, index, message), AbcEditStatus::Ok)
      << message;
  EXPECT_EQ(index, 3u);
  ASSERT_EQ(file.methods.size(), 4u);
  ASSERT_EQ(file.bodies.size(), 4u);
  EXPECT_EQ(file.bodies[3].method, 3u);
  EXPECT_EQ(file.bodies[3].local_count, 1u);
  EXPECT_EQ(file.bodies[3].max_stack, 0u);
  EXPECT_EQ(file.bodies[3].max_scope_depth, 0u);
  EXPECT_EQ(file.bodies[3].code, code);
  const auto reparsed = round_trip(file);
  ASSERT_EQ(reparsed.methods.size(), 4u);
  ASSERT_NE(reparsed.body_for(3), nullptr);
  EXPECT_EQ(reparsed.body_for(3)->code, code);
  EXPECT_EQ(verify_body(reparsed, 3), fo4::abc::AbcFlowStatus::Ok);
}

TEST(AbcEdit, AddingAMethodRefusesUnverifiedCodeAndLeavesTheFileUnchanged) {
  // Invariant: a method whose body fails verification is removed again, keeping the fixture table counts.
  auto file = fixture_file();
  fo4::abc::AbcMethodInfo method;
  method.return_type = fo4::test::kMultinameVoid;
  const std::vector<std::uint8_t> code = {0x48};
  std::uint32_t index = 99;
  std::string message;
  EXPECT_EQ(fo4::abc::add_method(file, method, code, index, message),
            AbcEditStatus::VerifierRefused);
  EXPECT_NE(message.find("verifier refused"), std::string::npos) << message;
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(file.methods.size(), 3u);
  EXPECT_EQ(file.bodies.size(), 3u);
}
