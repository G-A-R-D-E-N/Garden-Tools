#include "fo4/formid.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin_key.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

struct Sub {
  std::string_view signature;
  Bytes data;
};

Bytes u8(std::uint8_t value) { return Bytes{std::byte{value}}; }

Bytes i8(std::int8_t value) { return Bytes{std::byte{static_cast<std::uint8_t>(value)}}; }

Bytes u32(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

Bytes f32(float value) {
  fo4::test::ByteWriter out;
  out.f32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes zeros(std::size_t size) { return Bytes(size, std::byte{0}); }

Bytes join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{local}};
}

void write_body(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                const std::vector<Sub>& subrecords) {
  const auto record = fo4::test::begin_record(out, signature, 0, form_id);
  for (const auto& subrecord : subrecords) {
    fo4::test::write_subrecord(out, subrecord.signature, std::span<const std::byte>{subrecord.data});
  }
  fo4::test::end_record(out, record);
}

void write_record(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t form_id,
                  const std::vector<Sub>& subrecords) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  write_body(out, signature, form_id, subrecords);
  fo4::test::end_group(out, group);
}

// source:wbDefinitionsFO4.pas @01a11729; fixture payloads must match the R5 rows in fields.json.
std::vector<std::byte> make_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0u, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(2304);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  write_record(out, "NPC_", 3100, {{"EDID", zstr("RadicleActorOne")}});
  write_record(out, "NPC_", 3101, {{"EDID", zstr("RadicleActorTwo")}});
  write_record(out, "FACT", 3102, {{"EDID", zstr("RadicleFactionOne")}});
  write_record(out, "FACT", 3103, {{"EDID", zstr("RadicleFactionTwo")}});
  write_record(out, "LCTN", 3104, {{"EDID", zstr("RadicleLocationOne")}});
  write_record(out, "ASTP", 3107, {{"EDID", zstr("RadicleAssociationOne")}});
  write_record(out, "PROJ", 3108, {{"EDID", zstr("RadicleProjectileOne")}});
  write_record(out, "PROJ", 3109, {{"EDID", zstr("RadicleProjectileTwo")}});
  write_record(out, "EXPL", 3110, {{"EDID", zstr("RadicleExplosion")}});
  write_record(out, "EFSH", 3111, {{"EDID", zstr("RadicleShader")}});
  write_record(out, "ARTO", 3112, {{"EDID", zstr("RadicleArt")}});
  write_record(out, "IPDS", 3113, {{"EDID", zstr("RadicleImpact")}});
  write_record(out, "TXST", 3114, {{"EDID", zstr("RadicleTextureOne")}});
  write_record(out, "TXST", 3115, {{"EDID", zstr("RadicleTextureTwo")}});
  write_record(out, "CLFM", 3116, {{"EDID", zstr("RadicleColorOne")}});
  write_record(out, "FLST", 3117, {{"EDID", zstr("RadicleRacesOne")}});
  write_record(out, "FLST", 3118, {{"EDID", zstr("RadicleRacesTwo")}});
  write_record(out, "HDPT", 3119, {{"EDID", zstr("RadicleHeadOne")}});
  write_record(out, "HDPT", 3120, {{"EDID", zstr("RadicleHeadTwo")}});
  write_record(out, "ASTP", 3131, {{"EDID", zstr("RadicleAssociationTwo")}});
  write_record(out, "LCTN", 3132, {{"EDID", zstr("RadicleLocationTwo")}});
  write_record(out, "CLFM", 3133, {{"EDID", zstr("RadicleColorTwo")}});

  write_record(out, "CLAS", 3121, {{"EDID", zstr("RadicleClass")}, {"DESC", zeros(4)}, {"ICON", zstr("old.dds")},
                                   {"DATA", join({zeros(4), f32(1.0f)})}});
  write_record(out, "AVIF", 3122, {{"EDID", zstr("RadicleValue")}, {"DESC", zeros(4)}, {"NAM0", f32(1.0f)},
                                   {"AVFL", u32(0)}, {"NAM1", u32(8)}});
  write_record(out, "RELA", 3123, {{"EDID", zstr("RadicleRelation")},
                                   {"DATA", join({u32(3100), u32(3100), u8(3), zeros(2), u8(0), u32(3107)})}});
  write_record(out, "HDPT", 3124, {{"EDID", zstr("RadicleHead")}, {"DATA", u8(0)}, {"PNAM", u32(0)},
                                   {"HNAM", u32(3119)}, {"TNAM", u32(3114)}, {"CNAM", u32(3116)},
                                   {"RNAM", u32(3117)}});
  write_record(out, "ECZN", 3125, {{"EDID", zstr("RadicleZone")},
                                   {"DATA", join({u32(3100), u32(3104), i8(4), i8(1), u8(0), i8(5)})}});
  write_record(out, "EQUP", 3126, {{"EDID", zstr("RadicleSlot")}, {"DATA", u32(1)}});
  write_record(out, "MOVT", 3127, {{"EDID", zstr("RadicleMovement")}, {"MNAM", zstr("Walk")},
                                   {"INAM", join({f32(1.0f), f32(2.0f), f32(3.0f)})}, {"JNAM", f32(0.5f)},
                                   {"LNAM", f32(0.1f)}});
  write_record(out, "COLL", 3128, {{"EDID", zstr("RadicleLayer")}, {"DESC", zeros(4)}, {"BNAM", u32(1)},
                                   {"GNAM", u32(0)}, {"MNAM", zstr("Old")}, {"INTV", u32(0)}});
  write_record(out, "DUAL", 3129, {{"EDID", zstr("RadicleDualCast")},
                                   {"DATA", join({u32(3108), u32(3110), u32(3111), u32(3112), u32(3113), u32(0)})}});
  write_record(out, "ANIO", 3130, {{"EDID", zstr("RadicleAnimated")}, {"BNAM", zstr("old")}});
  return out.bytes;
}

fo4::Plugin load() { return fo4::read_plugin(make_plugin(), "Patch.esp"); }

fo4::schema::ScalarFieldDescriptor scalar(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown scalar row " + std::string(path));
  return *field;
}

fo4::schema::FormLinkFieldDescriptor link(std::string_view path) {
  const auto field = fo4::schema::formlink_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown FormLink row " + std::string(path));
  return *field;
}

fo4::schema::FormLinkListDescriptor list(std::string_view record, std::string_view path) {
  const auto descriptor = fo4::schema::formlink_list_descriptor(fo4::Signature::from_string(record), path);
  if (!descriptor) throw std::invalid_argument("unknown FormLink list row " + std::string(path));
  return *descriptor;
}

Bytes payload(const fo4::Plugin& plugin, std::uint32_t local, std::string_view signature) {
  const auto* record = fo4::find_record(plugin, key(local));
  if (record == nullptr) return {};
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) return subrecord.data;
  }
  return {};
}

}  // namespace

TEST(ActorSupportSynthetic, ClassAndValueRowsWriteOnlyTheirOwnBytes) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3121), scalar("CLAS\\BleedoutDefault"), 0, "0.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3121), "CLAS\\InventoryImage", 0, "new.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3122), scalar("AVIF\\DefaultValue"), 0, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3122), scalar("AVIF\\Flags"), 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3122), scalar("AVIF\\Type"), 0, "6"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3130), "ANIO\\UnloadEvent", 0, "unload"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3121, "DATA"), join({zeros(4), f32(0.5f)}));
  EXPECT_EQ(payload(reread, 3121, "ICON"), zstr("new.dds"));
  EXPECT_EQ(fo4::record_field_count(reread, key(3121), "CLAS\\BleedoutDefault"), 1u);
  EXPECT_EQ(payload(reread, 3122, "NAM0"), f32(2.5f));
  EXPECT_EQ(payload(reread, 3122, "AVFL"), u32(3));
  EXPECT_EQ(payload(reread, 3122, "NAM1"), u32(6));
  EXPECT_EQ(payload(reread, 3130, "BNAM"), zstr("unload"));
}

TEST(ActorSupportSynthetic, RelationAndZoneLinksRetargetToSecondRecords) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3123), scalar("RELA\\Rank"), 0, "6"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3123), scalar("RELA\\Flags"), 0, "129"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3123), link("RELA\\Parent"), key(3101)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3123), link("RELA\\AssociationType"), key(3131)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3125), link("ECZN\\Owner"), key(3103)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3125), link("ECZN\\Location"), key(3132)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3125), scalar("ECZN\\Rank"), 0, "-3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3125), scalar("ECZN\\Flags"), 0, "5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3125), scalar("ECZN\\MaxLevel"), 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3126), scalar("EQUP\\Flags"), 0, "5"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3123, "DATA"),
            join({u32(3101), u32(3100), u8(6), zeros(2), u8(129), u32(3131)}));
  EXPECT_EQ(fo4::record_field_count(reread, key(3123), link("RELA\\AssociationType")), 1u);
  EXPECT_EQ(payload(reread, 3125, "DATA"),
            join({u32(3103), u32(3132), i8(-3), i8(1), u8(5), i8(7)}));
  EXPECT_EQ(payload(reread, 3126, "DATA"), u32(5));
}

TEST(ActorSupportSynthetic, HeadPartRowsAndExtraPartsListRetarget) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3124), scalar("HDPT\\Flags"), 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3124), scalar("HDPT\\Type"), 0, "4"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3124), link("HDPT\\TextureSet"), key(3115)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3124), link("HDPT\\Color"), key(3133)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3124), link("HDPT\\ValidRaces"), key(3118)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(3124), list("HDPT", "HDPT\\ExtraParts"), 0,
                                                          key(3120)),
            fo4::schema::FormLinkListMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3124, "DATA"), u8(3));
  EXPECT_EQ(payload(reread, 3124, "PNAM"), u32(4));
  EXPECT_EQ(payload(reread, 3124, "TNAM"), u32(3115));
  EXPECT_EQ(payload(reread, 3124, "CNAM"), u32(3133));
  EXPECT_EQ(payload(reread, 3124, "RNAM"), u32(3118));
  EXPECT_EQ(payload(reread, 3124, "HNAM"), u32(3120));
}

TEST(ActorSupportSynthetic, MovementCollisionAndDualCastRowsWriteTheirPayloads) {
  auto plugin = load();
  EXPECT_EQ(fo4::set_record_string(plugin, key(3127), "MOVT\\Name", 0, "Sprint"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3127), scalar("MOVT\\MovementSpeedThreshold"), 0, "2.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3127), scalar("MOVT\\FloatHeight"), 0, "1.5"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3128), scalar("COLL\\Index"), 0, "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3128), scalar("COLL\\Flags"), 0, "2"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3128), scalar("COLL\\InteractablesCount"), 0, "3"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key(3128), "COLL\\Name", 0, "Radicle"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_formlink_field(plugin, key(3129), link("DUAL\\Projectile"), key(3109)),
            fo4::FormLinkFieldMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(3129), scalar("DUAL\\InheritScale"), 0, "3"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  EXPECT_EQ(payload(reread, 3127, "MNAM"), zstr("Sprint"));
  EXPECT_EQ(payload(reread, 3127, "INAM"), join({f32(1.0f), f32(2.5f), f32(3.0f)}));
  EXPECT_EQ(payload(reread, 3127, "JNAM"), f32(1.5f));
  EXPECT_EQ(payload(reread, 3128, "BNAM"), u32(7));
  EXPECT_EQ(payload(reread, 3128, "GNAM"), u32(2));
  EXPECT_EQ(payload(reread, 3128, "INTV"), u32(3));
  EXPECT_EQ(payload(reread, 3128, "MNAM"), zstr("Radicle"));
  EXPECT_EQ(payload(reread, 3129, "DATA"), join({u32(3109), u32(3110), u32(3111), u32(3112), u32(3113), u32(3)}));
}
