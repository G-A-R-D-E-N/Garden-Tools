#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_refr = fo4::Signature::from_string("REFR");
constexpr auto k_achr = fo4::Signature::from_string("ACHR");

std::uint32_t raw_id(std::uint32_t local_id) { return 0x01000000u | local_id; }

fo4::FormKey patch_key(std::uint32_t local_id) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local_id}};
}

std::string path_of(std::string_view record, std::string_view member) {
  return std::string(record) + "\\" + std::string(member);
}

Bytes floats(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes u32s(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes f32_bytes(float value) { return floats({value}); }

Bytes u32_bytes(std::uint32_t value) { return u32s({value}); }

Bytes owner_bytes(std::uint32_t owner, std::uint8_t no_crime, std::uint8_t tail) {
  ByteWriter out;
  out.u32(owner);
  out.u32(0);
  out.u8(no_crime);
  out.u8(tail);
  out.u8(tail);
  out.u8(tail);
  return out.bytes;
}

void write_record(ByteWriter& out, std::string_view signature, std::uint32_t local_id,
                  std::initializer_list<std::pair<std::string_view, Bytes>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, raw_id(local_id));
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin() {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  write_record(out, "REFR", 0x0C10,
               {{"NAME", u32_bytes(0x00000A20)},
                {"DATA", floats({1.5f, -2.25f, 3.0f, 0.5f, -1.0f, 6.25f})},
                {"XOWN", owner_bytes(0x00000A30, 0x01, 0x7F)},
                {"XRNK", u32_bytes(0)},
                {"XLRM", u32_bytes(0x00000A50)},
                {"XLRM", u32_bytes(0x00000A51)},
                {"XORD", u32s({0x00000A60, 0x00000A61, 0x00000A62, 0x00000A63})}});
  write_record(out, "REFR", 0x0C11,
               {{"DATA", floats({1.0f, 2.0f, 3.0f, 4.0f})},
                {"XORD", u32s({0x00000A70, 0x00000A71, 0x00000A72})}});
  write_record(out, "REFR", 0x0C12, {{"DATA", floats({1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f})}});
  write_record(out, "ACHR", 0x0D10,
               {{"DATA", floats({10.0f, 20.0f, 30.0f, 0.25f, 0.5f, 0.75f})},
                {"XRGB", floats({1.25f, -0.5f, 2.5f})},
                 {"XOWN", owner_bytes(0x00000A31, 0x00, 0x11)}});
  write_record(out, "ACHR", 0x0D11, {{"XRGB", u32_bytes(0x00000007)}});
  return out.bytes;
}

std::optional<Bytes> payload_at(const fo4::Plugin& plugin, std::uint32_t local_id,
                                std::string_view signature, std::size_t wanted = 0) {
  const auto* record = fo4::find_record(plugin, patch_key(local_id));
  if (record == nullptr) return std::nullopt;
  std::size_t seen = 0;
  for (const auto& sub : record->subrecords) {
    if (sub.signature.to_string() != signature) continue;
    if (seen++ == wanted) return Bytes(sub.data.begin(), sub.data.end());
  }
  return std::nullopt;
}

std::optional<Bytes> scalar_value(const fo4::Plugin& plugin, std::uint32_t local_id,
                                 std::string_view record, std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string(record), path);
  if (!field.has_value()) return std::nullopt;
  const auto payload = payload_at(plugin, local_id, field->scalar.subrecord.to_string());
  if (!payload.has_value()) return std::nullopt;
  const auto bytes = fo4::schema::scalar_field_bytes(*payload, field->scalar);
  return Bytes(bytes.begin(), bytes.end());
}

std::optional<Bytes> link_value(const fo4::Plugin& plugin, std::uint32_t local_id,
                                std::string_view record, std::string_view path,
                                std::size_t index = 0) {
  const auto field =
      fo4::schema::formlink_field_descriptor(fo4::Signature::from_string(record), path);
  if (!field.has_value()) return std::nullopt;
  const auto payload = payload_at(plugin, local_id, field->subrecord.to_string(), index);
  if (!payload.has_value() || payload->size() < field->offset + 4) return std::nullopt;
  Bytes out(4);
  std::memcpy(out.data(), payload->data() + field->offset, 4);
  return out;
}

template <typename Operation>
void expect_formlink_error(Operation operation, std::string_view message) {
  try {
    static_cast<void>(operation());
    ADD_FAILURE() << "expected a refusal containing " << message;
  } catch (const fo4::schema::FormLinkFieldError& error) {
    EXPECT_NE(std::string(error.what()).find(message), std::string::npos) << error.what();
  }
}

}  // namespace

TEST(PlacedDataRows, PositionAndRotationRowsAddressTheSixFloatsOfTheDataStruct) {
  const auto position_x = fo4::schema::scalar_field_descriptor(k_refr, "REFR\\PositionX");
  ASSERT_TRUE(position_x.has_value());
  EXPECT_EQ(position_x->scalar.subrecord, fo4::Signature::from_string("DATA"));
  EXPECT_TRUE(position_x->scalar.embedded);
  EXPECT_FALSE(position_x->scalar.address.repeated);
  EXPECT_EQ(position_x->scalar.payload_size, 24u);
  EXPECT_EQ(position_x->scalar.minimum_payload_size, 0u);
  EXPECT_EQ(position_x->scalar.storage, fo4::schema::ScalarStorage::f32);

  const char* members[] = {"PositionX", "PositionY", "PositionZ", "RotationX", "RotationY",
                          "RotationZ"};
  const float values[] = {1.5f, -2.25f, 3.0f, 0.5f, -1.0f, 6.25f};
  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  for (std::size_t index = 0; index < 6; ++index) {
    SCOPED_TRACE(members[index]);
    const auto path = path_of("REFR", members[index]);
    const auto field = fo4::schema::scalar_field_descriptor(k_refr, path);
    ASSERT_TRUE(field.has_value());
    EXPECT_EQ(field->scalar.offset, index * 4);
    const auto bytes = scalar_value(plugin, 0x0C10, "REFR", path);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(*bytes, f32_bytes(values[index]));
  }
}

TEST(PlacedDataRows, ShortOrPaddedDataHidesEveryRowAndRefusesTheWrite) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto rotation_z = fo4::schema::scalar_field_descriptor(k_refr, "REFR\\RotationZ");
  const auto position_x = fo4::schema::scalar_field_descriptor(k_refr, "REFR\\PositionX");
  ASSERT_TRUE(rotation_z.has_value());
  ASSERT_TRUE(position_x.has_value());

  EXPECT_FALSE(scalar_value(plugin, 0x0C11, "REFR", "REFR\\PositionX").has_value());
  EXPECT_FALSE(scalar_value(plugin, 0x0C11, "REFR", "REFR\\RotationZ").has_value());
  EXPECT_FALSE(scalar_value(plugin, 0x0C12, "REFR", "REFR\\RotationZ").has_value());
  const auto short_before = payload_at(plugin, 0x0C11, "DATA");
  const auto padded_before = payload_at(plugin, 0x0C12, "DATA");
  ASSERT_TRUE(short_before.has_value());
  ASSERT_TRUE(padded_before.has_value());
  EXPECT_EQ(short_before->size(), 16u);
  EXPECT_EQ(padded_before->size(), 28u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x0C11), *rotation_z, "2.0"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x0C12), *position_x, "2.0"),
            fo4::ScalarMutationStatus::subrecord_missing);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload_at(reread, 0x0C11, "DATA"), short_before);
  EXPECT_EQ(payload_at(reread, 0x0C12, "DATA"), padded_before);
}

TEST(PlacedDataRows, ActorDataSharesTheReferenceLayout) {
  const char* members[] = {"PositionX", "PositionY", "PositionZ", "RotationX", "RotationY",
                          "RotationZ"};
  for (const auto* member : members) {
    SCOPED_TRACE(member);
    const auto refr = fo4::schema::scalar_field_descriptor(k_refr, path_of("REFR", member));
    const auto achr = fo4::schema::scalar_field_descriptor(k_achr, path_of("ACHR", member));
    ASSERT_TRUE(refr.has_value());
    ASSERT_TRUE(achr.has_value());
    EXPECT_EQ(achr->scalar.subrecord, refr->scalar.subrecord);
    EXPECT_EQ(achr->scalar.offset, refr->scalar.offset);
    EXPECT_EQ(achr->scalar.payload_size, refr->scalar.payload_size);
    EXPECT_EQ(achr->scalar.storage, refr->scalar.storage);
  }

  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto position_x = scalar_value(plugin, 0x0D10, "ACHR", "ACHR\\PositionX");
  const auto rotation_z = scalar_value(plugin, 0x0D10, "ACHR", "ACHR\\RotationZ");
  ASSERT_TRUE(position_x.has_value());
  ASSERT_TRUE(rotation_z.has_value());
  EXPECT_EQ(*position_x, f32_bytes(10.0f));
  EXPECT_EQ(*rotation_z, f32_bytes(0.75f));
  EXPECT_FALSE(scalar_value(plugin, 0x0D11, "ACHR", "ACHR\\PositionY").has_value());
}

TEST(PlacedDataRows, BipedRotationRowsReadTheTwelveByteXrgbAndLeaveTheFourByteBandUntyped) {
  const auto biped_y = fo4::schema::scalar_field_descriptor(k_achr, "ACHR\\BipedRotationY");
  ASSERT_TRUE(biped_y.has_value());
  EXPECT_EQ(biped_y->scalar.subrecord, fo4::Signature::from_string("XRGB"));
  EXPECT_EQ(biped_y->scalar.payload_size, 12u);
  EXPECT_EQ(biped_y->scalar.offset, 4u);

  const auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto x = scalar_value(plugin, 0x0D10, "ACHR", "ACHR\\BipedRotationX");
  const auto y = scalar_value(plugin, 0x0D10, "ACHR", "ACHR\\BipedRotationY");
  const auto z = scalar_value(plugin, 0x0D10, "ACHR", "ACHR\\BipedRotationZ");
  ASSERT_TRUE(x.has_value());
  ASSERT_TRUE(y.has_value());
  ASSERT_TRUE(z.has_value());
  EXPECT_EQ(*x, f32_bytes(1.25f));
  EXPECT_EQ(*y, f32_bytes(-0.5f));
  EXPECT_EQ(*z, f32_bytes(2.5f));
  EXPECT_FALSE(scalar_value(plugin, 0x0D11, "ACHR", "ACHR\\BipedRotationX").has_value());
  EXPECT_FALSE(fo4::schema::scalar_field_descriptor(k_refr, "REFR\\BipedRotationX").has_value());
}

TEST(PlacedDataRows, BaseRowAcceptsEveryBaseObjectSignatureAndRejectsOtherRecords) {
  const auto base = fo4::schema::formlink_field_descriptor(k_refr, "REFR\\Base");
  ASSERT_TRUE(base.has_value());
  EXPECT_EQ(base->subrecord, fo4::Signature::from_string("NAME"));
  EXPECT_EQ(base->offset, 0u);
  EXPECT_EQ(base->payload_sizes[0], 4u);
  EXPECT_TRUE(base->nullable);
  EXPECT_FALSE(base->address.repeated);

  const auto targets = fo4::schema::formlink_field_targets(*base);
  EXPECT_EQ(targets.size(), 44u);
  for (const auto target : targets) {
    EXPECT_TRUE(fo4::schema::is_sig_base_object(target)) << target.to_string();
  }
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*base, fo4::Signature::from_string("STAT")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*base, fo4::Signature::from_string("SCRL")));
  EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(*base, fo4::Signature::from_string("NPC_")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*base, fo4::Signature::from_string("REGN")));
  EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(*base, fo4::Signature::from_string("KYWD")));

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = link_value(plugin, 0x0C10, "REFR", "REFR\\Base");
  ASSERT_TRUE(original.has_value());
  EXPECT_EQ(*original, u32_bytes(0x00000A20));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x0C10), *base,
                                           fo4::FormKey::parse("00000B00:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto written = link_value(reread, 0x0C10, "REFR", "REFR\\Base");
  const auto payload = payload_at(reread, 0x0C10, "NAME");
  ASSERT_TRUE(written.has_value());
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ(*written, u32_bytes(0x00000B00));
  EXPECT_EQ(payload->size(), 4u);
}

TEST(PlacedDataRows, OwnershipRowsKeepTheUnusedTailOfTheTwelveByteOwnerStruct) {
  const auto owner = fo4::schema::formlink_field_descriptor(k_refr, "REFR\\Owner");
  const auto no_crime = fo4::schema::scalar_field_descriptor(k_refr, "REFR\\OwnerNoCrime");
  const auto rank = fo4::schema::scalar_field_descriptor(k_refr, "REFR\\OwnerRank");
  ASSERT_TRUE(owner.has_value());
  ASSERT_TRUE(no_crime.has_value());
  ASSERT_TRUE(rank.has_value());
  EXPECT_EQ(owner->payload_sizes[0], 12u);
  EXPECT_EQ(no_crime->scalar.offset, 8u);
  EXPECT_EQ(no_crime->scalar.payload_size, 12u);
  EXPECT_TRUE(
      fo4::schema::formlink_field_target_allowed(*owner, fo4::Signature::from_string("FACT")));
  EXPECT_TRUE(
      fo4::schema::formlink_field_target_allowed(*owner, fo4::Signature::from_string("NPC_")));
  EXPECT_FALSE(
      fo4::schema::formlink_field_target_allowed(*owner, fo4::Signature::from_string("REFR")));

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto owner_id = link_value(plugin, 0x0C10, "REFR", "REFR\\Owner");
  const auto flag = scalar_value(plugin, 0x0C10, "REFR", "REFR\\OwnerNoCrime");
  ASSERT_TRUE(owner_id.has_value());
  ASSERT_TRUE(flag.has_value());
  EXPECT_EQ(*owner_id, u32_bytes(0x00000A30));
  EXPECT_EQ(*flag, Bytes{std::byte{0x01}});
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x0C10), *no_crime, "0"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, patch_key(0x0C10), *rank, "4"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = payload_at(reread, 0x0C10, "XOWN");
  const auto rank_payload = payload_at(reread, 0x0C10, "XRNK");
  ASSERT_TRUE(payload.has_value());
  ASSERT_TRUE(rank_payload.has_value());
  ASSERT_EQ(payload->size(), 12u);
  EXPECT_EQ((*payload)[8], std::byte{0x00});
  EXPECT_EQ((*payload)[9], std::byte{0x7F});
  EXPECT_EQ((*payload)[10], std::byte{0x7F});
  EXPECT_EQ((*payload)[11], std::byte{0x7F});
  EXPECT_EQ(Bytes(payload->begin(), payload->begin() + 4), u32_bytes(0x00000A30));
  EXPECT_EQ(*rank_payload, u32_bytes(4));

  const auto before = payload_at(plugin, 0x0C10, "XOWN");
  ASSERT_TRUE(before.has_value());
  expect_formlink_error(
      [&] {
        static_cast<void>(
            fo4::set_record_formlink_field(plugin, patch_key(0x0C10), *owner, 0, patch_key(0x0C11)));
      },
      "REFR\\Owner target type is not allowed");
  EXPECT_EQ(payload_at(plugin, 0x0C10, "XOWN"), before);

  const auto achr_owner = fo4::schema::formlink_field_descriptor(k_achr, "ACHR\\Owner");
  ASSERT_TRUE(achr_owner.has_value());
  EXPECT_EQ(link_value(plugin, 0x0D10, "ACHR", "ACHR\\Owner"), u32_bytes(0x00000A31));
  expect_formlink_error(
      [&] {
        static_cast<void>(fo4::set_record_formlink_field(plugin, patch_key(0x0D10), *achr_owner, 0,
                                                         patch_key(0x0C11)));
      },
      "ACHR\\Owner target type is not allowed");
}

TEST(PlacedDataRows, LinkedRoomRowWritesTheSecondOfTwoRepeatedLinks) {
  const auto room = fo4::schema::formlink_field_descriptor(k_refr, "REFR\\LinkedRoom");
  ASSERT_TRUE(room.has_value());
  EXPECT_EQ(room->subrecord, fo4::Signature::from_string("XLRM"));
  EXPECT_EQ(room->payload_sizes[0], 4u);
  EXPECT_TRUE(room->address.repeated);

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto first = link_value(plugin, 0x0C10, "REFR", "REFR\\LinkedRoom", 0);
  const auto second = link_value(plugin, 0x0C10, "REFR", "REFR\\LinkedRoom", 1);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(*first, u32_bytes(0x00000A50));
  EXPECT_EQ(*second, u32_bytes(0x00000A51));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x0C10), *room, 1,
                                           fo4::FormKey::parse("00000B10:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto kept = link_value(reread, 0x0C10, "REFR", "REFR\\LinkedRoom", 0);
  const auto changed = link_value(reread, 0x0C10, "REFR", "REFR\\LinkedRoom", 1);
  ASSERT_TRUE(kept.has_value());
  ASSERT_TRUE(changed.has_value());
  EXPECT_EQ(*kept, u32_bytes(0x00000A50));
  EXPECT_EQ(*changed, u32_bytes(0x00000B10));
}

TEST(PlacedDataRows, OcclusionRowsAddressTheFourSlotsOfOneXordStruct) {
  const auto right = fo4::schema::formlink_field_descriptor(k_refr, "REFR\\LinkedOcclusionRight");
  const auto left = fo4::schema::formlink_field_descriptor(k_refr, "REFR\\LinkedOcclusionLeft");
  const auto bottom =
      fo4::schema::formlink_field_descriptor(k_refr, "REFR\\LinkedOcclusionBottom");
  const auto top = fo4::schema::formlink_field_descriptor(k_refr, "REFR\\LinkedOcclusionTop");
  ASSERT_TRUE(right.has_value());
  ASSERT_TRUE(left.has_value());
  ASSERT_TRUE(bottom.has_value());
  ASSERT_TRUE(top.has_value());
  EXPECT_EQ(right->offset, 0u);
  EXPECT_EQ(left->offset, 4u);
  EXPECT_EQ(bottom->offset, 8u);
  EXPECT_EQ(top->offset, 12u);
  for (const auto* row : {&*right, &*left, &*bottom, &*top}) {
    EXPECT_EQ(row->payload_sizes[0], 16u);
    EXPECT_TRUE(row->nullable);
  }

  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto first_slot = link_value(plugin, 0x0C10, "REFR", "REFR\\LinkedOcclusionRight");
  const auto last_slot = link_value(plugin, 0x0C10, "REFR", "REFR\\LinkedOcclusionTop");
  ASSERT_TRUE(first_slot.has_value());
  ASSERT_TRUE(last_slot.has_value());
  EXPECT_EQ(*first_slot, u32_bytes(0x00000A60));
  EXPECT_EQ(*last_slot, u32_bytes(0x00000A63));
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, patch_key(0x0C10), *top,
                                           fo4::FormKey::parse("00000B20:Fallout4.esm")),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto payload = payload_at(reread, 0x0C10, "XORD");
  ASSERT_TRUE(payload.has_value());
  ASSERT_EQ(payload->size(), 16u);
  EXPECT_EQ(Bytes(payload->begin(), payload->begin() + 12),
            u32s({0x00000A60, 0x00000A61, 0x00000A62}));
  EXPECT_EQ(Bytes(payload->begin() + 12, payload->end()), u32_bytes(0x00000B20));

  const auto short_payload = payload_at(plugin, 0x0C11, "XORD");
  ASSERT_TRUE(short_payload.has_value());
  EXPECT_EQ(short_payload->size(), 12u);
  expect_formlink_error(
      [&] {
        static_cast<void>(fo4::set_record_formlink_field(
            plugin, patch_key(0x0C11), *right, fo4::FormKey::parse("00000B30:Fallout4.esm")));
      },
      "REFR\\LinkedOcclusionRight has unexpected payload size");
  const auto unchanged = payload_at(plugin, 0x0C11, "XORD");
  ASSERT_TRUE(unchanged.has_value());
  EXPECT_EQ(*unchanged, *short_payload);
}
