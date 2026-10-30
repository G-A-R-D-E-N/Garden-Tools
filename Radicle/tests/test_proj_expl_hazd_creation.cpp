// Typed creation for PROJ, EXPL and HAZD.
//
// The payload layouts these encoders write are measured, not derived from
// CommonLibF4's in-memory structs -- see docs/FORMAT_NOTES.md. The checks here
// are structural and round-trip only; test_proj_expl_hazd_reencode_real.cpp
// carries the stronger claim that re-encoding a shipped record reproduces its
// bytes.

#include "fo4/forms/proj_expl_hazd.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

// A plugin holding one record of `existing_signature` at 000800, so tests can
// point a typed FormKey slot at something real.
std::vector<std::byte> make_target(std::string_view existing_signature) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, existing_signature, 0);
  const auto record = fo4::test::begin_record(out, existing_signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"ExistingTarget\0", 15});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(ProjExplHazdCreation, ProjectileWritesTheMeasuredNinetyThreeByteDnam) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "TestProjectile";
  creation.speed = 12000.0F;
  creation.range = 4096.0F;
  creation.explosion_type = fo4::FormKey::parse("000800:Patch.esp");
  creation.tracer_frequency = 4;
  creation.muzzle_flash_model = "Effects\\MuzPistolSmall01.nif";
  creation.sound_level = 2;

  const auto key = fo4::forms::create_projectile(plugin, creation);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.signature.to_string(), "PROJ");

  const auto* dnam = find_subrecord(*record, "DNAM");
  ASSERT_NE(dnam, nullptr);
  EXPECT_EQ(dnam->data.size(), fo4::forms::k_proj_dnam_size);

  // DATA is a zero-length marker in all 239 shipped records, not a payload.
  const auto* data = find_subrecord(*record, "DATA");
  ASSERT_NE(data, nullptr);
  EXPECT_TRUE(data->data.empty());

  const auto decoded = fo4::forms::decode_projectile(reread, key);
  EXPECT_EQ(decoded.speed, 12000.0F);
  EXPECT_EQ(decoded.range, 4096.0F);
  EXPECT_EQ(decoded.explosion_type, creation.explosion_type);
  EXPECT_EQ(decoded.tracer_frequency, 4u);
  EXPECT_EQ(decoded.muzzle_flash_model, creation.muzzle_flash_model);
  EXPECT_EQ(decoded.sound_level, 2u);
  EXPECT_EQ(decoded.editor_id, "TestProjectile");
}

// vatsProjectile is the last four bytes of the DNAM and starts at +89, so it
// straddles the natural alignment. It was unwired until the layout was
// measured; a creator that drops it would silently lose the link.
TEST(ProjExplHazdCreation, ProjectileRoundTripsTheUnalignedVatsSlot) {
  auto plugin = fo4::read_plugin(make_target("PROJ"), "Patch.esp");

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "VatsHost";
  creation.vats_projectile = fo4::FormKey::parse("000800:Patch.esp");
  creation.tracer_frequency = 1;

  const auto key = fo4::forms::create_projectile(plugin, creation);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto decoded = fo4::forms::decode_projectile(reread, key);
  EXPECT_EQ(decoded.vats_projectile, creation.vats_projectile);
  EXPECT_EQ(decoded.tracer_frequency, 1u);

  // The neighbouring byte must not be disturbed by the unaligned write.
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* dnam = find_subrecord(*record, "DNAM");
  ASSERT_NE(dnam, nullptr);
  ASSERT_EQ(dnam->data.size(), fo4::forms::k_proj_dnam_size);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[88]), 1u);
}

TEST(ProjExplHazdCreation, ProjectileFlagsAndTypeOccupySeparateLeadingWords) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "SplitProjectile";
  creation.flags = 0x0005;
  creation.type = 0x0040;
  creation.explosion_type = fo4::FormKey::parse("000800:Patch.esp");

  const auto key = fo4::forms::create_projectile(plugin, creation);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* dnam = find_subrecord(*record, "DNAM");
  ASSERT_NE(dnam, nullptr);
  ASSERT_EQ(dnam->data.size(), fo4::forms::k_proj_dnam_size);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[0]), 0x05u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[1]), 0x00u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[2]), 0x40u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[3]), 0x00u);

  const auto decoded = fo4::forms::decode_projectile(reread, key);
  EXPECT_EQ(decoded.flags, 0x0005u);
  EXPECT_EQ(decoded.type, 0x0040u);
}

TEST(ProjExplHazdCreation, ProjectileTypeDefaultsToMissile) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "DefaultProjectile";
  creation.explosion_type = fo4::FormKey::parse("000800:Patch.esp");

  const auto key = fo4::forms::create_projectile(plugin, creation);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* dnam = find_subrecord(*record, "DNAM");
  ASSERT_NE(dnam, nullptr);
  ASSERT_EQ(dnam->data.size(), fo4::forms::k_proj_dnam_size);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[0]), 0x00u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[1]), 0x00u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[2]), 0x01u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(dnam->data[3]), 0x00u);
}

TEST(ProjExplHazdCreation, ProjectileFlagsRefuseValuesPastU16AndTypeIsTheSecondWord) {
  const auto flags = fo4::schema::scalar_field_descriptor("PROJ\\Flags");
  ASSERT_TRUE(flags.has_value());
  EXPECT_EQ(fo4::schema::encode_scalar("65535", flags->scalar),
            (std::vector<std::byte>{std::byte{0xFF}, std::byte{0xFF}}));
  EXPECT_THROW(fo4::schema::encode_scalar("65536", flags->scalar), fo4::schema::ScalarError);

  const auto type = fo4::schema::scalar_field_descriptor("PROJ\\Type");
  ASSERT_TRUE(type.has_value());
  std::vector<std::byte> dnam(fo4::forms::k_proj_dnam_size, std::byte{0});
  fo4::schema::patch_scalar_field(dnam, type->scalar, fo4::schema::encode_scalar("64", type->scalar));
  EXPECT_EQ(dnam[0], std::byte{0x00});
  EXPECT_EQ(dnam[1], std::byte{0x00});
  EXPECT_EQ(dnam[2], std::byte{0x40});
  EXPECT_EQ(dnam[3], std::byte{0x00});
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(dnam, type->scalar), type->scalar),
            "64");
}

TEST(ProjExplHazdCreation, ExplosionWritesTheMeasuredEightyFourByteData) {
  auto plugin = fo4::read_plugin(make_target("PROJ"), "Patch.esp");

  fo4::forms::ExplosionCreation creation;
  creation.editor_id = "TestExplosion";
  creation.spawn_projectile = fo4::FormKey::parse("000800:Patch.esp");
  creation.force = 1024.0F;
  creation.damage = 25.0F;
  creation.inner_radius = 300.0F;
  creation.outer_radius = 1024.0F;
  creation.sound_level = 2;
  creation.stagger_magnitude = 3;
  creation.spawn_projectile_vector = {0.0F, 1.0F, -0.25F};
  creation.projectile_spread = 180.0F;
  creation.projectile_count = 15;

  const auto key = fo4::forms::create_explosion(plugin, creation);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* data = find_subrecord(*record, "DATA");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->data.size(), fo4::forms::k_expl_data_size);

  const auto decoded = fo4::forms::decode_explosion(reread, key);
  EXPECT_EQ(decoded.spawn_projectile, creation.spawn_projectile);
  EXPECT_EQ(decoded.force, 1024.0F);
  EXPECT_EQ(decoded.inner_radius, 300.0F);
  EXPECT_EQ(decoded.sound_level, 2u);
  EXPECT_EQ(decoded.stagger_magnitude, 3u);
  EXPECT_EQ(decoded.spawn_projectile_vector, creation.spawn_projectile_vector);
  EXPECT_EQ(decoded.projectile_spread, 180.0F);
  EXPECT_EQ(decoded.projectile_count, 15u);
}

TEST(ProjExplHazdCreation, HazardWritesTheMeasuredFiftyTwoByteDnam) {
  auto plugin = fo4::read_plugin(make_target("SPEL"), "Patch.esp");

  fo4::forms::HazardCreation creation;
  creation.editor_id = "TestHazard";
  creation.model = "Effects\\FXFireMedSmokey01.nif";
  creation.limit = 20;
  creation.radius = 351.0F;
  creation.lifetime = 30.0F;
  creation.target_interval = 0.1F;
  creation.flags = 36;
  creation.spell = fo4::FormKey::parse("000800:Patch.esp");
  creation.taper_weight = 1.0F;

  const auto key = fo4::forms::create_hazard(plugin, creation);
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* dnam = find_subrecord(*record, "DNAM");
  ASSERT_NE(dnam, nullptr);
  EXPECT_EQ(dnam->data.size(), fo4::forms::k_hazd_dnam_size);

  // MODL is required and MODT defaults to the attested empty form.
  const auto* modt = find_subrecord(*record, "MODT");
  ASSERT_NE(modt, nullptr);
  EXPECT_EQ(modt->data, fo4::forms::empty_model_texture_data());

  const auto decoded = fo4::forms::decode_hazard(reread, key);
  EXPECT_EQ(decoded.model, creation.model);
  EXPECT_EQ(decoded.limit, 20u);
  EXPECT_EQ(decoded.radius, 351.0F);
  EXPECT_EQ(decoded.flags, 36u);
  EXPECT_EQ(decoded.spell, creation.spell);
  EXPECT_EQ(decoded.taper_weight, 1.0F);
}

TEST(ProjExplHazdCreation, HazardRefusesAMissingModel) {
  auto plugin = fo4::read_plugin(make_target("SPEL"), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);

  fo4::forms::HazardCreation creation;
  creation.editor_id = "NoModel";
  creation.spell = fo4::FormKey::parse("000800:Patch.esp");

  EXPECT_THROW(fo4::forms::create_hazard(plugin, creation), fo4::forms::ProjExplHazdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// A local FormKey must resolve to the record type the slot holds. A wrong type
// here is the mistake that produces a plausible-looking, broken record.
TEST(ProjExplHazdCreation, RejectsLocalFormKeyOfTheWrongType) {
  auto plugin = fo4::read_plugin(make_target("SPEL"), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "BadLight";
  creation.light = fo4::FormKey::parse("000800:Patch.esp");  // a SPEL, not a LIGH

  EXPECT_THROW(fo4::forms::create_projectile(plugin, creation), fo4::forms::ProjExplHazdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(ProjExplHazdCreation, RejectsALocalFormKeyThatDoesNotExist) {
  auto plugin = fo4::read_plugin(make_target("LIGH"), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "DanglingLight";
  creation.light = fo4::FormKey::parse("000FFF:Patch.esp");

  EXPECT_THROW(fo4::forms::create_projectile(plugin, creation), fo4::forms::ProjExplHazdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(ProjExplHazdCreation, RejectsAnEditorIdContainingANullByte) {
  auto plugin = fo4::read_plugin(make_target("LIGH"), "Patch.esp");

  fo4::forms::ExplosionCreation creation;
  creation.editor_id = std::string{"Bad\0Id", 6};

  EXPECT_THROW(fo4::forms::create_explosion(plugin, creation), fo4::forms::ProjExplHazdError);
}

TEST(ProjExplHazdCreation, StoresEditorIdAsWindows1252) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "Caf\xC3\xA9";
  creation.explosion_type = fo4::FormKey::parse("000800:Patch.esp");

  const auto key = fo4::forms::create_projectile(plugin, creation);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto* edid = find_subrecord(*record, "EDID");
  ASSERT_NE(edid, nullptr);
  const std::vector<std::byte> expected{
      std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(edid->data, expected);
}

TEST(ProjExplHazdCreation, EditorIdOutsideWindows1252IsRefusedBeforeAnyMutation) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "Snow\xE2\x98\x83";
  creation.explosion_type = fo4::FormKey::parse("000800:Patch.esp");

  EXPECT_THROW(static_cast<void>(fo4::forms::create_projectile(plugin, creation)),
               fo4::FormIdError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

// Truncated EXPL DATA is a clean prefix of the current shape, so a decoder must
// accept the legacy extents and leave the absent tail at its defaults.
TEST(ProjExplHazdCreation, DecodesTruncatedExplosionDataAsAPrefix) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "EXPL", 0);
  const auto record = fo4::test::begin_record(out, "EXPL", 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::write_subrecord(out, "OBND", std::vector<std::byte>(12, std::byte{0}));
  fo4::test::ByteWriter data;
  for (int i = 0; i < 6; ++i) data.u32(0);   // six FormID slots
  data.f32(500.0F);                          // +24 force
  data.f32(10.0F);                           // +28 damage
  data.f32(0.0F);                            // +32 innerRadius
  data.f32(256.0F);                          // +36 outerRadius
  data.f32(0.0F);                            // +40 imageSpaceRadius
  data.f32(0.3F);                            // +44 verticalOffsetMult
  data.u32(1);                               // +48 flags -- 52-byte shape ends here
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Legacy.esp");
  const auto decoded =
      fo4::forms::decode_explosion(plugin, fo4::FormKey::parse("000800:Legacy.esp"));
  EXPECT_EQ(decoded.force, 500.0F);
  EXPECT_EQ(decoded.outer_radius, 256.0F);
  EXPECT_EQ(decoded.vertical_offset_mult, 0.3F);
  EXPECT_EQ(decoded.flags, 1u);
  // Absent tail stays at its default rather than reading past the payload.
  EXPECT_EQ(decoded.projectile_count, 0u);
  EXPECT_EQ(decoded.stagger_magnitude, 0u);
}

TEST(ProjExplHazdCreation, EmptyEditorIdIsRefusedBeforeAnyMutation) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");
  const auto before = fo4::write_plugin(plugin);
  const auto refusal = [](auto&& create) -> std::string {
    try {
      create();
    } catch (const fo4::forms::ProjExplHazdError& error) {
      return error.what();
    }
    return {};
  };

  fo4::forms::ProjectileCreation projectile;
  projectile.explosion_type = fo4::FormKey::parse("000800:Patch.esp");
  EXPECT_NE(refusal([&] { static_cast<void>(fo4::forms::create_projectile(plugin, projectile)); })
                .find("editor ID is empty"),
            std::string::npos);

  fo4::forms::ExplosionCreation explosion;
  EXPECT_NE(refusal([&] { static_cast<void>(fo4::forms::create_explosion(plugin, explosion)); })
                .find("editor ID is empty"),
            std::string::npos);

  fo4::forms::HazardCreation hazard;
  hazard.model = "Effects\\FXFireMedSmokey01.nif";
  EXPECT_NE(refusal([&] { static_cast<void>(fo4::forms::create_hazard(plugin, hazard)); })
                .find("editor ID is empty"),
            std::string::npos);

  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(ProjExplHazdCreation, EmptyNameWritesNoFullSubrecordButNamedRecordKeepsIt) {
  auto plugin = fo4::read_plugin(make_target("EXPL"), "Patch.esp");

  fo4::forms::ProjectileCreation projectile;
  projectile.editor_id = "NoNameProjectile";
  projectile.name = std::string{};
  projectile.explosion_type = fo4::FormKey::parse("000800:Patch.esp");
  const auto projectile_key = fo4::forms::create_projectile(plugin, projectile);

  fo4::forms::ExplosionCreation explosion;
  explosion.editor_id = "NoNameExplosion";
  explosion.name = std::string{};
  const auto explosion_key = fo4::forms::create_explosion(plugin, explosion);

  explosion.editor_id = "NamedExplosion";
  explosion.name = std::string{"Named Explosion"};
  const auto named_key = fo4::forms::create_explosion(plugin, explosion);

  fo4::forms::HazardCreation hazard;
  hazard.editor_id = "NoNameHazard";
  hazard.name = std::string{};
  hazard.model = "Effects\\FXFireMedSmokey01.nif";
  const auto hazard_key = fo4::forms::create_hazard(plugin, hazard);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  for (const auto& key : {projectile_key, explosion_key, hazard_key}) {
    const auto* record = fo4::find_record(reread, key);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(find_subrecord(*record, "FULL"), nullptr);
  }
  const auto* named = fo4::find_record(reread, named_key);
  ASSERT_NE(named, nullptr);
  EXPECT_NE(find_subrecord(*named, "FULL"), nullptr);
}
