#include "abc_test_support.hpp"
#include "fo4/abc/abc.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

using fo4::abc::AbcFile;
using fo4::abc::AbcStatus;
using namespace fo4::test;

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

TEST(Abc, ReadsFullWidthIntUIntAndDoublePoolEntries) {
  std::vector<std::uint8_t> bytes;
  put_u16(bytes, fo4::abc::kAbcMinorVersion);
  put_u16(bytes, fo4::abc::kAbcMajorVersion);
  put_u30(bytes, 5);
  put_u32(bytes, 0x7FFFFFFFu);
  put_u32(bytes, 0xFFFFFFFFu);
  put_u32(bytes, 0x80000000u);
  put_u32(bytes, 1);
  put_u30(bytes, 2);
  put_u32(bytes, 0xFF000000u);
  put_u30(bytes, 2);
  bytes.insert(bytes.end(), {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x3F});
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  ASSERT_EQ(file.pool.ints.size(), 5u);
  EXPECT_EQ(file.pool.ints[1], 0x7FFFFFFF);
  EXPECT_EQ(file.pool.ints[2], -1);
  EXPECT_EQ(file.pool.ints[3], std::numeric_limits<std::int32_t>::min());
  EXPECT_EQ(file.pool.ints[4], 1);
  ASSERT_EQ(file.pool.uints.size(), 2u);
  EXPECT_EQ(file.pool.uints[1], 0xFF000000u);
  ASSERT_EQ(file.pool.doubles.size(), 2u);
  EXPECT_EQ(file.pool.doubles[1], 1.5);
}

TEST(Abc, RejectsTypeNameReferencesThatAreNotEarlierAndExpansionOverCap) {
  AbcFile file;
  EXPECT_EQ(AbcFile::parse(multiname_file(2, {0x1D, 0x01, 0x00}), file), AbcStatus::IndexOutOfRange);
  EXPECT_EQ(AbcFile::parse(multiname_file(2, {0x1D, 0x02, 0x00}), file), AbcStatus::IndexOutOfRange);
  EXPECT_EQ(AbcFile::parse(multiname_file(2, {0x1D, 0x00, 0x01, 0x02}), file),
            AbcStatus::IndexOutOfRange);
  std::vector<std::uint8_t> wide{0x1D, 0x00, 0x41};
  wide.resize(wide.size() + 65, 0x00);
  EXPECT_EQ(AbcFile::parse(multiname_file(2, wide), file), AbcStatus::TypeNameTooLarge);
  const std::vector<std::uint8_t> shared{0x1D, 0x00, 0x00, 0x1D, 0x00, 0x02, 0x01, 0x01,
                                         0x1D, 0x00, 0x02, 0x02, 0x02, 0x1D, 0x00, 0x02,
                                         0x03, 0x03, 0x1D, 0x00, 0x02, 0x04, 0x04};
  EXPECT_EQ(AbcFile::parse(multiname_file(6, shared), file), AbcStatus::Ok);
  std::vector<std::uint8_t> over = shared;
  over.insert(over.end(), {0x1D, 0x00, 0x02, 0x05, 0x05});
  EXPECT_EQ(AbcFile::parse(multiname_file(7, over), file), AbcStatus::TypeNameTooLarge);
  std::vector<std::uint8_t> chain;
  for (std::uint8_t k = 0; k < 63; ++k) {
    chain.insert(chain.end(), {0x1D, k, 0x00});
  }
  EXPECT_EQ(AbcFile::parse(multiname_file(64, chain), file), AbcStatus::Ok);
  chain.insert(chain.end(), {0x1D, 63, 0x00});
  EXPECT_EQ(AbcFile::parse(multiname_file(65, chain), file), AbcStatus::TypeNameTooLarge);
}

TEST(Abc, AcceptsEverySpecValueKindInOptionalValues) {
  const std::vector<std::uint8_t> kinds{0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x0A,
                                        0x0B, 0x0C, 0x16, 0x17, 0x18, 0x19, 0x1A};
  std::vector<std::uint8_t> bytes = abc_header();
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 1);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u8(bytes, 0x08);
  put_u30(bytes, static_cast<std::uint32_t>(kinds.size()));
  for (const std::uint8_t kind : kinds) {
    put_u30(bytes, 0);
    put_u8(bytes, kind);
  }
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  put_u30(bytes, 0);
  AbcFile file;
  ASSERT_EQ(AbcFile::parse(bytes, file), AbcStatus::Ok);
  ASSERT_EQ(file.methods.size(), 1u);
  const fo4::abc::AbcMethodInfo& method = file.methods[0];
  ASSERT_EQ(method.options.size(), kinds.size());
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    EXPECT_EQ(static_cast<std::uint8_t>(method.options[i].kind), kinds[i]);
  }
  EXPECT_EQ(file.optional_value_at(method.options[0]), "undefined");
  EXPECT_EQ(file.optional_value_at(method.options[7]), "false");
  EXPECT_EQ(file.optional_value_at(method.options[8]), "true");
  EXPECT_EQ(file.optional_value_at(method.options[9]), "null");
}
