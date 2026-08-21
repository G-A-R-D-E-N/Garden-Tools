#include "fo4/signature.hpp"

#include <gtest/gtest.h>

TEST(Signature, PacksLittleEndianFourCC) {
  const auto tes4 = fo4::Signature::from_string("TES4");
  EXPECT_EQ(tes4.to_string(), "TES4");
  EXPECT_EQ(tes4.packed(), 0x34534554u);
  EXPECT_EQ(fo4::Signature::from_bytes(0x34534554u).to_string(), "TES4");
}

TEST(Signature, DistinguishesGrupAndXxxx) {
  EXPECT_EQ(fo4::sig_grup.to_string(), "GRUP");
  EXPECT_EQ(fo4::sig_xxxx.to_string(), "XXXX");
  EXPECT_NE(fo4::sig_grup, fo4::sig_tes4);
}
