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

bool scalar_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  return refused(plugin, [&] { scalar(plugin, id, path, index, value); });
}

bool link_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const fo4::FormKey& target) {
  return refused(plugin, [&] { link(plugin, id, path, index, target); });
}

}  // namespace

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
  EXPECT_EQ(link(plugin, k_music, "MUSC\\Tracks", 1, key(k_must)), k_link_updated);
  EXPECT_EQ(link(plugin, k_music, "MUSC\\Tracks", 0, fo4::FormKey{}), k_link_updated);
  EXPECT_EQ(link(plugin, k_music, "MUSC\\Tracks", 2, key(k_must)), k_link_missing);
  EXPECT_TRUE(link_refused(plugin, k_music, "MUSC\\Tracks", 0, key(k_misc)));
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
