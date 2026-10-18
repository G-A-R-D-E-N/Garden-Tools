#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::Signature;
using fo4::schema::ScalarStorage;

Signature sig(std::string_view text) { return Signature::from_string(text); }

struct ScalarRow {
  std::string_view path;
  std::string_view subrecord;
  ScalarStorage storage;
  std::string_view text;
};

struct LinkRow {
  std::string_view path;
  std::string_view subrecord;
  std::string_view first_target;
};

constexpr ScalarRow k_scalar_rows[] = {
    {"PARW\\AmmoCount", "XAMC", ScalarStorage::u32, "7"},
    {"PARW\\HeadTrackingWeight", "XHTW", ScalarStorage::f32, "3.5"},
    {"PARW\\FavorCost", "XFVC", ScalarStorage::f32, "12.25"},
};

constexpr LinkRow k_link_rows[] = {
    {"PARW\\EncounterZone", "XEZN", "ECZN"},
    {"PARW\\AcousticParent", "XASP", "REFR"},
    {"PARW\\Layer", "XLYR", "LAYR"},
    {"PARW\\MaterialSwap", "XMSP", "MSWP"},
    {"PARW\\ReferenceGroup", "XRFG", "RFGP"},
    {"PARW\\Emittance", "XEMI", "LIGH"},
};

std::vector<std::byte> zero_payload(std::size_t size) { return std::vector<std::byte>(size); }

}  // namespace

TEST(ParwRows, ScalarRowsMatchTheSharedReferenceRecordLayout) {
  EXPECT_EQ(fo4::schema::scalar_fields(sig("PARW")).size(), 14u);
  EXPECT_EQ(fo4::schema::formlink_fields(sig("PARW")).size(), 9u);
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(sig("PARW"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.subrecord, sig(row.subrecord));
    EXPECT_FALSE(field->scalar.embedded);
    EXPECT_EQ(field->scalar.offset, 0u);
    EXPECT_EQ(field->scalar.storage, row.storage);
    EXPECT_EQ(field->path, row.path);
  }
}

TEST(ParwRows, LinkRowsNameTheirSubrecordsOffsetsAndTargets) {
  for (const auto& row : k_link_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::formlink_field_descriptor(sig("PARW"), row.path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->subrecord, sig(row.subrecord));
    EXPECT_EQ(field->offset, 0u);
    EXPECT_FALSE(field->nullable);
    EXPECT_EQ(field->payload_sizes[0], 4u);
    EXPECT_EQ(field->payload_sizes[1], 0u);
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*field, sig(row.first_target)));
    EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*field, sig("WEAP")));
  }
  const auto emittance = fo4::schema::formlink_field_descriptor(sig("PARW"), "PARW\\Emittance");
  ASSERT_TRUE(emittance.has_value());
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*emittance, sig("REGN")));
}

TEST(ParwRows, ScalarFieldsReadAndWriteTheirPayloadBytes) {
  for (const auto& row : k_scalar_rows) {
    SCOPED_TRACE(row.path);
    const auto field = fo4::schema::scalar_field_descriptor(sig("PARW"), row.path);
    ASSERT_TRUE(field.has_value());
    auto payload = zero_payload(fo4::schema::scalar_storage_size(row.storage));
    const auto encoded = fo4::schema::encode_scalar(row.text, field->scalar);
    ASSERT_EQ(encoded.size(), payload.size());
    fo4::schema::patch_scalar_field(payload, field->scalar, encoded);
    const auto read = fo4::schema::scalar_field_bytes(payload, field->scalar);
    EXPECT_EQ(std::vector<std::byte>(read.begin(), read.end()), encoded);
    EXPECT_EQ(fo4::schema::decode_scalar(read, field->scalar), std::string(row.text));
  }
}

TEST(ParwRows, EmbeddedWritesLeaveTheirSiblingsAlone) {
  const auto field = fo4::schema::scalar_field_descriptor(sig("PARW"), "PARW\\OwnerNoCrime");
  ASSERT_TRUE(field.has_value());
  ASSERT_TRUE(field->scalar.embedded);
  auto payload = std::vector<std::byte>(12, std::byte{0xAB});
  const auto value = fo4::schema::scalar_name_value(field->scalar, "True");
  ASSERT_TRUE(value.has_value());
  const auto encoded = fo4::schema::encode_scalar(std::to_string(*value), field->scalar);
  ASSERT_EQ(encoded.size(), 1u);
  fo4::schema::patch_scalar_field(payload, field->scalar, encoded);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    const bool written = i >= field->scalar.offset && i < field->scalar.offset + encoded.size();
    if (!written) EXPECT_EQ(payload[i], std::byte{0xAB}) << i;
  }
  const auto read = fo4::schema::scalar_field_bytes(payload, field->scalar);
  ASSERT_EQ(read.size(), 1u);
  EXPECT_EQ(payload[field->scalar.offset], encoded[0]);
  EXPECT_EQ(fo4::schema::decode_scalar(read, field->scalar), std::string("True"));
}

TEST(ParwRows, WholePayloadRowsRefuseTheWrongWidth) {
  const auto field = fo4::schema::scalar_field_descriptor(sig("PARW"), "PARW\\AmmoCount");
  ASSERT_TRUE(field.has_value());
  auto short_payload = zero_payload(2);
  EXPECT_THROW(fo4::schema::scalar_field_bytes(short_payload, field->scalar),
               fo4::schema::ScalarError);
}

TEST(ParwRows, SharedSubrecordsNeedTheRecordPath) {
  EXPECT_FALSE(fo4::schema::scalar_descriptor(sig("XAMC")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(sig("XFVC")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(sig("PARW"), "PARW\\Radius").has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_descriptor(sig("PARW"), "PARW\\LockKey").has_value());
}
