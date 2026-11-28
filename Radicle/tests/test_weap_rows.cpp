#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

constexpr auto k_dnam = fo4::Signature::from_string("DNAM");
constexpr auto k_fnam = fo4::Signature::from_string("FNAM");

struct Piece {
  std::string_view signature;
  Bytes data;
};

fo4::FormKey record_key() { return fo4::FormKey::parse("000800:Patch.esp"); }
fo4::FormKey ammo_key() { return fo4::FormKey::parse("000900:Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

Bytes dnam(std::uint16_t unknown_run, std::uint32_t tail) {
  Bytes data(132, std::byte{0});
  const auto put = [&data](std::size_t offset, std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      data[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
    }
  };
  put(4, 0x447A0000u, 4);
  put(110, unknown_run, 2);
  put(128, tail, 4);
  return data;
}

Bytes fnam(std::uint32_t bolt_seconds) {
  Bytes data(41, std::byte{0});
  for (std::size_t i = 0; i < 4; ++i) {
    data[20 + i] = std::byte{static_cast<std::uint8_t>(bolt_seconds >> (8 * i))};
  }
  return data;
}

std::vector<std::byte> make_plugin(const std::vector<Piece>& weapon) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto record = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
  for (const auto& piece : weapon) {
    fo4::test::write_subrecord(out, piece.signature, piece.data);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  const auto ammo_group = fo4::test::begin_group(out, "AMMO", 0);
  const auto ammo = fo4::test::begin_record(out, "AMMO", 0, 0x00000900);
  fo4::test::end_record(out, ammo);
  fo4::test::end_group(out, ammo_group);
  return out.bytes;
}

fo4::Plugin reparse(const fo4::Plugin& plugin) {
  return fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
}

const fo4::Record& record_of(const fo4::Plugin& plugin) {
  return *fo4::find_record(plugin, record_key());
}

const Bytes* payload_of(const fo4::Plugin& plugin, fo4::Signature signature) {
  for (const auto& subrecord : record_of(plugin).subrecords) {
    if (subrecord.signature == signature) return &subrecord.data;
  }
  return nullptr;
}

std::string read_member(const fo4::Plugin& plugin, std::string_view path) {
  const auto field = scalar(path);
  const auto& record = record_of(plugin);
  const auto resolved = fo4::schema::resolve_field_slots(
      record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
  if (resolved.slots.empty() || !resolved.slots[0].valid) return "absent";
  const auto slot = resolved.slots[0];
  return fo4::schema::decode_scalar(
      fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar),
      field.scalar);
}

std::uint32_t read_u32(const Bytes& data, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

std::uint16_t read_u16(const Bytes& data, std::size_t offset) {
  std::uint16_t value = 0;
  for (std::size_t i = 0; i < 2; ++i) {
    value |= static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[offset + i])) << (8 * i);
  }
  return value;
}

std::vector<Piece> base() {
  return {{"DNAM", dnam(0x1234, 0xAABBCCDDu)}, {"FNAM", fnam(0x00112233u)}};
}

}  // namespace

TEST(WeapRows, UnknownDnamAndFnamRunsReadAtTheirOffsets) {
  auto plugin = fo4::read_plugin(make_plugin(base()), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "WEAP\\UnknownAfterAnimationAttackSeconds"), "4660");
  EXPECT_EQ(read_member(plugin, "WEAP\\UnknownEnd"), "2864434397");
  EXPECT_EQ(read_member(plugin, "WEAP\\BoltAnimationSeconds"), "1122867");
  EXPECT_EQ(read_member(plugin, "WEAP\\Speed"), "1000");
  EXPECT_EQ(fo4::record_field_count(plugin, record_key(), "WEAP\\UnknownEnd"), 1u);
}

TEST(WeapRows, ScalarWritesSurviveSaveAndReparse) {
  auto plugin = fo4::read_plugin(make_plugin(base()), "Patch.esp");
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(),
                                   scalar("WEAP\\UnknownAfterAnimationAttackSeconds"), "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("WEAP\\UnknownEnd"), "4294967295"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("WEAP\\BoltAnimationSeconds"), "25"),
            fo4::ScalarMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto* dnam_after = payload_of(after, k_dnam);
  const auto* fnam_after = payload_of(after, k_fnam);
  ASSERT_NE(dnam_after, nullptr);
  ASSERT_NE(fnam_after, nullptr);
  ASSERT_EQ(dnam_after->size(), 132u);
  ASSERT_EQ(fnam_after->size(), 41u);
  EXPECT_EQ(read_u16(*dnam_after, 110), 7u);
  EXPECT_EQ(read_u32(*dnam_after, 128), 4294967295u);
  EXPECT_EQ(read_u32(*fnam_after, 20), 25u);
  EXPECT_EQ(read_u32(*dnam_after, 4), 0x447A0000u);
  EXPECT_EQ(read_member(after, "WEAP\\UnknownAfterAnimationAttackSeconds"), "7");
  EXPECT_EQ(read_member(after, "WEAP\\UnknownEnd"), "4294967295");
  EXPECT_EQ(read_member(after, "WEAP\\BoltAnimationSeconds"), "25");
}

TEST(WeapRows, FormLinkWriteSurvivesSaveAndReparse) {
  auto plugin = fo4::read_plugin(make_plugin(base()), "Patch.esp");
  const auto ammo = fo4::schema::formlink_field_descriptor("WEAP\\Ammo").value();
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, record_key(), ammo, ammo_key()),
            fo4::FormLinkFieldMutationStatus::updated);

  const auto after = reparse(plugin);
  const auto* dnam_after = payload_of(after, k_dnam);
  ASSERT_NE(dnam_after, nullptr);
  ASSERT_EQ(dnam_after->size(), 132u);
  EXPECT_EQ(read_u32(*dnam_after, 0), 0x00000900u);
  EXPECT_EQ(read_u16(*dnam_after, 110), 0x1234u);
  EXPECT_EQ(read_u32(*dnam_after, 128), 0xAABBCCDDu);
}

TEST(WeapRows, ShortDnamPayloadHidesTheUnknownRunsAndRefusesTheWrite) {
  auto short_dnam = base();
  short_dnam[0].data.resize(131);
  auto plugin = fo4::read_plugin(make_plugin(short_dnam), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "WEAP\\UnknownAfterAnimationAttackSeconds"), "absent");
  EXPECT_EQ(read_member(plugin, "WEAP\\UnknownEnd"), "absent");
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("WEAP\\UnknownEnd"), "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(read_member(plugin, "WEAP\\BoltAnimationSeconds"), "1122867");
}

TEST(WeapRows, ShortFnamPayloadHidesTheBoltRunAndRefusesTheWrite) {
  auto short_fnam = base();
  short_fnam[1].data.resize(40);
  auto plugin = fo4::read_plugin(make_plugin(short_fnam), "Patch.esp");
  EXPECT_EQ(read_member(plugin, "WEAP\\BoltAnimationSeconds"), "absent");
  EXPECT_EQ(fo4::set_record_scalar(plugin, record_key(), scalar("WEAP\\BoltAnimationSeconds"), "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(read_member(plugin, "WEAP\\UnknownEnd"), "2864434397");
}

TEST(WeapRows, OutOfRangeWriteLeavesTheRecordBytesUnchanged) {
  auto plugin = fo4::read_plugin(make_plugin(base()), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record_key(), scalar("WEAP\\UnknownAfterAnimationAttackSeconds"), "65536"),
               fo4::schema::ScalarError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
