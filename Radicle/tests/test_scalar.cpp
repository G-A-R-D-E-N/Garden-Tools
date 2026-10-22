#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto k_refr = fo4::Signature::from_string("REFR");
constexpr auto k_misc = fo4::Signature::from_string("MISC");
constexpr auto k_xhlt = fo4::Signature::from_string("XHLT");
constexpr auto k_xscl = fo4::Signature::from_string("XSCL");

std::optional<fo4::schema::ScalarDescriptor> record_scalar(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field) return std::nullopt;
  return field->scalar;
}

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
  ASSERT_EQ(fields.size(), 52u);
  const auto has = [&fields](std::string_view path) {
    return std::any_of(fields.begin(), fields.end(), [path](const auto& field) { return field.path == path; });
  };
  EXPECT_TRUE(has("REFR\\HealthPercent"));
  EXPECT_TRUE(has("REFR\\Scale"));
}

TEST(ScalarSchema, XhltIsU32HumanPercent) {
  const auto descriptor = record_scalar("REFR\\HealthPercent");
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(descriptor->presentation, fo4::schema::ScalarPresentation::percent_0_100);
  EXPECT_EQ(descriptor->minimum, 0.0);
  EXPECT_EQ(descriptor->maximum, 100.0);
}

TEST(ScalarSchema, XhltWritesPercentagePointsDirectlyAsLittleEndianU32) {
  const auto descriptor = record_scalar("REFR\\HealthPercent");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_EQ(fo4::schema::encode_scalar("0", *descriptor),
            (std::vector<std::byte>{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(fo4::schema::encode_scalar("50", *descriptor),
            (std::vector<std::byte>{std::byte{50}, std::byte{0}, std::byte{0}, std::byte{0}}));
  EXPECT_EQ(fo4::schema::encode_scalar("100", *descriptor),
            (std::vector<std::byte>{std::byte{100}, std::byte{0}, std::byte{0}, std::byte{0}}));
}

TEST(ScalarSchema, XhltRejectsFractionsNegativesAndOverOneHundred) {
  const auto descriptor = record_scalar("REFR\\HealthPercent");
  ASSERT_TRUE(descriptor.has_value());

  EXPECT_THROW(fo4::schema::encode_scalar("50.5", *descriptor), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::encode_scalar("-1", *descriptor), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::encode_scalar("101", *descriptor), fo4::schema::ScalarError);
}

TEST(ScalarSchema, XsclIsF32ScaleLimitedToZeroThroughTenOnWrite) {
  const auto descriptor = record_scalar("REFR\\Scale");
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
  const auto health = record_scalar("REFR\\HealthPercent");
  ASSERT_TRUE(health.has_value());
  const std::array<std::byte, 4> raw_health{
      std::byte{0x96}, std::byte{0}, std::byte{0}, std::byte{0}};
  EXPECT_EQ(fo4::schema::decode_scalar(raw_health, *health), "150");

  const auto scale = record_scalar("REFR\\Scale");
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
  EXPECT_EQ(fields.size(), 70u);
}

TEST(ScalarSchema, EnumRowExposesItsXEditNames) {
  const auto stagger = fo4::schema::scalar_field_descriptor("WEAP\\Stagger");
  ASSERT_TRUE(stagger.has_value());
  const auto& scalar = stagger->scalar;
  EXPECT_EQ(scalar.presentation, fo4::schema::ScalarPresentation::enumeration);
  ASSERT_EQ(scalar.names.size(), 5u);
  EXPECT_EQ(scalar.names[0].value, 0);
  EXPECT_EQ(scalar.names[0].name, "None");
  EXPECT_EQ(scalar.names[4].value, 4);
  EXPECT_EQ(scalar.names[4].name, "Extra Large");

  const auto large = fo4::schema::scalar_value_name(scalar, 3);
  ASSERT_TRUE(large.has_value());
  EXPECT_EQ(*large, "Large");
  EXPECT_FALSE(fo4::schema::scalar_value_name(scalar, 5).has_value());

  const auto small_value = fo4::schema::scalar_name_value(scalar, "Small");
  ASSERT_TRUE(small_value.has_value());
  EXPECT_EQ(*small_value, 1);
  EXPECT_FALSE(fo4::schema::scalar_name_value(scalar, "small").has_value());
}

TEST(ScalarSchema, FlagsRowExposesOneMaskPerNamedBit) {
  const auto flags = fo4::schema::scalar_field_descriptor("FACT\\Flags");
  ASSERT_TRUE(flags.has_value());
  const auto& scalar = flags->scalar;
  EXPECT_EQ(scalar.presentation, fo4::schema::ScalarPresentation::flags);
  ASSERT_EQ(scalar.names.size(), 17u);
  EXPECT_EQ(scalar.names[0].value, std::int64_t{1});
  EXPECT_EQ(scalar.names[0].name, "Hidden From NPC");
  EXPECT_EQ(scalar.names[16].value, std::int64_t{1} << 16);
  EXPECT_EQ(scalar.names[16].name, "Ignore Crimes: Werewolf (unused)");

  const auto vendor = fo4::schema::scalar_name_value(scalar, "Vendor");
  ASSERT_TRUE(vendor.has_value());
  EXPECT_EQ(*vendor, std::int64_t{1} << 14);
  const auto track = fo4::schema::scalar_value_name(scalar, std::int64_t{1} << 6);
  ASSERT_TRUE(track.has_value());
  EXPECT_EQ(*track, "Track Crime");
  EXPECT_FALSE(fo4::schema::scalar_value_name(scalar, 3).has_value());
  EXPECT_FALSE(fo4::schema::scalar_value_name(scalar, std::int64_t{1} << 17).has_value());
}

TEST(ScalarSchema, EmbeddedFlagsRowKeepsItsRawU16Encoding) {
  const auto templates = fo4::schema::scalar_field_descriptor("NPC_\\TemplateFlags");
  ASSERT_TRUE(templates.has_value());
  const auto& scalar = templates->scalar;
  EXPECT_EQ(scalar.presentation, fo4::schema::ScalarPresentation::flags);
  EXPECT_EQ(scalar.storage, fo4::schema::ScalarStorage::u16);
  EXPECT_EQ(scalar.offset, 14u);
  ASSERT_EQ(scalar.names.size(), 13u);
  EXPECT_EQ(scalar.names[12].value, std::int64_t{1} << 12);
  EXPECT_EQ(scalar.names[12].name, "Keywords");

  const std::array<std::byte, 2> raw{std::byte{0x01}, std::byte{0x10}};
  EXPECT_EQ(fo4::schema::decode_scalar(raw, scalar), "4097");
  EXPECT_EQ(fo4::schema::encode_scalar("4097", scalar),
            (std::vector<std::byte>{std::byte{0x01}, std::byte{0x10}}));
}

TEST(ScalarSchema, OnlyEnumAndFlagsRowsCarryNames) {
  for (const std::string_view record : {"FACT", "NPC_", "WEAP", "REFR"}) {
    const auto fields = fo4::schema::scalar_fields(fo4::Signature::from_string(record));
    ASSERT_FALSE(fields.empty()) << record;
    for (const auto& field : fields) {
      const auto presentation = field.scalar.presentation;
      const bool named = presentation == fo4::schema::ScalarPresentation::enumeration ||
                         presentation == fo4::schema::ScalarPresentation::flags;
      EXPECT_EQ(!field.scalar.names.empty(), named) << field.path;
    }
  }
}

TEST(ScalarSchema, WrongBinaryWidthFailsClosed) {
  const auto health = record_scalar("REFR\\HealthPercent");
  ASSERT_TRUE(health.has_value());
  const std::array<std::byte, 2> raw{std::byte{50}, std::byte{0}};
  EXPECT_THROW(fo4::schema::decode_scalar(raw, *health), fo4::schema::ScalarError);

  const auto scale = record_scalar("REFR\\Scale");
  ASSERT_TRUE(scale.has_value());
  EXPECT_THROW(fo4::schema::decode_scalar(raw, *scale), fo4::schema::ScalarError);
}
