#include "sound_writer_fixture.hpp"

#include "fo4/schema/form_links.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace fo4::test::sound;

constexpr auto k_updated = fo4::ScalarMutationStatus::updated;
constexpr auto k_missing = fo4::ScalarMutationStatus::subrecord_missing;
constexpr auto k_link_updated = fo4::FormLinkFieldMutationStatus::updated;
constexpr auto k_link_missing = fo4::FormLinkFieldMutationStatus::subrecord_missing;
constexpr auto k_list_updated = fo4::schema::FormLinkListMutationStatus::updated;

bool scalar_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  return refused(plugin, [&] { scalar(plugin, id, path, index, value); });
}

bool link_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const fo4::FormKey& target) {
  return refused(plugin, [&] { link(plugin, id, path, index, target); });
}

}

TEST(SoundAuthoring, ADuplicatedKindRefusesBothDescriptorLayouts) {
  auto entries = fixture();
  for (auto& entry : entries) {
    if (entry.id == k_std || entry.id == k_auto) {
      for (std::size_t i = 0; i < entry.subs.size(); ++i) {
        if (entry.subs[i].first == "CNAM") {
          entry.subs.insert(entry.subs.begin() + static_cast<std::ptrdiff_t>(i) + 1, entry.subs[i]);
          break;
        }
      }
    }
  }
  auto plugin = read(build(entries));
  const auto before = snapshot(plugin);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\FrequencyShift", 0, "-10"), k_missing);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\StaticAttenuation", 0, "1"), k_missing);
  EXPECT_EQ(link(plugin, k_auto, "SNDR\\BaseDescriptor", 0, key(k_compound)), k_link_missing);
  EXPECT_EQ(snapshot(plugin), before);
}

TEST(SoundAuthoring, DescriptorValuesFollowTheKindDiscriminatorIncludingAMissingKind) {
  auto plugin = read(build(fixture()));
  const auto before = snapshot(plugin);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\FrequencyShift", 0, "-10"), k_updated);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\FrequencyVariance", 0, "-128"), k_updated);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\Priority", 0, "200"), k_updated);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\DbVariance", 0, "9"), k_updated);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\StaticAttenuation", 0, "65535"), k_updated);
  EXPECT_EQ(payloads(plugin, k_std, "BNAM").at(0), raw({0xF6, 0x80, 200, 9, 0xFF, 0xFF}));
  EXPECT_TRUE(scalar_refused(plugin, k_std, "SNDR\\FrequencyShift", 0, "128"));
  EXPECT_TRUE(scalar_refused(plugin, k_std, "SNDR\\FrequencyShift", 0, "-129"));
  EXPECT_TRUE(scalar_refused(plugin, k_std, "SNDR\\Priority", 0, "-1"));
  EXPECT_TRUE(scalar_refused(plugin, k_std, "SNDR\\StaticAttenuation", 0, "65536"));
  EXPECT_EQ(scalar(plugin, k_nokind, "SNDR\\FrequencyShift", 0, "-1"), k_updated);
  EXPECT_EQ(payloads(plugin, k_nokind, "BNAM").at(0), raw({0xFF, 0, 0, 0, 0, 0}));
  EXPECT_EQ(scalar(plugin, k_auto, "SNDR\\FrequencyShift", 0, "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_bare, "SNDR\\Priority", 0, "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_compound, "SNDR\\Priority", 0, "1"), k_missing);
  EXPECT_EQ(link(plugin, k_std, "SNDR\\BaseDescriptor", 0, key(k_auto)), k_link_missing);
  EXPECT_EQ(link(plugin, k_nokind, "SNDR\\BaseDescriptor", 0, key(k_auto)), k_link_missing);
  EXPECT_EQ(link(plugin, k_auto, "SNDR\\BaseDescriptor", 0, key(k_compound)), k_link_updated);
  EXPECT_EQ(payloads(plugin, k_auto, "BNAM").at(0), words({k_compound}));
  EXPECT_TRUE(link_refused(plugin, k_auto, "SNDR\\BaseDescriptor", 0, key(k_misc)));
  EXPECT_TRUE(link_refused(plugin, k_auto, "SNDR\\BaseDescriptor", 0, fo4::FormKey{}));

  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\ValuesUnknown", 0, "4"), k_updated);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\Looping", 0, "16"), k_updated);
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\RumbleSendValue", 0, "255"), k_updated);
  EXPECT_EQ(payloads(plugin, k_std, "LNAM").at(0), raw({4, 16, 2, 255}));
  EXPECT_EQ(scalar(plugin, k_bare, "SNDR\\Sidechain", 0, "1"), k_missing);

  const auto reloaded = reload(plugin);
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_std) && record != key(k_nokind) && record != key(k_auto)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  EXPECT_EQ(order(reloaded, k_std), order(read(build(fixture())), k_std));
  const SavedEnvironment saved(reloaded);
  const auto standard = fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_std));
  ASSERT_TRUE(standard.playback);
  EXPECT_EQ(standard.playback->frequency_shift, -10);
  EXPECT_EQ(standard.playback->frequency_variance, -128);
  EXPECT_EQ(standard.playback->priority, 200);
  EXPECT_EQ(standard.playback->attenuation_hundredths, 65535);
  const auto nokind = fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_nokind));
  EXPECT_FALSE(nokind.kind.has_value());
  EXPECT_EQ(nokind.playback->frequency_shift, -1);
  EXPECT_EQ(fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_auto)).base_descriptor, key(k_compound));
}

TEST(SoundAuthoring, DescriptorLinksStringsAndRateGroupsEditInPlace) {
  auto plugin = read(build(fixture()));
  EXPECT_EQ(link(plugin, k_std, "SNDR\\Category", 0, key(k_snct)), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_TRUE(link_refused(plugin, k_std, "SNDR\\Category", 0, key(k_misc)));
  EXPECT_TRUE(link_refused(plugin, k_std, "SNDR\\Category", 0, fo4::FormKey{}));
  EXPECT_EQ(link(plugin, k_std, "SNDR\\AlternateSoundFor", 0, key(k_auto)), k_link_updated);
  EXPECT_TRUE(link_refused(plugin, k_std, "SNDR\\AlternateSoundFor", 0, key(k_snct)));
  EXPECT_TRUE(link_refused(plugin, k_std, "SNDR\\OutputModel", 0, key(k_snct)));
  EXPECT_TRUE(link_refused(plugin, k_std, "SNDR\\OutputModel", 0, fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}}));
  EXPECT_EQ(link(plugin, k_bare, "SNDR\\OutputModel", 0, key(k_sopm)), k_link_missing);
  EXPECT_EQ(count(plugin, k_compound, "SNDR\\Descriptors"), 2u);
  EXPECT_EQ(link(plugin, k_compound, "SNDR\\Descriptors", 1, key(k_auto)), k_link_updated);
  EXPECT_EQ(link(plugin, k_compound, "SNDR\\Descriptors", 2, key(k_auto)), k_link_missing);
  EXPECT_TRUE(link_refused(plugin, k_compound, "SNDR\\Descriptors", 0, key(k_must)));
  EXPECT_EQ(payloads(plugin, k_compound, "DNAM"), (std::vector<Bytes>{words({k_std}), words({k_auto})}));

  EXPECT_EQ(count(plugin, k_std, "SNDR\\Sounds"), 2u);
  EXPECT_EQ(text(plugin, k_std, "SNDR\\Sounds", 1, "sound\\c.xwm"), k_updated);
  EXPECT_EQ(text(plugin, k_std, "SNDR\\Sounds", 2, "x.wav"), k_missing);
  EXPECT_EQ(text(plugin, k_std, "SNDR\\Notes", 0, "n"), k_missing);
  EXPECT_EQ(count(plugin, k_auto, "SNDR\\RateOfFireRpm"), 2u);
  EXPECT_EQ(scalar(plugin, k_auto, "SNDR\\RateOfFireRpm", 1, "1200"), k_updated);
  EXPECT_EQ(text(plugin, k_auto, "SNDR\\RateOfFireFile", 0, "sound\\fast.xwm"), k_updated);
  EXPECT_EQ(scalar(plugin, k_auto, "SNDR\\RateOfFireRpm", 2, "1"), k_missing);
  EXPECT_EQ(count(plugin, k_std, "SNDR\\RateOfFireFile"), 0u);
  EXPECT_EQ(order(plugin, k_auto), order(read(build(fixture())), k_auto));

  const SavedEnvironment saved(reload(plugin));
  const auto standard = fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_std));
  EXPECT_EQ(standard.alternate, key(k_auto));
  ASSERT_EQ(standard.files.size(), 2u);
  EXPECT_EQ(standard.files[1], "sound\\c.xwm");
  const auto weapon = fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_auto));
  ASSERT_EQ(weapon.rate_files.size(), 2u);
  EXPECT_EQ(weapon.rate_files[0].file, "sound\\fast.xwm");
  EXPECT_EQ(weapon.rate_files[1].rpm, 1200u);
  EXPECT_EQ(fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_compound)).descriptors.at(1), key(k_auto));
}

TEST(SoundAuthoring, MalformedRateGroupsRefuseCountsAndEditsWithoutChangingBytes) {
  for (int defect = 0; defect < 8; ++defect) {
    SCOPED_TRACE(defect);
    auto entries = fixture();
    auto& subs = entries[2].subs;
    switch (defect) {
      case 0: subs.insert(subs.begin() + 4, {"FNAM", zstr("stray.wav")}); break;
      case 1: subs[3].second = words({3}); break;
      case 2: subs.erase(subs.begin() + 7); break;
      case 3: subs[4].second = raw({1}); break;
      case 4: subs[5].second = raw({1, 2, 3}); break;
      case 5: subs[6].second = raw({1, 2, 3}); break;
      case 6: subs.insert(subs.begin() + 3, subs[3]); break;
      case 7: subs.insert(subs.begin() + 6, subs[5]); break;
    }
    auto plugin = read(build(entries));
    const auto before = fo4::write_plugin(plugin);
    const SavedEnvironment saved(plugin);
    EXPECT_THROW(fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_auto)), fo4::env::WorldDecodeError);
    EXPECT_THROW(count(plugin, k_auto, "SNDR\\RateOfFireRpm"), std::exception);
    EXPECT_THROW(count(plugin, k_auto, "SNDR\\RateOfFireFile"), std::exception);
    EXPECT_TRUE(scalar_refused(plugin, k_auto, "SNDR\\RateOfFireRpm", 0, "1200"));
    EXPECT_TRUE(refused(plugin, [&] { text(plugin, k_auto, "SNDR\\RateOfFireFile", 0, "new.wav"); }));
    EXPECT_EQ(fo4::write_plugin(plugin), before);
  }
}

TEST(SoundAuthoring, UnknownFixedLinkWidthsRefuseWithoutChangingBytes) {
  const std::tuple<std::uint32_t, const char*, const char*, std::uint32_t> cases[] = {
      {k_std, "GNAM", "SNDR\\Category", k_snct},
      {k_std, "SNAM", "SNDR\\AlternateSoundFor", k_compound},
      {k_std, "ONAM", "SNDR\\OutputModel", k_sopm},
      {k_auto, "BNAM", "SNDR\\BaseDescriptor", k_compound},
      {k_compound, "DNAM", "SNDR\\Descriptors", k_std},
      {k_marker, "SDSC", "SOUN\\SoundDescriptor", k_std},
      {k_step, "DATA", "FSTP\\ImpactDataSet", k_ipds},
  };
  for (const auto& [id, tag, path, target] : cases) {
    for (const auto width : {6u, 8u}) {
      SCOPED_TRACE(std::string(path) + " " + std::to_string(width));
      auto entries = fixture();
      for (auto& entry : entries) {
        if (entry.id != id) { continue; }
        for (auto& [name, data] : entry.subs) {
          if (name == tag) { data = filled(width, 0); }
        }
      }
      auto plugin = read(build(entries));
      EXPECT_THROW(count(plugin, id, path), fo4::schema::FormLinkFieldError);
      EXPECT_TRUE(link_refused(plugin, id, path, 0, key(target)));
    }
  }
}

TEST(SoundAuthoring, UnknownRepeatWidthsRefuseWithoutChangingBytes) {
  for (const auto width : {4u, 5u, 6u, 7u, 10u, 12u}) {
    SCOPED_TRACE(width);
    auto entries = fixture();
    entries[5].subs[3].second = filled(width, 0);
    auto plugin = read(build(entries));
    const SavedEnvironment saved(plugin);
    EXPECT_THROW(fo4::env::decode_sound_marker(saved.env(), saved.record(k_marker)), fo4::env::WorldDecodeError);
    EXPECT_THROW(count(plugin, k_marker, "SOUN\\RepeatMinTime"), std::exception);
    EXPECT_TRUE(scalar_refused(plugin, k_marker, "SOUN\\RepeatMinTime", 0, "1"));
    EXPECT_TRUE(scalar_refused(plugin, k_marker, "SOUN\\RepeatMaxTime", 0, "1"));
    EXPECT_TRUE(scalar_refused(plugin, k_marker, "SOUN\\RepeatStackable", 0, "1"));
  }
}

TEST(SoundAuthoring, MalformedFootstepCountsRefuseWithoutChangingBytes) {
  for (int defect = 0; defect < 6; ++defect) {
    SCOPED_TRACE(defect);
    auto entries = fixture();
    auto& subs = entries[9].subs;
    switch (defect) {
      case 0: subs[1].second = words({1, 2, 0, 1, 2}); break;
      case 1: subs.erase(subs.begin() + 1); break;
      case 2: subs[1].second.resize(16); break;
      case 3: subs.insert(subs.begin() + 1, subs[1]); break;
      case 4: subs.insert(subs.begin() + 2, subs[2]); break;
      case 5: subs[1].second = words({0xFFFFFFFFu, 0, 0, 0, 0}); break;
    }
    auto plugin = read(build(entries));
    const SavedEnvironment saved(plugin);
    EXPECT_THROW(fo4::env::decode_footstep_set(saved.env(), saved.record(k_set)), fo4::env::WorldDecodeError);
    EXPECT_THROW(count(plugin, k_set, "FSTS\\Footsteps"), std::exception);
    EXPECT_TRUE(link_refused(plugin, k_set, "FSTS\\Footsteps", 0, key(k_step2)));
  }
}

TEST(SoundAuthoring, MarkerMusicAndFootstepFieldsWriteOnlyTheirBytes) {
  auto plugin = read(build(fixture()));
  const auto before = snapshot(plugin);
  EXPECT_EQ(scalar(plugin, k_marker, "SOUN\\RepeatMinTime", 0, "1.5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_marker, "SOUN\\RepeatStackable", 0, "0"), k_updated);
  EXPECT_TRUE(scalar_refused(plugin, k_marker, "SOUN\\RepeatStackable", 0, "2"));
  EXPECT_EQ(scalar(plugin, k_marker_old, "SOUN\\RepeatStackable", 0, "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_marker_old, "SOUN\\RepeatMaxTime", 0, "9"), k_updated);
  EXPECT_EQ(link(plugin, k_marker, "SOUN\\SoundDescriptor", 0, key(k_auto)), k_link_updated);
  EXPECT_TRUE(link_refused(plugin, k_marker, "SOUN\\SoundDescriptor", 0, fo4::FormKey{}));
  EXPECT_TRUE(link_refused(plugin, k_marker, "SOUN\\SoundDescriptor", 0, key(k_must)));
  EXPECT_EQ(payloads(plugin, k_marker, "REPT").at(0), raw({0, 0, 0xC0, 0x3F, 0, 0, 0x80, 0x40, 0}));
  EXPECT_EQ(payloads(plugin, k_marker_old, "REPT").at(0), f32s({1.0f, 9.0f}));

  EXPECT_EQ(scalar(plugin, k_music, "MUSC\\Flags", 0, "4294967295"), k_updated);
  EXPECT_EQ(scalar(plugin, k_music, "MUSC\\Priority", 0, "60"), k_updated);
  EXPECT_EQ(scalar(plugin, k_music, "MUSC\\Ducking", 0, "500"), k_updated);
  EXPECT_EQ(scalar(plugin, k_music, "MUSC\\FadeDuration", 0, "0.5"), k_updated);
  EXPECT_TRUE(scalar_refused(plugin, k_music, "MUSC\\Priority", 0, "65536"));
  EXPECT_EQ(payloads(plugin, k_music, "PNAM").at(0), raw({60, 0, 0xF4, 0x01}));
  EXPECT_EQ(count(plugin, k_music, "MUSC\\Tracks"), 2u);
  EXPECT_EQ(list_link(plugin, k_music, "MUSC\\Tracks", 1, key(k_must)), k_list_updated);
  EXPECT_EQ(list_link(plugin, k_music, "MUSC\\Tracks", 0, fo4::FormKey{}), k_list_updated);
  EXPECT_EQ(list_link(plugin, k_music, "MUSC\\Tracks", 2, key(k_must)), fo4::schema::FormLinkListMutationStatus::index_out_of_range);
  EXPECT_EQ(list_link(plugin, k_music, "MUSC\\Tracks", 0, key(k_misc)), fo4::schema::FormLinkListMutationStatus::target_type_mismatch);
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), words({0, k_must}));

  EXPECT_EQ(link(plugin, k_step, "FSTP\\ImpactDataSet", 0, fo4::FormKey{}), k_link_updated);
  EXPECT_TRUE(link_refused(plugin, k_step, "FSTP\\ImpactDataSet", 0, key(k_misc)));
  EXPECT_EQ(link(plugin, k_step2, "FSTP\\ImpactDataSet", 0, key(k_ipds)), k_link_updated);
  EXPECT_EQ(text(plugin, k_step, "FSTP\\Tag", 0, "FootRight"), k_updated);

  EXPECT_EQ(count(plugin, k_set, "FSTS\\Footsteps"), 5u);
  EXPECT_EQ(link(plugin, k_set, "FSTS\\Footsteps", 4, key(k_step2)), k_link_updated);
  EXPECT_EQ(link(plugin, k_set, "FSTS\\Footsteps", 5, key(k_step2)), k_link_missing);
  EXPECT_TRUE(link_refused(plugin, k_set, "FSTS\\Footsteps", 0, fo4::FormKey{}));
  EXPECT_TRUE(link_refused(plugin, k_set, "FSTS\\Footsteps", 0, key(k_ipds)));
  EXPECT_EQ(payloads(plugin, k_set, "DATA").at(0), words({k_step, k_step, k_step, k_step2, k_step2}));
  EXPECT_EQ(payloads(plugin, k_set, "XCNT").at(0), words({1, 2, 0, 1, 1}));

  const auto reloaded = reload(plugin);
  const auto after = snapshot(reloaded);
  const std::set<fo4::FormKey> touched{key(k_marker), key(k_marker_old), key(k_music), key(k_step), key(k_step2), key(k_set)};
  for (const auto& [record, bytes] : before) {
    if (touched.count(record) == 0) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  const SavedEnvironment saved(reloaded);
  const auto marker = fo4::env::decode_sound_marker(saved.env(), saved.record(k_marker));
  EXPECT_EQ(marker.descriptor, key(k_auto));
  EXPECT_FLOAT_EQ(marker.repeat->min_time, 1.5f);
  EXPECT_EQ(marker.repeat->stackable, std::optional<std::uint8_t>{0});
  const auto music = fo4::env::decode_music_type(saved.env(), saved.record(k_music));
  EXPECT_EQ(music.flags, 0xFFFFFFFFu);
  EXPECT_EQ(music.priority, 60);
  EXPECT_EQ(music.ducking_hundredths, 500);
  ASSERT_EQ(music.tracks.size(), 2u);
  EXPECT_TRUE(music.tracks[0].owner().empty());
  EXPECT_EQ(music.tracks[1], key(k_must));
  const auto step = fo4::env::decode_footstep(saved.env(), saved.record(k_step));
  ASSERT_TRUE(step.impact_set.has_value());
  EXPECT_TRUE(step.impact_set->owner().empty());
  EXPECT_EQ(step.tag, std::optional<std::string_view>{"FootRight"});
  const auto set = fo4::env::decode_footstep_set(saved.env(), saved.record(k_set));
  EXPECT_EQ(set.categories[4].at(0), key(k_step));
  EXPECT_EQ(set.categories[0].at(0), key(k_step2));
}
