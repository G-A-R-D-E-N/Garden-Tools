#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

namespace {

constexpr auto k_refr = fo4::Signature::from_string("REFR");
constexpr auto k_misc = fo4::Signature::from_string("MISC");
constexpr auto k_xhlt = fo4::Signature::from_string("XHLT");
constexpr auto k_xscl = fo4::Signature::from_string("XSCL");

}

TEST(ScalarSchema, GeneratedRefrFieldsAreRecordQualified) {
  const auto health = fo4::schema::scalar_field_descriptor("REFR\\HealthPercent");
  ASSERT_TRUE(health.has_value());
  EXPECT_EQ(health->record, k_refr);
  EXPECT_EQ(health->path, "REFR\\HealthPercent");
  EXPECT_EQ(health->scalar.subrecord, k_xhlt);
  EXPECT_FALSE(health->can_insert);

  const auto scale = fo4::schema::scalar_field_descriptor("REFR\\Scale");
  ASSERT_TRUE(scale.has_value());
  EXPECT_EQ(scale->record, k_refr);
  EXPECT_EQ(scale->path, "REFR\\Scale");
  EXPECT_EQ(scale->scalar.subrecord, k_xscl);
  EXPECT_FALSE(scale->can_insert);

  const auto exact = fo4::schema::scalar_field_descriptor(k_refr, "REFR\\HealthPercent");
  ASSERT_TRUE(exact.has_value());
  EXPECT_EQ(exact->record, k_refr);
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(k_misc, "REFR\\HealthPercent").has_value());
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(k_misc, "REFR\\Scale").has_value());

  const auto fields = fo4::schema::scalar_fields(k_refr);
  ASSERT_EQ(fields.size(), 2u);
  EXPECT_EQ(fields[0].path, "REFR\\HealthPercent");
  EXPECT_EQ(fields[1].path, "REFR\\Scale");
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

TEST(ScalarSchema, XsclIsF32ScaleLimitedToZeroThroughTenOnWrite) {
  const auto descriptor = fo4::schema::scalar_descriptor(k_xscl);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(descriptor->presentation, fo4::schema::ScalarPresentation::identity);
  EXPECT_EQ(descriptor->minimum, 0.0);
  EXPECT_EQ(descriptor->maximum, 10.0);

  EXPECT_EQ(fo4::schema::encode_scalar("1.25", *descriptor),
            (std::vector<std::byte>{
                std::byte{0x00}, std::byte{0x00}, std::byte{0xA0}, std::byte{0x3F}}));
  EXPECT_THROW(fo4::schema::encode_scalar("-0.01", *descriptor), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::encode_scalar("10.01", *descriptor), fo4::schema::ScalarError);
}

TEST(ScalarSchema, ExistingOutOfRangeValuesRemainReadable) {
  const auto health = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(health.has_value());
  const std::array<std::byte, 4> raw_health{
      std::byte{0x96}, std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_EQ(fo4::schema::decode_scalar(raw_health, *health), "150");

  const auto scale = fo4::schema::scalar_descriptor(k_xscl);
  ASSERT_TRUE(scale.has_value());
  const std::array<std::byte, 4> raw_scale{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x41}};
  EXPECT_EQ(fo4::schema::decode_scalar(raw_scale, *scale), "12");
}

TEST(ScalarSchema, WeaponDnamFieldsUseTheirPayloadAndEnumBounds) {
  const auto speed = fo4::schema::scalar_field_descriptor("WEAP\\Speed");
  ASSERT_TRUE(speed.has_value());
  EXPECT_EQ(speed->scalar.subrecord, fo4::Signature::from_string("DNAM"));
  EXPECT_EQ(speed->scalar.offset, 4u);
  EXPECT_EQ(speed->scalar.payload_size, 132u);
  EXPECT_EQ(speed->scalar.storage, fo4::schema::ScalarStorage::f32);

  const auto on_hit = fo4::schema::scalar_field_descriptor("WEAP\\OnHit");
  ASSERT_TRUE(on_hit.has_value());
  EXPECT_EQ(on_hit->scalar.offset, 36u);
  EXPECT_EQ(on_hit->scalar.maximum, 3.0);

  const auto stagger = fo4::schema::scalar_field_descriptor("WEAP\\Stagger");
  ASSERT_TRUE(stagger.has_value());
  EXPECT_EQ(stagger->scalar.offset, 124u);
  EXPECT_EQ(stagger->scalar.maximum, 4.0);

  const auto melee = fo4::schema::scalar_field_descriptor("WEAP\\MeleeSpeed");
  ASSERT_TRUE(melee.has_value());
  EXPECT_FALSE(melee->scalar.embedded);
  EXPECT_EQ(melee->scalar.subrecord, fo4::Signature::from_string("MASE"));
  EXPECT_EQ(melee->scalar.maximum, 4.0);

  const auto fields = fo4::schema::scalar_fields(fo4::Signature::from_string("WEAP"));
  EXPECT_EQ(fields.size(), 45u);
}

TEST(ScalarSchema, WrongBinaryWidthFailsClosed) {
  const auto health = fo4::schema::scalar_descriptor(k_xhlt);
  ASSERT_TRUE(health.has_value());
  const std::array<std::byte, 2> raw{std::byte{50}, std::byte{0}};
  EXPECT_THROW(fo4::schema::decode_scalar(raw, *health), fo4::schema::ScalarError);

  const auto scale = fo4::schema::scalar_descriptor(k_xscl);
  ASSERT_TRUE(scale.has_value());
  EXPECT_THROW(fo4::schema::decode_scalar(raw, *scale), fo4::schema::ScalarError);
}
