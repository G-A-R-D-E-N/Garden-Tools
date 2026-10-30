#include "sound_writer_fixture.hpp"

#include "fo4/schema/form_links.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace fo4::test::sound;

using Add = fo4::SubrecordAddStatus;
using Remove = fo4::SubrecordRemoveStatus;

Add add(fo4::Plugin& plugin, std::uint32_t id, const char* name) { return fo4::add_record_subrecord(plugin, key(id), sig(name)); }
Remove remove(fo4::Plugin& plugin, std::uint32_t id, const char* name, std::size_t index = 0) {
  return fo4::remove_record_subrecord(plugin, key(id), sig(name), index);
}

}

TEST(SoundSubrecords, DescriptorOptionalSubrecordsAddAndRemoveAtTheirSchemaPosition) {
  auto plugin = read(build(fixture()));
  EXPECT_EQ(add(plugin, k_bare, "LNAM"), Add::added);
  EXPECT_EQ(add(plugin, k_bare, "ANAM"), Add::added);
  EXPECT_EQ(add(plugin, k_bare, "NNAM"), Add::added);
  EXPECT_EQ(add(plugin, k_bare, "ANAM"), Add::added);
  EXPECT_EQ(add(plugin, k_bare, "NNAM"), Add::already_present);
  EXPECT_EQ(order(plugin, k_bare), (Order{"EDID", "NNAM", "ANAM", "ANAM", "LNAM"}));
  EXPECT_EQ(payloads(plugin, k_bare, "LNAM").at(0), filled(4, 0));
  EXPECT_EQ(payloads(plugin, k_bare, "ANAM").at(1), zstr(""));
  EXPECT_EQ(text(plugin, k_bare, "SNDR\\Notes", 0, "note"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(text(plugin, k_bare, "SNDR\\Sounds", 1, "b.wav"), fo4::ScalarMutationStatus::updated);
  for (const char* coupled : {"CNAM", "BNAM", "ITMC", "CTDA"}) {
    EXPECT_EQ(add(plugin, k_bare, coupled), Add::not_authorable) << coupled;
    EXPECT_EQ(remove(plugin, k_std, coupled), Remove::not_authorable) << coupled;
  }
  for (const char* unproven : {"GNAM", "SNAM", "ONAM", "DNAM"}) {
    EXPECT_EQ(add(plugin, k_bare, unproven), Add::no_default) << unproven;
  }
  EXPECT_EQ(add(plugin, k_bare, "ITMS"), Add::not_authorable);
  EXPECT_EQ(add(plugin, k_auto, "NNAM"), Add::unknown_order);
  EXPECT_EQ(remove(plugin, k_auto, "ITMC"), Remove::not_authorable);
  EXPECT_EQ(remove(plugin, k_auto, "INTV"), Remove::not_authorable);
  EXPECT_EQ(remove(plugin, k_bare, "ANAM", 0), Remove::removed);
  EXPECT_EQ(remove(plugin, k_bare, "ANAM", 5), Remove::absent);
  EXPECT_EQ(remove(plugin, k_std, "GNAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_std, "ONAM"), Remove::removed);
  EXPECT_EQ(remove(plugin, k_compound, "DNAM", 0), Remove::removed);
  EXPECT_EQ(add(plugin, k_std, "NNAM"), Add::added);
  EXPECT_EQ(order(plugin, k_std), (Order{"EDID", "NNAM", "CNAM", "SNAM", "ANAM", "ANAM", "CTDA", "LNAM", "BNAM"}));

  const auto reloaded = reload(plugin);
  for (const auto id : {k_std, k_compound, k_nokind, k_bare, k_auto}) { EXPECT_TRUE(authoring_clean(reloaded, id)) << id; }
  const SavedEnvironment saved(reloaded);
  const auto bare = fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_bare));
  ASSERT_EQ(bare.files.size(), 1u);
  EXPECT_EQ(bare.files[0], "b.wav");
  ASSERT_TRUE(bare.controls.has_value());
  const auto compound = fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_compound));
  ASSERT_EQ(compound.descriptors.size(), 1u);
  EXPECT_EQ(compound.descriptors[0], key(k_nokind));
  EXPECT_FALSE(fo4::env::decode_sound_descriptor(saved.env(), saved.record(k_std)).category.has_value());
}

TEST(SoundSubrecords, MarkerMusicAndFootstepAuthoringHonoursRequiredAndCoupledEntries) {
  auto plugin = read(build(fixture()));
  EXPECT_EQ(remove(plugin, k_marker, "REPT"), Remove::removed);
  EXPECT_EQ(add(plugin, k_marker, "REPT"), Add::added);
  EXPECT_EQ(payloads(plugin, k_marker, "REPT").at(0), filled(9, 0));
  EXPECT_EQ(remove(plugin, k_marker, "SDSC"), Remove::below_minimum);
  EXPECT_EQ(remove(plugin, k_marker, "OBND"), Remove::below_minimum);
  EXPECT_EQ(remove(plugin, k_music, "TNAM"), Remove::removed);
  EXPECT_EQ(add(plugin, k_music, "TNAM"), Add::added);
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), filled(4, 0));
  EXPECT_EQ(list_link(plugin, k_music, "MUSC\\Tracks", 0, key(k_must)), fo4::schema::FormLinkListMutationStatus::updated);
  for (const char* required : {"FNAM", "PNAM", "WNAM"}) {
    EXPECT_EQ(remove(plugin, k_music, required), Remove::below_minimum) << required;
  }
  EXPECT_EQ(remove(plugin, k_step, "DATA"), Remove::below_minimum);
  EXPECT_EQ(remove(plugin, k_step, "ANAM"), Remove::below_minimum);
  EXPECT_EQ(remove(plugin, k_set, "XCNT"), Remove::not_authorable);
  EXPECT_EQ(remove(plugin, k_set, "DATA"), Remove::not_authorable);
  EXPECT_EQ(add(plugin, k_set, "DATA"), Add::not_authorable);

  const auto reloaded = reload(plugin);
  for (const auto id : {k_marker, k_marker_old, k_music, k_step, k_step2, k_set}) { EXPECT_TRUE(authoring_clean(reloaded, id)) << id; }
  const SavedEnvironment saved(reloaded);
  const auto marker = fo4::env::decode_sound_marker(saved.env(), saved.record(k_marker));
  ASSERT_TRUE(marker.repeat.has_value());
  EXPECT_EQ(marker.repeat->stackable, std::optional<std::uint8_t>{0});
  EXPECT_EQ(fo4::env::decode_music_type(saved.env(), saved.record(k_music)).tracks.at(0), key(k_must));
}

TEST(SoundSubrecords, UnknownSubrecordsBlockAddButKeepFieldEdits) {
  auto entries = fixture();
  entries[4].subs.push_back({"XUNK", filled(3, 9)});
  auto plugin = read(build(entries));
  EXPECT_EQ(add(plugin, k_bare, "NNAM"), Add::unknown_order);
  EXPECT_EQ(payloads(reload(plugin), k_bare, "XUNK").at(0), filled(3, 9));
  EXPECT_EQ(scalar(plugin, k_std, "SNDR\\Priority", 0, "1"), fo4::ScalarMutationStatus::updated);
}

TEST(SoundSubrecords, CreateUsesProvenDefaultsAndRefusesUnprovableRecords) {
  auto plugin = read(build({}));
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::create_record(plugin, sig("SOUN"), "NewMarker").status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::create_record(plugin, sig("FSTS"), "NewSet").status, fo4::RecordCreateStatus::not_authorable);
  EXPECT_EQ(fo4::write_plugin(plugin), before);

  const auto descriptor = fo4::create_record(plugin, sig("SNDR"), "NewDescriptor");
  ASSERT_EQ(descriptor.status, fo4::RecordCreateStatus::created);
  const auto music = fo4::create_record(plugin, sig("MUSC"), "NewMusic");
  ASSERT_EQ(music.status, fo4::RecordCreateStatus::created);
  const auto step = fo4::create_record(plugin, sig("FSTP"), "NewStep");
  ASSERT_EQ(step.status, fo4::RecordCreateStatus::created);
  const auto d = descriptor.key.local().value(), m = music.key.local().value(), s = step.key.local().value();
  EXPECT_EQ(order(plugin, d), (Order{"EDID"}));
  EXPECT_EQ(order(plugin, m), (Order{"EDID", "FNAM", "PNAM", "WNAM"}));
  EXPECT_EQ(payloads(plugin, m, "PNAM").at(0), raw({50, 0, 0, 0}));
  EXPECT_EQ(order(plugin, s), (Order{"EDID", "DATA", "ANAM"}));
  EXPECT_EQ(payloads(plugin, s, "DATA").at(0), filled(4, 0));
  EXPECT_EQ(payloads(plugin, s, "ANAM").at(0), zstr(""));
  for (const auto id : {d, m, s}) { EXPECT_TRUE(authoring_clean(plugin, id)) << id; }
  ASSERT_EQ(add(plugin, d, "LNAM"), Add::added);
  ASSERT_EQ(add(plugin, d, "ANAM"), Add::added);
  EXPECT_EQ(text(plugin, d, "SNDR\\Sounds", 0, "sound\\new.wav"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(scalar(plugin, d, "SNDR\\Looping", 0, "8"), fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(add(plugin, m, "TNAM"), Add::added);
  EXPECT_EQ(scalar(plugin, m, "MUSC\\FadeDuration", 0, "2"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(text(plugin, s, "FSTP\\Tag", 0, "Left"), fo4::ScalarMutationStatus::updated);

  const SavedEnvironment saved(reload(plugin));
  const auto created = fo4::env::decode_sound_descriptor(saved.env(), saved.record(d));
  EXPECT_FALSE(created.kind.has_value());
  EXPECT_EQ(created.files.at(0), "sound\\new.wav");
  EXPECT_EQ((*created.controls)[1], 8);
  const auto created_music = fo4::env::decode_music_type(saved.env(), saved.record(m));
  EXPECT_EQ(created_music.priority, 50);
  EXPECT_FLOAT_EQ(created_music.fade_duration.value_or(0.0f), 2.0f);
  ASSERT_EQ(created_music.tracks.size(), 1u);
  EXPECT_EQ(fo4::env::decode_footstep(saved.env(), saved.record(s)).tag, std::optional<std::string_view>{"Left"});
}

TEST(SoundSubrecords, FormLinkScanVisitsExactlyTheLinkSlots) {
  auto plugin = read(build(fixture()));
  std::set<std::tuple<std::uint32_t, std::string, std::size_t, std::size_t>> hits;
  for (const auto& hit : fo4::collect_form_links(plugin)) {
    const auto id = hit.record.local().value();
    if (id < k_std || id > k_set) { continue; }
    hits.insert({id, hit.subrecord.to_string(), hit.subrecord_ordinal, hit.offset});
  }
  const std::set<std::tuple<std::uint32_t, std::string, std::size_t, std::size_t>> expected{
      {k_std, "GNAM", 0, 0}, {k_std, "SNAM", 0, 0}, {k_std, "ONAM", 0, 0},
      {k_compound, "DNAM", 0, 0}, {k_compound, "DNAM", 1, 0}, {k_auto, "BNAM", 0, 0},
      {k_marker, "SDSC", 0, 0}, {k_marker_old, "SDSC", 0, 0},
      {k_music, "TNAM", 0, 0}, {k_music, "TNAM", 0, 4}, {k_step, "DATA", 0, 0},
      {k_set, "DATA", 0, 0}, {k_set, "DATA", 0, 4}, {k_set, "DATA", 0, 8}, {k_set, "DATA", 0, 12}, {k_set, "DATA", 0, 16}};
  for (const auto& want : expected) { EXPECT_TRUE(hits.count(want)) << std::get<0>(want) << ' ' << std::get<1>(want) << ' ' << std::get<3>(want); }
  for (const auto& got : hits) { EXPECT_TRUE(expected.count(got)) << std::get<0>(got) << ' ' << std::get<1>(got) << ' ' << std::get<3>(got); }
}
