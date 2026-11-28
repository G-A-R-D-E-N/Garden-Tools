#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr auto k_npc = fo4::Signature::from_string("NPC_");
constexpr auto k_tpta = fo4::Signature::from_string("TPTA");
constexpr auto k_bmmo = fo4::Signature::from_string("BMMO");
constexpr auto k_lvln = fo4::Signature::from_string("LVLN");

struct RowExpectation {
  std::string path;
  std::uint32_t offset;
};

const std::array k_rows{
    RowExpectation{"NPC_\\TemplateActorTraits", 0},
    RowExpectation{"NPC_\\TemplateActorStats", 4},
    RowExpectation{"NPC_\\TemplateActorFactions", 8},
    RowExpectation{"NPC_\\TemplateActorSpellList", 12},
    RowExpectation{"NPC_\\TemplateActorAIData", 16},
    RowExpectation{"NPC_\\TemplateActorAIPackages", 20},
    RowExpectation{"NPC_\\TemplateActorModelAnimation", 24},
    RowExpectation{"NPC_\\TemplateActorBaseData", 28},
    RowExpectation{"NPC_\\TemplateActorInventory", 32},
    RowExpectation{"NPC_\\TemplateActorScript", 36},
    RowExpectation{"NPC_\\TemplateActorDefaultPackageList", 40},
    RowExpectation{"NPC_\\TemplateActorAttackData", 44},
    RowExpectation{"NPC_\\TemplateActorKeywords", 48},
};

fo4::Signature sig(std::string_view text) { return fo4::Signature::from_string(text); }

fo4::FormKey key(std::uint32_t local) {
  char text[64];
  std::snprintf(text, sizeof(text), "%06X:Patch.esp", local);
  return fo4::FormKey::parse(text);
}

fo4::FormKey npc_key() { return key(0x000800); }

fo4::schema::FormLinkFieldDescriptor row(std::string_view path) {
  return fo4::schema::formlink_field_descriptor(path).value();
}

Bytes tpta_bytes(const std::vector<std::uint32_t>& values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::uint32_t> tpta_default_values() {
  std::vector<std::uint32_t> values;
  for (std::uint32_t index = 0; index < k_rows.size(); ++index) {
    values.push_back(0x00000100u + index * 0x10u);
  }
  return values;
}

void add_group(ByteWriter& out, std::string_view signature, const std::vector<std::uint32_t>& locals) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  for (const auto local : locals) {
    const auto record = fo4::test::begin_record(out, signature, 0, local);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_plugin(Bytes payload = tpta_bytes(tpta_default_values())) {
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
  fo4::test::write_subrecord(out, "TPTA", payload);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  add_group(out, "BMMO", {0x00000A00u});
  add_group(out, "LVLN", {0x00000A01u});
  add_group(out, "WEAP", {0x00000A02u});
  return out.bytes;
}

std::vector<std::uint32_t> tpta_words(const fo4::Plugin& plugin) {
  std::vector<std::uint32_t> out;
  const auto* record = fo4::find_record(plugin, npc_key());
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != k_tpta) continue;
    for (std::size_t offset = 0; offset + 4 <= subrecord.data.size(); offset += 4) {
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(subrecord.data[offset + byte]))
                 << (8 * byte);
      }
      out.push_back(value);
    }
  }
  return out;
}

}  // namespace

TEST(NpcTemplateActorRows, RowsDeclareEveryTemplateSlotAndTarget) {
  for (const auto& expected : k_rows) {
    const auto descriptor = row(expected.path);
    EXPECT_EQ(descriptor.record, k_npc) << expected.path;
    EXPECT_EQ(descriptor.subrecord, k_tpta) << expected.path;
    EXPECT_EQ(descriptor.offset, expected.offset) << expected.path;
    EXPECT_TRUE(descriptor.nullable) << expected.path;
    EXPECT_FALSE(descriptor.address.repeated) << expected.path;
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(descriptor, k_bmmo)) << expected.path;
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(descriptor, k_lvln)) << expected.path;
    EXPECT_TRUE(fo4::schema::formlink_field_target_allowed(descriptor, k_npc)) << expected.path;
    EXPECT_FALSE(fo4::schema::formlink_field_target_allowed(descriptor, sig("WEAP"))) << expected.path;
    EXPECT_TRUE(fo4::schema::formlink_field_descriptor(k_npc, expected.path).has_value()) << expected.path;
  }
}

TEST(NpcTemplateActorRows, WritesReachOnlyTheirOwnSlot) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\TemplateActorKeywords"), 1u);
  EXPECT_EQ(fo4::record_field_count(plugin, npc_key(), "NPC_\\TemplateActorTraits"), 1u);

  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorKeywords"), 0,
                                           key(0x000A00)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorTraits"), 0, npc_key()),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorAttackData"), 0,
                                           key(0x000A01)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorAttackData"), 0,
                                           key(0x000A01)),
            fo4::FormLinkFieldMutationStatus::unchanged);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  auto expected = tpta_default_values();
  expected[0] = 0x00000800u;
  expected[11] = 0x00000A01u;
  expected[12] = 0x00000A00u;
  EXPECT_EQ(tpta_words(after), expected);
}

TEST(NpcTemplateActorRows, NullTargetAndOutOfRangeIndexAreHandled) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorStats"), 0, fo4::FormKey{}),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorStats"), 1, key(0x000A00)),
            fo4::FormLinkFieldMutationStatus::subrecord_missing);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  auto expected = tpta_default_values();
  expected[1] = 0u;
  EXPECT_EQ(tpta_words(after), expected);
}

TEST(NpcTemplateActorRows, WrongTargetTypeIsRefused) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorFactions"), 0,
                                              key(0x000A02)),
               fo4::schema::FormLinkFieldError);
  EXPECT_EQ(tpta_words(plugin), tpta_default_values());
}

TEST(NpcTemplateActorRows, ShortPayloadRefusesSlotsPastItsEnd) {
  auto plugin = fo4::read_plugin(make_plugin(tpta_bytes({0x00000200u, 0x00000201u, 0x00000202u})), "Patch.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorFactions"), 0,
                                           key(0x000A00)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_THROW(fo4::set_record_formlink_field(plugin, npc_key(), row("NPC_\\TemplateActorKeywords"), 0,
                                              key(0x000A00)),
               fo4::schema::FormLinkFieldError);

  const auto after = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(tpta_words(after), (std::vector<std::uint32_t>{0x00000200u, 0x00000201u, 0x00000A00u}));
}
