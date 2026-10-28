#include "abc_test_support.hpp"
#include "fo4/abc/abc.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using fo4::abc::AbcFile;
using fo4::abc::AbcStatus;
using fo4::abc::AbcValueKind;
using namespace fo4::test;

namespace {

AbcFile parse_or_fail(const std::vector<std::uint8_t>& bytes) {
  AbcFile file;
  EXPECT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  return file;
}

void expect_rejected(const AbcFile& file, AbcStatus expected) {
  std::vector<std::uint8_t> out{0xEE};
  EXPECT_EQ(file.serialize(out), expected);
  EXPECT_TRUE(out.empty());
}

}

TEST(AbcWrite, RoundTripsTheSyntheticFixtureByteForByte) {
  const std::vector<std::uint8_t> bytes = build_abc();
  const AbcFile file = parse_or_fail(bytes);
  std::vector<std::uint8_t> out;
  EXPECT_EQ(file.serialize(out), AbcStatus::Ok);
  EXPECT_EQ(out, bytes);
}

TEST(AbcWrite, RoundTripsProtectedNamespaceAndSlotMetadataOfAMinimalInstance) {
  const std::vector<std::uint8_t> bytes = minimal_instance_file(0x40, 0, 0x08);
  const AbcFile file = parse_or_fail(bytes);
  ASSERT_EQ(file.instances.size(), 1u);
  EXPECT_EQ(file.instances[0].protected_ns, 1u);
  std::vector<std::uint8_t> out;
  EXPECT_EQ(file.serialize(out), AbcStatus::Ok);
  EXPECT_EQ(out, bytes);
}

TEST(AbcWrite, SerializesEditedStringAndBodyCodeAndReparsesWithSameCounts) {
  AbcFile file = parse_or_fail(build_abc());
  file.pool.strings[kStringFoo] = "RenamedFoo";
  file.bodies[0].code[0] = 0x61;
  std::vector<std::uint8_t> out;
  ASSERT_EQ(file.serialize(out), AbcStatus::Ok);
  const AbcFile reread = parse_or_fail(out);
  EXPECT_EQ(reread.pool.strings[kStringFoo], "RenamedFoo");
  EXPECT_EQ(reread.bodies[0].code[0], 0x61);
  EXPECT_EQ(reread.pool.strings.size(), file.pool.strings.size());
  EXPECT_EQ(reread.pool.multinames.size(), file.pool.multinames.size());
  EXPECT_EQ(reread.methods.size(), file.methods.size());
  EXPECT_EQ(reread.metadata.size(), file.metadata.size());
  EXPECT_EQ(reread.instances.size(), file.instances.size());
  EXPECT_EQ(reread.classes.size(), file.classes.size());
  EXPECT_EQ(reread.scripts.size(), file.scripts.size());
  EXPECT_EQ(reread.bodies.size(), file.bodies.size());
  EXPECT_EQ(reread.bodies[0].code.size(), file.bodies[0].code.size());
  EXPECT_EQ(reread.bodies[1].code, file.bodies[1].code);
}

TEST(AbcWrite, RejectsAnInconsistentModelWithoutWritingBytes) {
  AbcFile param_mismatch = parse_or_fail(build_abc());
  param_mismatch.methods[0].param_types.push_back(kMultinameVoid);
  expect_rejected(param_mismatch, AbcStatus::InconsistentModel);

  AbcFile options_without_flag = parse_or_fail(build_abc());
  options_without_flag.methods[0].options.push_back({AbcValueKind::Int, 1});
  expect_rejected(options_without_flag, AbcStatus::InconsistentModel);

  AbcFile names_missing = parse_or_fail(build_abc());
  names_missing.methods[1].param_names.clear();
  expect_rejected(names_missing, AbcStatus::InconsistentModel);

  AbcFile protected_without_flag = parse_or_fail(build_abc());
  protected_without_flag.instances[0].protected_ns = 1;
  expect_rejected(protected_without_flag, AbcStatus::InconsistentModel);

  AbcFile metadata_without_flag = parse_or_fail(build_abc());
  metadata_without_flag.instances[0].traits[2].metadata.push_back(0);
  expect_rejected(metadata_without_flag, AbcStatus::InconsistentModel);

  AbcFile attrs_overflow = parse_or_fail(build_abc());
  attrs_overflow.instances[0].traits[2].attrs = 0x10;
  expect_rejected(attrs_overflow, AbcStatus::InconsistentModel);

  AbcFile class_missing = parse_or_fail(build_abc());
  class_missing.classes.pop_back();
  expect_rejected(class_missing, AbcStatus::InconsistentModel);

  AbcFile index_past_pool = parse_or_fail(build_abc());
  index_past_pool.bodies[0].method = kBadIndex;
  expect_rejected(index_past_pool, AbcStatus::IndexOutOfRange);
}

TEST(AbcWrite, ReadsMetadataItemsAsKeysThenValuesAndRoundTrips) {
  std::vector<std::uint8_t> bytes;
  put_u16(bytes, fo4::abc::kAbcMinorVersion);
  put_u16(bytes, fo4::abc::kAbcMajorVersion);
  put_constant_pool(bytes);
  put_u30(bytes, 0);
  put_u30(bytes, 1);
  put_u30(bytes, kStringFoo);
  put_u30(bytes, 2);
  put_u30(bytes, kStringExternalInterface);
  put_u30(bytes, kStringClick);
  put_u30(bytes, kStringCall);
  put_u30(bytes, kStringTest);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  const AbcFile file = parse_or_fail(bytes);
  ASSERT_EQ(file.metadata.size(), 1u);
  ASSERT_EQ(file.metadata[0].items.size(), 2u);
  EXPECT_EQ(file.metadata[0].items[0].key, kStringExternalInterface);
  EXPECT_EQ(file.metadata[0].items[0].value, kStringCall);
  EXPECT_EQ(file.metadata[0].items[1].key, kStringClick);
  EXPECT_EQ(file.metadata[0].items[1].value, kStringTest);
  std::vector<std::uint8_t> out;
  EXPECT_EQ(file.serialize(out), AbcStatus::Ok);
  EXPECT_EQ(out, bytes);
}

TEST(AbcWrite, RejectsPoolReferencesThatTheParserWouldRefuse) {
  AbcFile qname_namespace = parse_or_fail(build_abc());
  qname_namespace.pool.multinames[kMultinameExternalInterface].ns = kNamespaceCount;
  expect_rejected(qname_namespace, AbcStatus::IndexOutOfRange);

  AbcFile qname_name = parse_or_fail(build_abc());
  qname_name.pool.multinames[kMultinameCall].name = kStringCount;
  expect_rejected(qname_name, AbcStatus::IndexOutOfRange);

  AbcFile runtime_name_set = parse_or_fail(build_abc());
  runtime_name_set.pool.multinames[kMultinameRuntimeName].ns_set = 1;
  expect_rejected(runtime_name_set, AbcStatus::IndexOutOfRange);

  AbcFile ns_set_member = parse_or_fail(build_abc());
  ns_set_member.pool.ns_sets.push_back(fo4::abc::AbcNsSet{{kNamespaceCount}});
  ns_set_member.pool.multinames[kMultinameRuntimeName].ns_set = 1;
  expect_rejected(ns_set_member, AbcStatus::IndexOutOfRange);

  AbcFile type_name_base = parse_or_fail(build_abc());
  type_name_base.pool.multinames[kMultinameTypeName].name = kMultinameInner;
  expect_rejected(type_name_base, AbcStatus::IndexOutOfRange);

  AbcFile type_name_param = parse_or_fail(build_abc());
  type_name_param.pool.multinames[kMultinameTypeName].params[0] = kMultinameTypeName;
  expect_rejected(type_name_param, AbcStatus::IndexOutOfRange);
}

TEST(AbcWrite, RejectsOutOfSpecKindValuesWithoutWritingBytes) {
  AbcFile namespace_kind = parse_or_fail(build_abc());
  namespace_kind.pool.namespaces[1].kind = static_cast<fo4::abc::AbcNamespaceKind>(0x09);
  expect_rejected(namespace_kind, AbcStatus::BadNamespaceKind);

  AbcFile multiname_kind = parse_or_fail(build_abc());
  multiname_kind.pool.multinames[kMultinameExternalInterface].kind =
      static_cast<fo4::abc::AbcMultinameKind>(0x08);
  expect_rejected(multiname_kind, AbcStatus::BadMultinameKind);

  AbcFile trait_value_kind = parse_or_fail(build_abc());
  trait_value_kind.instances[0].traits[2].value_kind = static_cast<AbcValueKind>(0x09);
  expect_rejected(trait_value_kind, AbcStatus::BadValueKind);

  AbcFile option_value_kind = parse_or_fail(build_abc());
  option_value_kind.methods[1].options[0].kind = static_cast<AbcValueKind>(0x09);
  expect_rejected(option_value_kind, AbcStatus::BadValueKind);

  AbcFile trait_kind = parse_or_fail(build_abc());
  trait_kind.instances[0].traits[0].kind = static_cast<fo4::abc::AbcTraitKind>(7);
  expect_rejected(trait_kind, AbcStatus::InconsistentModel);
}

TEST(AbcWrite, RejectsU30FieldsPastTheMaskedRangeAndKeepsTheLargestValue) {
  constexpr std::uint32_t kPastU30 = 0x40000000u;

  AbcFile max_stack = parse_or_fail(build_abc());
  max_stack.bodies[0].max_stack = kPastU30;
  expect_rejected(max_stack, AbcStatus::InconsistentModel);

  AbcFile slot_id = parse_or_fail(build_abc());
  slot_id.instances[0].traits[1].slot_id = kPastU30;
  expect_rejected(slot_id, AbcStatus::InconsistentModel);

  AbcFile namespace_name = parse_or_fail(build_abc());
  namespace_name.pool.namespaces[1].name = kPastU30;
  expect_rejected(namespace_name, AbcStatus::InconsistentModel);

  AbcFile exception_to = parse_or_fail(build_abc());
  exception_to.bodies[2].exceptions[0].to = kPastU30;
  expect_rejected(exception_to, AbcStatus::InconsistentModel);

  AbcFile largest = parse_or_fail(build_abc());
  largest.bodies[0].max_stack = 0x3FFFFFFFu;
  std::vector<std::uint8_t> out;
  ASSERT_EQ(largest.serialize(out), AbcStatus::Ok);
  EXPECT_EQ(parse_or_fail(out).bodies[0].max_stack, 0x3FFFFFFFu);
}
