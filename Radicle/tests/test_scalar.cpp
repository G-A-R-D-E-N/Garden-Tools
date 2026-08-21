#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

namespace {

constexpr auto k_xhlt = fo4::Signature::from_string("XHLT");

}

TEST(ScalarSchema, XhltIsU32HumanPercent) {
  const auto descriptor = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(descriptor->presentation, fo4::schema::ScalarPresentation::percent_0_100);
  EXPECT_EQ(descriptor->minimum, 0.0);
  EXPECT_EQ(descriptor->maximum, 100.0);
}

TEST(ScalarSchema, XhltWritesPercentagePointsDirectlyAsLittleEndianU32) {
  const auto descriptor = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(fo4::schema::encode_scalar("0", *descriptor),
            (std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(fo4::schema::encode_scalar("50", *descriptor),
            (std::vector<std::byte>{std::byte{50}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(fo4::schema::encode_scalar("100", *descriptor),
            (std::vector<std::byte>{std::byte{100}, std::byte{0}, std::byte{0}, std::byte{0}}));
}

TEST(ScalarSchema, XhltRejectsFractionsNegativesAndOverOneHundred) {
  const auto descriptor = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_THROW(fo4::schema::encode_scalar("50.5", *descriptor), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::encode_scalar("-1", *descriptor), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::encode_scalar("101", *descriptor), fo4::schema::ScalarError);
}

TEST(ScalarSchema, ExistingOutOfRangeXhltRemainsReadable) {
  const auto descriptor = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(descriptor.has_value());
  const std::array<std::byte, 4> raw{
      std::byte{0x96}, std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_EQ(fo4::schema::decode_scalar(raw, *descriptor), "150");
}

TEST(ScalarSchema, WrongBinaryWidthFailsClosed) {
  const auto descriptor = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(descriptor.has_value());
  const std::array<std::byte, 2> raw{std::byte{50}, std::byte{0}};
  EXPECT_THROW(fo4::schema::decode_scalar(raw, *descriptor), fo4::schema::ScalarError);
}
