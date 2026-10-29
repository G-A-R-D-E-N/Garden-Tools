// Scalar fields addressed at a byte offset inside a struct payload.
//
// The original scalar schema assumed a field *was* its whole subrecord, which
// is true of REFR XHLT/XSCL and of nothing in PROJ/EXPL/HAZD -- those pack
// dozens of fields into one DNAM or DATA. The behaviour that matters here is
// that writing one field leaves its neighbours byte-identical; replacing the
// payload, which is what the old write path did, would erase them.

#include "fo4/forms/proj_expl_hazd.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> empty_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() == signature) return &sub;
  }
  return nullptr;
}

fo4::schema::ScalarFieldDescriptor field(std::string_view path) {
  const auto descriptor = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(descriptor.has_value()) << path << " is not in the scalar schema";
  return *descriptor;
}

std::vector<std::byte> expl_plugin_at(std::uint16_t form_version, std::size_t data_size = 84) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "EXPL", 0);
  const auto record = fo4::test::begin_record(out, "EXPL", 0, 0x00000800, form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::ByteWriter data;
  for (int i = 0; i < 6; ++i) data.u32(0);
  data.f32(500.0F);
  data.f32(10.0F);
  data.f32(300.0F);
  data.f32(1024.0F);
  data.f32(0.0F);
  data.f32(0.3F);
  data.u32(1);
  data.u32(3);
  data.f32(0.0F);
  data.u32(2);
  for (int i = 0; i < 5; ++i) data.u32(0);
  std::vector<std::byte> payload = data.bytes;
  payload.resize(data_size);
  fo4::test::write_subrecord(out, "DATA", payload);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

bool exposed(const fo4::Plugin& plugin, const fo4::FormKey& key,
             const fo4::schema::ScalarFieldDescriptor& descriptor) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return false;
  const auto slots = fo4::schema::resolve_field_slots(
      *record, fo4::schema::scalar_field_shape(descriptor.scalar), descriptor.scalar.address);
  return slots.slots.size() == 1 && slots.slots[0].valid;
}

std::vector<std::byte> one_subrecord_plugin(std::string_view signature, std::string_view subrecord,
                                            std::uint16_t form_version,
                                            const std::vector<std::byte>& payload) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x00000800, form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::write_subrecord(out, subrecord, payload);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> imad_dnam(std::size_t size) {
  fo4::test::ByteWriter data;
  for (int i = 0; i < 60; ++i) data.u32(0);
  data.u32(3);
  data.u32(7);
  data.u32(9);
  data.bytes.resize(size);
  return data.bytes;
}

std::vector<std::byte> ench_enit(std::size_t size) {
  fo4::test::ByteWriter data;
  data.i32(50);
  data.u32(1);
  data.u32(2);
  data.u32(9);
  data.u32(1);
  data.u32(6);
  data.f32(0.5F);
  data.u32(0);
  data.u32(0);
  data.bytes.resize(size);
  return data.bytes;
}

std::vector<std::byte> ench_with_flst_plugin(std::size_t enit_size) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto ench_group = fo4::test::begin_group(out, "ENCH", 0);
  const auto ench = fo4::test::begin_record(out, "ENCH", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Legacy\0", 7});
  fo4::test::write_subrecord(out, "ENIT", ench_enit(enit_size));
  fo4::test::end_record(out, ench);
  fo4::test::end_group(out, ench_group);

  const auto flst_group = fo4::test::begin_group(out, "FLST", 0);
  const auto flst = fo4::test::begin_record(out, "FLST", 0, 0x00000801, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"LegacyList\0", 11});
  fo4::test::end_record(out, flst);
  fo4::test::end_group(out, flst_group);
  return out.bytes;
}

}  // namespace

TEST(ScalarEmbedded, ReadsEveryPayloadFieldAtItsMeasuredOffset) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");

  fo4::forms::ProjectileCreation creation;
  creation.editor_id = "Probe";
  creation.speed = 12000.0F;
  creation.collision_radius = 2.5F;
  creation.tracer_frequency = 4;
  const auto key = fo4::forms::create_projectile(plugin, creation);

  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto* dnam = find_subrecord(*record, "DNAM");
  ASSERT_NE(dnam, nullptr);

  const auto speed = field("PROJ\\Speed");
  const auto radius = field("PROJ\\CollisionRadius");
  const auto tracer = field("PROJ\\TracerFrequency");

  EXPECT_EQ(fo4::schema::decode_scalar(
                fo4::schema::scalar_field_bytes(dnam->data, speed.scalar), speed.scalar),
            "12000");
  EXPECT_EQ(fo4::schema::decode_scalar(
                fo4::schema::scalar_field_bytes(dnam->data, radius.scalar), radius.scalar),
            "2.5");
  EXPECT_EQ(fo4::schema::decode_scalar(
                fo4::schema::scalar_field_bytes(dnam->data, tracer.scalar), tracer.scalar),
            "4");
}

// The whole point of offsets: a write must not disturb the rest of the struct.
TEST(ScalarEmbedded, WritingOneFieldLeavesEveryOtherByteUntouched) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");

  fo4::forms::ExplosionCreation creation;
  creation.editor_id = "Blast";
  creation.force = 1024.0F;
  creation.damage = 25.0F;
  creation.inner_radius = 300.0F;
  creation.outer_radius = 1024.0F;
  creation.sound_level = 3;
  creation.stagger_magnitude = 2;
  creation.projectile_count = 15;
  const auto key = fo4::forms::create_explosion(plugin, creation);

  const auto* before_record = fo4::find_record(plugin, key);
  ASSERT_NE(before_record, nullptr);
  const auto before = find_subrecord(*before_record, "DATA")->data;

  const auto damage = field("EXPL\\Damage");
  ASSERT_EQ(fo4::set_record_scalar(plugin, key, damage, "40"),
            fo4::ScalarMutationStatus::updated);

  const auto* after_record = fo4::find_record(plugin, key);
  ASSERT_NE(after_record, nullptr);
  const auto after = find_subrecord(*after_record, "DATA")->data;

  ASSERT_EQ(after.size(), before.size());
  for (std::size_t i = 0; i < before.size(); ++i) {
    const bool in_field = i >= damage.scalar.offset && i < damage.scalar.offset + 4;
    if (in_field) continue;
    EXPECT_EQ(after[i], before[i]) << "byte " << i << " changed but is not part of the field";
  }

  // And the neighbours still decode to what they were.
  const auto decoded = fo4::forms::decode_explosion(plugin, key);
  EXPECT_EQ(decoded.damage, 40.0F);
  EXPECT_EQ(decoded.force, 1024.0F);
  EXPECT_EQ(decoded.inner_radius, 300.0F);
  EXPECT_EQ(decoded.outer_radius, 1024.0F);
  EXPECT_EQ(decoded.sound_level, 3u);
  EXPECT_EQ(decoded.stagger_magnitude, 2u);
  EXPECT_EQ(decoded.projectile_count, 15u);
}

TEST(ScalarEmbedded, SurvivesAWriteThroughSerialization) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");

  fo4::forms::HazardCreation creation;
  creation.editor_id = "Rads";
  creation.model = "Effects\\FXFireMedSmokey01.nif";
  creation.radius = 256.0F;
  creation.taper_weight = 0.5F;
  const auto key = fo4::forms::create_hazard(plugin, creation);

  ASSERT_EQ(fo4::set_record_scalar(plugin, key, field("HAZD\\TaperCurve"), "0.75"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto decoded = fo4::forms::decode_hazard(reread, key);
  EXPECT_EQ(decoded.taper_curve, 0.75F);
  EXPECT_EQ(decoded.radius, 256.0F);
  EXPECT_EQ(decoded.taper_weight, 0.5F);
}

TEST(ScalarEmbedded, RejectsAValueOutsideTheFieldBounds) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");

  fo4::forms::ExplosionCreation creation;
  creation.editor_id = "Blast";
  const auto key = fo4::forms::create_explosion(plugin, creation);

  // Sound level is a closed engine enum: 0..4 in every shipped and corpus record.
  EXPECT_THROW(
      static_cast<void>(fo4::set_record_scalar(plugin, key, field("EXPL\\SoundLevel"), "9")),
      fo4::schema::ScalarError);
}

TEST(ScalarEmbedded, AcceptsAnyFiniteFloatUpToTheSingleLimit) {
  auto plugin = fo4::read_plugin(empty_plugin(), "Patch.esp");

  fo4::forms::ExplosionCreation explosion;
  explosion.editor_id = "Blast";
  const auto blast = fo4::forms::create_explosion(plugin, explosion);

  fo4::forms::HazardCreation hazard;
  hazard.editor_id = "Rads";
  hazard.model = "Effects\\FXFireMedSmokey01.nif";
  const auto rads = fo4::forms::create_hazard(plugin, hazard);

  fo4::forms::ProjectileCreation projectile;
  projectile.editor_id = "Bolt";
  const auto bolt = fo4::forms::create_projectile(plugin, projectile);

  ASSERT_EQ(fo4::set_record_scalar(plugin, blast, field("EXPL\\Damage"), "1e10"),
            fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(fo4::set_record_scalar(plugin, rads, field("HAZD\\Radius"), "1e10"),
            fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(fo4::set_record_scalar(plugin, bolt, field("PROJ\\Speed"), "1e10"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::forms::decode_explosion(plugin, blast).damage, 1e10F);
  EXPECT_EQ(fo4::forms::decode_hazard(plugin, rads).radius, 1e10F);
  EXPECT_EQ(fo4::forms::decode_projectile(plugin, bolt).speed, 1e10F);

  for (const char* value : {"1e39", "-1e39", "nan", "inf"}) {
    EXPECT_THROW(
        static_cast<void>(fo4::set_record_scalar(plugin, blast, field("EXPL\\Damage"), value)),
        fo4::schema::ScalarError)
        << value;
    EXPECT_THROW(
        static_cast<void>(fo4::set_record_scalar(plugin, rads, field("HAZD\\Radius"), value)),
        fo4::schema::ScalarError)
        << value;
    EXPECT_THROW(
        static_cast<void>(fo4::set_record_scalar(plugin, bolt, field("PROJ\\Speed"), value)),
        fo4::schema::ScalarError)
        << value;
  }
}

// EXPL DATA also ships at 52/56/60/64 bytes. Those are clean prefixes, so a
// field past the end is absent -- it must not be read from beyond the payload,
// and a write must not silently extend the record.
TEST(ScalarEmbedded, TreatsAFieldPastATruncatedPayloadAsAbsent) {
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
  for (int i = 0; i < 6; ++i) data.u32(0);
  data.f32(500.0F);
  data.f32(10.0F);
  data.f32(0.0F);
  data.f32(256.0F);
  data.f32(0.0F);
  data.f32(0.3F);
  data.u32(1);  // 52 bytes: ends at flags
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  auto plugin = fo4::read_plugin(out.bytes, "Legacy.esp");
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");

  // Present in the prefix.
  const auto force = field("EXPL\\Force");
  ASSERT_EQ(fo4::set_record_scalar(plugin, key, force, "600"),
            fo4::ScalarMutationStatus::updated);

  // Past the end: reported absent, and the payload keeps its length.
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, field("EXPL\\ProjectileCount"), "3"),
            fo4::ScalarMutationStatus::subrecord_missing);
  const auto* after = fo4::find_record(plugin, key);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(find_subrecord(*after, "DATA")->data.size(), 52u);

  // And preflight must not report the absent field as malformed storage.
  for (const auto& issue : fo4::schema::preflight_record(*after)) {
    EXPECT_NE(issue.code, "invalid_scalar_storage") << issue.message;
  }
}

// Looking a field up by signature alone cannot name one of many fields packed
// into a shared DNAM, and several records disagree about what DNAM means.
TEST(ScalarEmbedded, SignatureOnlyLookupSkipsEmbeddedFields) {
  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("DNAM")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("DATA")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("VNAM")).has_value());
  // XHLT and XSCL also name placed-reference variant rows, so they are ambiguous.
  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("XHLT")).has_value());
  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("XSCL")).has_value());
  // A signature only one whole-payload row names still resolves.
  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("COCT")).has_value());
  EXPECT_TRUE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("IDLC")).has_value());
}

TEST(ScalarEmbedded, ReadsWeaponDnamAtMeasuredOffsetsAndTreatsAShortPayloadAsAbsent) {
  std::vector<std::byte> dnam(132, std::byte{0});
  const auto put = [&dnam](std::size_t offset, std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      dnam[offset + i] = std::byte{static_cast<std::uint8_t>(value >> (8 * i))};
    }
  };
  put(4, std::bit_cast<std::uint32_t>(1000.0F), 4);
  put(36, 2, 4);
  put(52, 30, 2);
  put(54, 7, 1);
  put(124, 3, 4);

  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "WEAP", 0);
  const auto weap = fo4::test::begin_record(out, "WEAP", 0, 0x800);
  fo4::test::write_subrecord(out, "DNAM", dnam);
  fo4::test::end_record(out, weap);
  fo4::test::end_group(out, group);

  const auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
  const auto key = fo4::FormKey{fo4::PluginKey::from_filename("Patch.esp"), fo4::LocalFormId{0x800}};
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto* sub = find_subrecord(*record, "DNAM");
  ASSERT_NE(sub, nullptr);

  const auto read = [&](std::string_view path) {
    const auto descriptor = field(path);
    return fo4::schema::decode_scalar(
        fo4::schema::scalar_field_bytes(sub->data, descriptor.scalar), descriptor.scalar);
  };
  EXPECT_EQ(read("WEAP\\Speed"), "1000");
  EXPECT_EQ(read("WEAP\\OnHit"), "2");
  EXPECT_EQ(read("WEAP\\Capacity"), "30");
  EXPECT_EQ(read("WEAP\\AnimationType"), "7");
  EXPECT_EQ(read("WEAP\\Stagger"), "3");

  const std::vector<std::byte> short_dnam(60, std::byte{0});
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(short_dnam, field("WEAP\\Stagger").scalar).empty());
}

TEST(ScalarEmbedded, InnerRadiusIsGatedByFormVersion97) {
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  const auto inner = field("EXPL\\InnerRadius");
  EXPECT_EQ(inner.scalar.min_form_version, 97);

  auto old_plugin = fo4::read_plugin(expl_plugin_at(96), "Legacy.esp");
  const auto* old_record = fo4::find_record(old_plugin, key);
  ASSERT_NE(old_record, nullptr);
  const auto before = find_subrecord(*old_record, "DATA")->data;
  EXPECT_FALSE(exposed(old_plugin, key, inner));
  EXPECT_EQ(fo4::set_record_scalar(old_plugin, key, inner, "400"),
            fo4::ScalarMutationStatus::subrecord_missing);
  const auto* old_after = fo4::find_record(old_plugin, key);
  ASSERT_NE(old_after, nullptr);
  EXPECT_EQ(find_subrecord(*old_after, "DATA")->data, before);

  auto new_plugin = fo4::read_plugin(expl_plugin_at(97), "Legacy.esp");
  EXPECT_TRUE(exposed(new_plugin, key, inner));
  ASSERT_EQ(fo4::set_record_scalar(new_plugin, key, inner, "400"),
            fo4::ScalarMutationStatus::updated);
  const auto* new_record = fo4::find_record(new_plugin, key);
  ASSERT_NE(new_record, nullptr);
  const auto& data = find_subrecord(*new_record, "DATA")->data;
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, inner.scalar), inner.scalar),
            "400");
}

TEST(ScalarEmbedded, ExplDataFullLayoutReadsAndWritesLaterMembersFrom112) {
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  auto pre_spawn = fo4::read_plugin(expl_plugin_at(111), "Legacy.esp");
  EXPECT_TRUE(exposed(pre_spawn, key, field("EXPL\\StaggerMagnitude")));
  EXPECT_FALSE(exposed(pre_spawn, key, field("EXPL\\ProjectileCount")));
  EXPECT_EQ(fo4::set_record_scalar(pre_spawn, key, field("EXPL\\ProjectileCount"), "7"),
            fo4::ScalarMutationStatus::subrecord_missing);

  auto plugin = fo4::read_plugin(expl_plugin_at(112), "Legacy.esp");
  const auto read = [&plugin, &key](std::string_view path) {
    const auto* record = fo4::find_record(plugin, key);
    const auto descriptor = field(path);
    return fo4::schema::decode_scalar(
        fo4::schema::scalar_field_bytes(find_subrecord(*record, "DATA")->data, descriptor.scalar),
        descriptor.scalar);
  };
  EXPECT_EQ(read("EXPL\\InnerRadius"), "300");
  EXPECT_EQ(read("EXPL\\OuterRadius"), "1024");
  EXPECT_EQ(read("EXPL\\Flags"), "1");
  EXPECT_EQ(read("EXPL\\SoundLevel"), "3");
  EXPECT_EQ(read("EXPL\\StaggerMagnitude"), "2");
  EXPECT_EQ(read("EXPL\\ProjectileCount"), "0");

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, field("EXPL\\ProjectileCount"), "7"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(read("EXPL\\ProjectileCount"), "7");
  EXPECT_EQ(read("EXPL\\Flags"), "1");
  EXPECT_EQ(read("EXPL\\InnerRadius"), "300");
}

TEST(ScalarEmbedded, ExplDataOlderLayoutsHideGatedMembersWithoutMutation) {
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  const auto check = [&key](std::uint16_t version, std::size_t size) {
    auto plugin = fo4::read_plugin(expl_plugin_at(version, size), "Legacy.esp");
    const auto* record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr);
    const auto before = find_subrecord(*record, "DATA")->data;
    for (const std::string_view path : {"EXPL\\InnerRadius", "EXPL\\Flags", "EXPL\\StaggerMagnitude",
                                        "EXPL\\ProjectileCount"}) {
      const auto descriptor = field(path);
      EXPECT_FALSE(exposed(plugin, key, descriptor)) << path << " at form version " << version;
      EXPECT_EQ(fo4::set_record_scalar(plugin, key, descriptor, "9"),
                fo4::ScalarMutationStatus::subrecord_missing)
          << path << " at form version " << version;
    }
    EXPECT_TRUE(exposed(plugin, key, field("EXPL\\Force")));
    record = fo4::find_record(plugin, key);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(find_subrecord(*record, "DATA")->data, before);
  };
  check(69, 52);
  check(90, 56);
  check(96, 60);
}

TEST(ScalarEmbedded, ExplData64BytesFrom97ExposesStaggerButNotSpawnRows) {
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  auto plugin = fo4::read_plugin(expl_plugin_at(100, 64), "Legacy.esp");
  EXPECT_TRUE(exposed(plugin, key, field("EXPL\\PlacedObjectFadeDelay")));
  EXPECT_TRUE(exposed(plugin, key, field("EXPL\\StaggerMagnitude")));
  EXPECT_FALSE(exposed(plugin, key, field("EXPL\\SpawnProjectileVectorX")));
  EXPECT_FALSE(exposed(plugin, key, field("EXPL\\ProjectileCount")));

  EXPECT_EQ(fo4::set_record_scalar(plugin, key, field("EXPL\\StaggerMagnitude"), "3"),
            fo4::ScalarMutationStatus::updated);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto stagger = field("EXPL\\StaggerMagnitude");
  EXPECT_EQ(fo4::schema::decode_scalar(
                fo4::schema::scalar_field_bytes(find_subrecord(*record, "DATA")->data, stagger.scalar),
                stagger.scalar),
            "3");
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, field("EXPL\\SpawnProjectileVectorX"), "1"),
            fo4::ScalarMutationStatus::subrecord_missing);
}

TEST(ScalarEmbedded, ImageAdapterVignetteRowsAreGatedByFormVersion116) {
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  const auto radius = field("IMAD\\VignetteRadius");
  const auto strength = field("IMAD\\VignetteStrength");
  EXPECT_EQ(radius.scalar.min_form_version, 116);
  EXPECT_EQ(strength.scalar.min_form_version, 116);

  auto old_plugin =
      fo4::read_plugin(one_subrecord_plugin("IMAD", "DNAM", 113, imad_dnam(244)), "Legacy.esp");
  const auto* old_record = fo4::find_record(old_plugin, key);
  ASSERT_NE(old_record, nullptr);
  const auto before = find_subrecord(*old_record, "DNAM")->data;
  EXPECT_FALSE(exposed(old_plugin, key, radius));
  EXPECT_FALSE(exposed(old_plugin, key, strength));
  EXPECT_TRUE(exposed(old_plugin, key, field("IMAD\\MotionBlurStrength")));
  EXPECT_EQ(fo4::set_record_scalar(old_plugin, key, radius, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_record_scalar(old_plugin, key, strength, "9"),
            fo4::ScalarMutationStatus::subrecord_missing);
  const auto* old_after = fo4::find_record(old_plugin, key);
  ASSERT_NE(old_after, nullptr);
  EXPECT_EQ(find_subrecord(*old_after, "DNAM")->data, before);

  auto plugin =
      fo4::read_plugin(one_subrecord_plugin("IMAD", "DNAM", 116, imad_dnam(252)), "Legacy.esp");
  EXPECT_TRUE(exposed(plugin, key, radius));
  EXPECT_TRUE(exposed(plugin, key, strength));
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, radius, "11"), fo4::ScalarMutationStatus::updated);
  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  const auto& data = find_subrecord(*record, "DNAM")->data;
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, radius.scalar),
                                       radius.scalar),
            "11");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(data, strength.scalar),
                                       strength.scalar),
            "9");
  EXPECT_EQ(fo4::schema::decode_scalar(
                fo4::schema::scalar_field_bytes(data, field("IMAD\\MotionBlurStrength").scalar),
                field("IMAD\\MotionBlurStrength").scalar),
            "3");
}

TEST(ScalarEmbedded, EnchEnitShortFormsReadOnlyTheMembersXEditReadsCleanly) {
  const auto key = fo4::FormKey::parse("000800:Legacy.esp");
  const auto base = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("ENCH"),
                                                           "ENCH\\BaseEnchantment");
  const auto worn = fo4::schema::formlink_field_descriptor(fo4::Signature::from_string("ENCH"),
                                                           "ENCH\\WornRestrictions");
  ASSERT_TRUE(base.has_value());
  ASSERT_TRUE(worn.has_value());

  const auto list = fo4::FormKey::parse("000801:Legacy.esp");
  const auto expect_size_error = [](auto&& write) {
    try {
      static_cast<void>(write());
      ADD_FAILURE() << "expected a FormLinkFieldError";
    } catch (const fo4::schema::FormLinkFieldError& error) {
      EXPECT_NE(std::string(error.what()).find("unexpected payload size"), std::string::npos)
          << error.what();
    }
  };

  auto short_form = fo4::read_plugin(ench_with_flst_plugin(28), "Legacy.esp");
  EXPECT_TRUE(exposed(short_form, key, field("ENCH\\Cost")));
  EXPECT_TRUE(exposed(short_form, key, field("ENCH\\ChargeTime")));
  EXPECT_EQ(fo4::set_record_scalar(short_form, key, field("ENCH\\Cost"), "75"),
            fo4::ScalarMutationStatus::updated);
  expect_size_error([&] { return fo4::set_record_formlink_field(short_form, key, *base, key); });

  auto mid_form = fo4::read_plugin(ench_with_flst_plugin(32), "Legacy.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(mid_form, key, *base, key),
            fo4::FormLinkFieldMutationStatus::updated);
  expect_size_error([&] { return fo4::set_record_formlink_field(mid_form, key, *worn, list); });

  auto full_form = fo4::read_plugin(ench_with_flst_plugin(36), "Legacy.esp");
  EXPECT_EQ(fo4::set_record_formlink_field(full_form, key, *worn, list),
            fo4::FormLinkFieldMutationStatus::updated);
}
