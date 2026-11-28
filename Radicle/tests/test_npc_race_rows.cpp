#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/field_address.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_npc = fo4::Signature::from_string("NPC_");

fo4::FormKey npc_key() { return fo4::FormKey::parse("000800:Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  return fo4::schema::scalar_field_descriptor(path).value();
}

Bytes u32_words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes f32_words(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const Bytes& keys, const Bytes& values) {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x911);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "NPC_", 0);
  const auto record = fo4::test::begin_record(out, "NPC_", 0, 0x000800);
  fo4::test::write_subrecord(out, "MSDK", keys);
  fo4::test::write_subrecord(out, "MSDV", values);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

Bytes payload(const fo4::Plugin& plugin, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return {};
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == wanted) return subrecord.data;
  }
  return {};
}

}  // namespace

TEST(NpcMorphRows, MorphKeysAndValuesDeclareStridedArrayMembers) {
  const auto keys = scalar("NPC_\\MorphKeys");
  EXPECT_EQ(keys.record, k_npc);
  EXPECT_EQ(keys.scalar.subrecord, fo4::Signature::from_string("MSDK"));
  EXPECT_EQ(keys.scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(keys.scalar.address.stride, 4u);
  EXPECT_FALSE(keys.scalar.address.repeated);
  EXPECT_TRUE(keys.scalar.embedded);

  const auto values = scalar("NPC_\\MorphValues");
  EXPECT_EQ(values.record, k_npc);
  EXPECT_EQ(values.scalar.subrecord, fo4::Signature::from_string("MSDV"));
  EXPECT_EQ(values.scalar.storage, fo4::schema::ScalarStorage::f32);
  EXPECT_EQ(values.scalar.address.stride, 4u);
  EXPECT_FALSE(values.scalar.address.repeated);
  EXPECT_TRUE(values.scalar.embedded);
}

TEST(NpcMorphRows, MorphArrayElementsResolveAndWriteBack) {
  const auto keys = scalar("NPC_\\MorphKeys");
  const auto values = scalar("NPC_\\MorphValues");
  const auto out = make_plugin(u32_words({0x11u, 0x22u, 0x33u}), f32_words({0.25f, 0.5f}));
  auto plugin = fo4::read_plugin(out, "Patch.esp");
  const auto* record = fo4::find_record(plugin, npc_key());
  ASSERT_NE(record, nullptr);

  const auto key_slots = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(keys.scalar), keys.scalar.address);
  const auto value_slots = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(values.scalar), values.scalar.address);
  EXPECT_EQ(key_slots.slots.size(), 3u);
  EXPECT_EQ(value_slots.slots.size(), 2u);

  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), keys, 1, "99"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, npc_key(), values, 0, "2.5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, "MSDK"), u32_words({0x11u, 99u, 0x33u}));
  EXPECT_EQ(payload(reread, "MSDV"), f32_words({2.5f, 0.5f}));
}