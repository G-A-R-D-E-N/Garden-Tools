#include "water_fixture.hpp"

#include "fo4/environment/base_records.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::water;

constexpr auto k_updated = fo4::ScalarMutationStatus::updated;
constexpr auto k_missing = fo4::ScalarMutationStatus::subrecord_missing;
constexpr auto k_link_updated = fo4::FormLinkFieldMutationStatus::updated;
constexpr auto k_link_missing = fo4::FormLinkFieldMutationStatus::subrecord_missing;

fo4::ScalarMutationStatus text(fo4::Plugin& plugin, std::uint32_t id, const char* path, const char* value) {
  return fo4::set_record_string(plugin, key(id), path, 0, value);
}

bool scalar_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, const char* value) {
  return refused(plugin, [&] { scalar(plugin, id, path, value); });
}

bool link_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, const fo4::FormKey& target) {
  return refused(plugin, [&] { link(plugin, id, path, target); });
}

}

TEST(WaterAuthoring, VisualFloatsAndColorBytesWriteOnlyTheirBytes) {
  auto plugin = read(build(fixture()));
  const auto before = snapshot(plugin);
  auto want = visual(201);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\DepthAmount", "4096"), k_updated);
  put_f32(want, 0, 4096.0f);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\ShallowColorGreen", "200"), k_updated);
  want[5] = std::byte{200};
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\UnderwaterColorBlue", "7"), k_updated);
  want[38] = std::byte{7};
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\ReflectionColorRed", "0"), k_updated);
  want[96] = std::byte{0};
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\DisplacementStartingSize", "0.05"), k_updated);
  put_f32(want, 92, 0.05f);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\Layer2UVScale", "50"), k_updated);
  put_f32(want, 168, 50.0f);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\Layer3NoiseFalloff", "250"), k_updated);
  put_f32(want, 184, 250.0f);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\SiltAmount", "0.75"), k_updated);
  put_f32(want, 188, 0.75f);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\LightColorBlue", "9"), k_updated);
  want[194] = std::byte{9};
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\DarkColorRed", "11"), k_updated);
  want[196] = std::byte{11};
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\ScreenSpaceReflections", "0"), k_updated);
  want[200] = std::byte{0};
  EXPECT_EQ(payload(plugin, k_full, "DNAM"), want);

  EXPECT_TRUE(scalar_refused(plugin, k_full, "WATR\\ShallowColorRed", "256"));
  EXPECT_TRUE(scalar_refused(plugin, k_full, "WATR\\ScreenSpaceReflections", "2"));
  EXPECT_TRUE(scalar_refused(plugin, k_full, "WATR\\DepthAmount", "2000000000"));
  EXPECT_TRUE(scalar_refused(plugin, k_full, "WATR\\Flags", "256"));
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\Flags", "5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\OpacityUnused", "100"), k_updated);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\LinearVelocityZ", "-3.5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_full, "WATR\\AngularVelocityX", "0.25"), k_updated);
  EXPECT_EQ(scalar(plugin, k_bare, "WATR\\OpacityUnused", "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_bare, "WATR\\LinearVelocityX", "1"), k_missing);
  EXPECT_EQ(payload(plugin, k_full, "FNAM"), hex("05"));
  EXPECT_EQ(payload(plugin, k_full, "ANAM"), hex("64"));
  EXPECT_EQ(payload(plugin, k_full, "NAM0"), f32s({1.0f, 2.0f, -3.5f}));
  EXPECT_EQ(payload(plugin, k_full, "NAM1"), f32s({0.25f, 5.0f, 6.0f}));

  const auto reloaded = reload(plugin);
  for (const auto& [record, bytes] : snapshot(reloaded)) {
    if (record != key(k_full)) { EXPECT_EQ(before.at(record), bytes) << record.to_string(); }
  }
  EXPECT_EQ(order(reloaded, k_full), order(read(build(fixture())), k_full));
  EXPECT_EQ(payload(reloaded, k_full, "GNAM"), filled(12, 0xAB));
  EXPECT_TRUE(authoring_clean(reloaded, key(k_full)));
  with_saved_water(reloaded, k_full, [](const fo4::env::WaterData& water) {
    ASSERT_TRUE(water.visual.has_value());
    EXPECT_FLOAT_EQ(water.visual->fog_depth_amount, 4096.0f);
    EXPECT_EQ(water.visual->fog_shallow_color.green, 200);
    EXPECT_EQ(water.visual->fog_underwater_color.blue, 7);
    EXPECT_FLOAT_EQ(water.visual->noise[1].uv_scale, 50.0f);
    EXPECT_FLOAT_EQ(water.visual->silt_amount.value_or(0.0f), 0.75f);
    EXPECT_EQ(water.visual->silt_light_color->blue, 9);
    EXPECT_EQ(water.visual->silt_dark_color->red, 11);
    EXPECT_EQ(water.visual->screen_space_reflections, std::optional<bool>{false});
    EXPECT_EQ(water.flags, std::optional<std::uint8_t>{5});
    EXPECT_FLOAT_EQ((*water.linear_velocity)[2], -3.5f);
  });
}

TEST(WaterAuthoring, TailFieldsFollowThePrefixRuleOnA188ByteVisualBlock) {
  auto plugin = read(build(fixture()));
  const auto original = payload(plugin, k_short, "DNAM");
  for (const char* path : {"WATR\\SiltAmount", "WATR\\LightColorRed", "WATR\\LightColorBlue", "WATR\\DarkColorRed",
                           "WATR\\DarkColorBlue", "WATR\\ScreenSpaceReflections"}) {
    EXPECT_EQ(scalar(plugin, k_short, path, "1"), k_missing) << path;
  }
  EXPECT_EQ(payload(plugin, k_short, "DNAM"), original);
  EXPECT_EQ(scalar(plugin, k_short, "WATR\\Layer3NoiseFalloff", "123"), k_updated);
  EXPECT_EQ(scalar(plugin, k_short, "WATR\\DepthAmount", "1"), k_updated);
  auto want = original;
  put_f32(want, 184, 123.0f);
  put_f32(want, 0, 1.0f);
  EXPECT_EQ(payload(plugin, k_short, "DNAM"), want);
  EXPECT_EQ(payload(plugin, k_short, "DNAM").size(), 188u);
}

TEST(WaterAuthoring, ScreenSpaceReflectionsIsRefusedBelow201BytesAndEditableAt201) {
  for (std::size_t size = 189; size <= 200; ++size) {
    auto entries = fixture();
    for (auto& [name, data] : entries[2].subs) {
      if (name == "DNAM") { data = visual(size); }
    }
    auto plugin = read(build(entries));
    const auto original = fo4::write_plugin(plugin);
    SCOPED_TRACE(std::to_string(size));
    EXPECT_EQ(scalar(plugin, k_bare, "WATR\\ScreenSpaceReflections", "0"), k_missing);
    EXPECT_EQ(fo4::write_plugin(plugin), original);
    EXPECT_EQ(payload(plugin, k_bare, "DNAM"), visual(size));
  }
  auto plugin = read(build(fixture()));
  EXPECT_EQ(scalar(plugin, k_bare, "WATR\\ScreenSpaceReflections", "0"), k_updated);
  auto want = visual(201);
  want[200] = std::byte{0};
  EXPECT_EQ(payload(plugin, k_bare, "DNAM"), want);
}

TEST(WaterAuthoring, SiltFieldsRequireTheCompleteOptionalStruct) {
  for (std::size_t size = 188; size <= 201; ++size) {
    auto entries = fixture();
    for (auto& [name, data] : entries[2].subs) {
      if (name == "DNAM") { data = visual(size); }
    }
    auto plugin = read(build(entries));
    const auto original = fo4::write_plugin(plugin);
    for (const char* path : {"WATR\\SiltAmount", "WATR\\LightColorRed", "WATR\\LightColorGreen",
                            "WATR\\LightColorBlue", "WATR\\DarkColorRed", "WATR\\DarkColorGreen",
                            "WATR\\DarkColorBlue"}) {
      SCOPED_TRACE(std::to_string(size) + ' ' + path);
      EXPECT_EQ(scalar(plugin, k_bare, path, "1"), size < 200 ? k_missing : k_updated);
      const auto field = fo4::schema::scalar_field_descriptor(path);
      ASSERT_TRUE(field.has_value());
      EXPECT_EQ(fo4::schema::scalar_field_bytes(payload(plugin, k_bare, "DNAM"), field->scalar).empty(), size < 200);
      if (size < 200) {
        EXPECT_EQ(fo4::write_plugin(plugin), original);
        auto raw = payload(plugin, k_bare, "DNAM");
        const auto before = raw;
        EXPECT_THROW(fo4::schema::patch_scalar_field(raw, field->scalar,
                     fo4::schema::encode_scalar("1", field->scalar)), fo4::schema::ScalarError);
        EXPECT_EQ(raw, before);
      }
    }
  }
}

TEST(WaterAuthoring, StringsWriteAndTheLocalizedNameIsRefusedInALocalizedPlugin) {
  auto plugin = read(build(fixture()));
  EXPECT_EQ(text(plugin, k_full, "WATR\\Layer1NoiseTexture", "water\\new.dds"), k_updated);
  EXPECT_EQ(text(plugin, k_full, "WATR\\Layer3NoiseTexture", ""), k_updated);
  EXPECT_EQ(text(plugin, k_full, "WATR\\Name", "Pond"), k_updated);
  EXPECT_EQ(text(plugin, k_bare, "WATR\\Layer2NoiseTexture", "x.dds"), k_missing);
  EXPECT_EQ(text(plugin, k_bare, "WATR\\Name", "x"), k_missing);
  EXPECT_EQ(payload(plugin, k_full, "NAM2"), zstr("water\\new.dds"));
  EXPECT_EQ(payload(plugin, k_full, "NAM4"), zstr(""));
  EXPECT_EQ(payload(plugin, k_full, "FULL"), zstr("Pond"));
  EXPECT_TRUE(refused(plugin, [&] { scalar(plugin, k_full, "WATR\\Name", "x"); }));

  auto localized = read(build(fixture(), k_tes4_localized));
  EXPECT_TRUE(refused(localized, [&] { text(localized, k_full, "WATR\\Name", "Pond"); }));
  EXPECT_EQ(text(localized, k_full, "WATR\\Layer2NoiseTexture", "water\\l.dds"), k_updated);
  EXPECT_EQ(fo4::add_record_subrecord(localized, key(k_bare), sig("FULL")), fo4::SubrecordAddStatus::no_default);
}

TEST(WaterAuthoring, LinksCheckTargetTypesAndNullability) {
  auto plugin = read(build(fixture()));
  EXPECT_EQ(link(plugin, k_full, "WATR\\MaterialUnused", key(k_matt)), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_EQ(link(plugin, k_full, "WATR\\OpenSound", fo4::FormKey{}), k_link_updated);
  EXPECT_EQ(link(plugin, k_full, "WATR\\ConsumeSpell", key(k_spel)), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_EQ(link(plugin, k_short, "WATR\\ContactSpell", key(k_spel)), k_link_missing);
  EXPECT_EQ(link(plugin, k_short, "WATR\\OpenSound", key(k_sndr)), k_link_missing);
  EXPECT_EQ(payload(plugin, k_full, "SNAM"), words({0}));
  EXPECT_EQ(link(plugin, k_full, "WATR\\OpenSound", key(k_sndr)), k_link_updated);
  for (const char* path : {"WATR\\MaterialUnused", "WATR\\ConsumeSpell", "WATR\\ContactSpell", "WATR\\ImageSpace"}) {
    EXPECT_TRUE(link_refused(plugin, k_full, path, fo4::FormKey{})) << path;
    EXPECT_TRUE(link_refused(plugin, k_full, path, key(k_misc))) << path;
  }
  EXPECT_TRUE(link_refused(plugin, k_full, "WATR\\OpenSound", key(k_spel)));
  EXPECT_TRUE(link_refused(plugin, k_full, "WATR\\ImageSpace", key(k_spel)));
  EXPECT_TRUE(link_refused(plugin, k_full, "WATR\\ImageSpace",
                           fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}}));
  EXPECT_EQ(link(plugin, k_short, "WATR\\ImageSpace", key(k_imgs)), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_TRUE(authoring_clean(reload(plugin), key(k_full)));
}

TEST(WaterAuthoring, OptionalSubrecordsAddAndRemoveAtTheirSchemaPosition) {
  auto plugin = read(build(fixture()));
  for (const char* name : {"NAM4", "SNAM", "NAM0", "FULL", "ANAM", "NAM2", "NAM1", "NAM3"}) {
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_bare), sig(name)), fo4::SubrecordAddStatus::added) << name;
  }
  EXPECT_EQ(order(plugin, k_bare), (Order{"EDID", "FULL", "ANAM", "FNAM", "SNAM", "DNAM", "NAM0", "NAM1", "NAM2", "NAM3", "NAM4"}));
  EXPECT_EQ(payload(plugin, k_bare, "SNAM"), words({0}));
  EXPECT_EQ(payload(plugin, k_bare, "NAM0"), filled(12, 0));
  EXPECT_EQ(payload(plugin, k_bare, "ANAM"), hex("00"));
  EXPECT_EQ(payload(plugin, k_bare, "FULL"), zstr(""));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_bare), sig("SNAM")), fo4::SubrecordAddStatus::already_present);
  for (const char* name : {"TNAM", "XNAM", "YNAM", "INAM", "DATA", "GNAM"}) {
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_bare), sig(name)), fo4::SubrecordAddStatus::no_default) << name;
  }
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_bare), sig("XXXX")), fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(link(plugin, k_bare, "WATR\\OpenSound", key(k_sndr)), k_link_updated);
  EXPECT_EQ(scalar(plugin, k_bare, "WATR\\LinearVelocityY", "2"), k_updated);
  EXPECT_EQ(text(plugin, k_bare, "WATR\\Layer3NoiseTexture", "n.dds"), k_updated);
  for (const char* required : {"FNAM", "DNAM"}) {
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_bare), sig(required), 0), fo4::SubrecordRemoveStatus::below_minimum) << required;
  }
  for (const char* name : {"FULL", "ANAM", "TNAM", "SNAM", "XNAM", "YNAM", "INAM", "DATA", "GNAM", "NAM0", "NAM1", "NAM2", "NAM3", "NAM4"}) {
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_full), sig(name), 0), fo4::SubrecordRemoveStatus::removed) << name;
  }
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_full), sig("NAM4"), 0), fo4::SubrecordRemoveStatus::absent);
  EXPECT_EQ(order(plugin, k_full), (Order{"EDID", "FNAM", "DNAM"}));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_short), sig("SNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, k_short), (Order{"EDID", "ANAM", "FNAM", "SNAM", "XNAM", "INAM", "DATA", "DNAM", "GNAM", "NAM0",
                                           "NAM1", "NAM2", "NAM3", "NAM4"}));

  const auto reloaded = reload(plugin);
  for (const auto id : {k_full, k_short, k_bare}) { EXPECT_TRUE(authoring_clean(reloaded, key(id))) << id; }
  with_saved_water(reloaded, k_bare, [](const fo4::env::WaterData& water) {
    EXPECT_EQ(water.noise_textures[0], std::optional<std::string_view>{""});
    EXPECT_EQ(water.noise_textures[2], std::optional<std::string_view>{"n.dds"});
    EXPECT_FLOAT_EQ((*water.linear_velocity)[1], 2.0f);
  });
}

TEST(WaterAuthoring, UnknownSubrecordsBlockAddButKeepFieldEdits) {
  auto entries = fixture();
  entries[2].subs.push_back({"XUNK", filled(3, 9)});
  auto plugin = read(build(entries));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_bare), sig("NAM2")), fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(scalar(plugin, k_bare, "WATR\\DepthAmount", "60"), k_updated);
  EXPECT_EQ(payload(reload(plugin), k_bare, "XUNK"), filled(3, 9));
}

TEST(WaterAuthoring, CreatesFromTheXEditDefaults) {
  for (const std::uint32_t flags : {0u, k_tes4_localized}) {
    auto plugin = read(build({}, flags));
    const auto created = fo4::create_record(plugin, sig("WATR"), "NewWater");
    ASSERT_EQ(created.status, fo4::RecordCreateStatus::created) << flags;
    const auto id = created.key.local().value();
    EXPECT_EQ(order(plugin, id), (Order{"EDID", "FNAM", "DNAM"}));
    EXPECT_EQ(payload(plugin, id, "FNAM"), hex("00"));
    const auto dnam = payload(plugin, id, "DNAM");
    ASSERT_EQ(dnam.size(), 201u);
    EXPECT_TRUE(authoring_clean(plugin, created.key));
    ASSERT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("NAM2")), fo4::SubrecordAddStatus::added);
    ASSERT_EQ(fo4::set_record_string(plugin, created.key, "WATR\\Layer1NoiseTexture", 0, "water\\c.dds"), k_updated);
    EXPECT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("FULL")),
              flags == 0 ? fo4::SubrecordAddStatus::added : fo4::SubrecordAddStatus::no_default);
    with_saved_water(plugin, id, [](const fo4::env::WaterData& water) {
      ASSERT_TRUE(water.visual.has_value());
      const auto& v = *water.visual;
      EXPECT_FLOAT_EQ(v.fog_depth_amount, 8192.0f);
      EXPECT_FLOAT_EQ(v.fog_color_shallow_range, 0.6f);
      EXPECT_FLOAT_EQ(v.fog_color_deep_range, 0.3f);
      EXPECT_FLOAT_EQ(v.fog_shallow_alpha, 0.0f);
      EXPECT_FLOAT_EQ(v.fog_deep_alpha, 1.0f);
      EXPECT_FLOAT_EQ(v.fog_alpha_deep_range, 0.5f);
      EXPECT_FLOAT_EQ(v.fog_underwater_far, 1000.0f);
      EXPECT_FLOAT_EQ(v.deep_normal_falloff, 0.75f);
      EXPECT_FLOAT_EQ(v.fresnel_amount, 0.025f);
      EXPECT_FLOAT_EQ(v.displacement_falloff, 0.985f);
      EXPECT_FLOAT_EQ(v.displacement_dampener, 10.0f);
      EXPECT_FLOAT_EQ(v.displacement_starting_size, 0.05f);
      EXPECT_FLOAT_EQ(v.sun_specular_power, 50.0f);
      EXPECT_FLOAT_EQ(v.interior_specular_radius, 100000.0f);
      EXPECT_FLOAT_EQ(v.interior_specular_power, 100.0f);
      for (const auto& layer : v.noise) {
        EXPECT_FLOAT_EQ(layer.wind_speed, 0.0f);
        EXPECT_FLOAT_EQ(layer.uv_scale, 100.0f);
        EXPECT_FLOAT_EQ(layer.noise_falloff, 300.0f);
      }
      EXPECT_EQ(v.fog_shallow_color.red + v.fog_deep_color.green + v.reflection_color.blue, 0);
      EXPECT_FLOAT_EQ(v.silt_amount.value_or(-1.0f), 0.0f);
      ASSERT_TRUE(v.silt_dark_color.has_value());
      EXPECT_EQ(v.silt_dark_color->red, 0);
      EXPECT_EQ(v.screen_space_reflections, std::optional<bool>{true});
      EXPECT_EQ(water.flags, std::optional<std::uint8_t>{0});
      EXPECT_EQ(water.noise_textures[0], std::optional<std::string_view>{"water\\c.dds"});
    });
  }
}

TEST(WaterReader, SiltIsAllOrNothingLikeXEdit) {
  for (const std::size_t size : {188u, 192u, 196u, 199u, 200u, 201u}) {
    auto entries = fixture();
    for (auto& [name, data] : entries[2].subs) {
      if (name == "DNAM") { data = visual(size); }
    }
    with_saved_water(read(build(entries)), k_bare, [size](const fo4::env::WaterData& water) {
      ASSERT_TRUE(water.visual.has_value()) << size;
      const auto& v = *water.visual;
      EXPECT_FLOAT_EQ(v.noise[2].noise_falloff, 184.5f) << size;
      EXPECT_EQ(v.silt_amount.has_value(), size >= 200) << size;
      EXPECT_EQ(v.silt_light_color.has_value(), size >= 200) << size;
      EXPECT_EQ(v.silt_dark_color.has_value(), size >= 200) << size;
      EXPECT_EQ(v.screen_space_reflections.has_value(), size == 201) << size;
      if (size >= 200) {
        EXPECT_FLOAT_EQ(*v.silt_amount, 188.5f);
        EXPECT_EQ(v.silt_light_color->green, 0x20);
      }
    });
  }
  for (const std::size_t size : {187u, 202u}) {
    auto entries = fixture();
    for (auto& [name, data] : entries[2].subs) {
      if (name == "DNAM") { data = visual(size); }
    }
    EXPECT_THROW(with_saved_water(read(build(entries)), k_bare, [](const fo4::env::WaterData&) {}), fo4::env::WorldDecodeError) << size;
  }
}

TEST(WaterAuthoring, UnmodifiedWaterRecordsRoundTripByteIdentical) {
  const auto original = build({{"WATR", k_full, full_water(), 131}, {"WATR", k_short, short_water(), 122}});
  auto plugin = read(original);
  EXPECT_EQ(payload(plugin, k_full, "DNAM").size(), 201u);
  EXPECT_EQ(payload(plugin, k_short, "DNAM").size(), 188u);
  plugin.original_range.clear();
  fo4::visit_records(plugin, [](auto& record) { record.original_range.clear(); });
  const auto bytes = fo4::write_plugin(plugin);
  ASSERT_EQ(bytes.size(), original.size());
  EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
  EXPECT_EQ(order(read(bytes), k_full), order(plugin, k_full));
}
