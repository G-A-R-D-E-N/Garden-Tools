#include "sound_writer_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_env.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>

namespace {

using namespace fo4::test::sound;

using ListStatus = fo4::schema::FormLinkListMutationStatus;
using ListDescriptor = fo4::schema::FormLinkListDescriptor;

ListDescriptor track_list(const char* record, const char* path) {
  return *fo4::schema::formlink_list_descriptor(sig(record), path);
}

}  // namespace

TEST(MusicTrackLists, TracksArePackedAppendableNullableLists) {
  const auto musc = track_list("MUSC", "MUSC\\Tracks");
  const auto must = track_list("MUST", "MUST\\Tracks");
  EXPECT_EQ(musc.subrecord, sig("TNAM"));
  EXPECT_EQ(must.subrecord, sig("SNAM"));
  for (const auto& list : {musc, must}) {
    EXPECT_EQ(list.storage, fo4::schema::FormLinkListStorage::packed_array);
    EXPECT_TRUE(list.can_insert);
    EXPECT_TRUE(list.nullable);
  }
  EXPECT_FALSE(fo4::schema::formlink_field_descriptor("MUSC\\Tracks").has_value());
  EXPECT_FALSE(fo4::schema::formlink_field_descriptor("MUST\\Tracks").has_value());
}

TEST(MusicTrackLists, InsertIntoExistingTracksAppendsAndNullsInPlace) {
  auto plugin = read(build(fixture()));
  const auto list = track_list("MUSC", "MUSC\\Tracks");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_music), list, 2, key(k_must)),
            ListStatus::updated);
  EXPECT_EQ(count(plugin, k_music, "MUSC\\Tracks"), 3u);
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), words({k_must, 0, k_must}));
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_null_element(plugin, key(k_music), list, 0),
            ListStatus::updated);
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), words({0, k_must, 0, k_must}));
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_music), list, 5, key(k_must)),
            ListStatus::index_out_of_range);
  const auto reloaded = reload(plugin);
  EXPECT_EQ(count(reloaded, k_music, "MUSC\\Tracks"), 4u);
  EXPECT_EQ(payloads(reloaded, k_music, "TNAM").at(0), words({0, k_must, 0, k_must}));
}

TEST(MusicTrackLists, AppendWhenTracksAbsentAnchorsAfterWnam) {
  auto plugin = read(build(fixture()));
  const auto list = track_list("MUSC", "MUSC\\Tracks");
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_music), sig("TNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(count(plugin, k_music, "MUSC\\Tracks"), 0u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_music), list, 0, key(k_must)),
            ListStatus::updated);
  EXPECT_EQ(order(plugin, k_music), (Order{"EDID", "FNAM", "PNAM", "WNAM", "TNAM"}));
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), words({k_must}));
  const auto reloaded = reload(plugin);
  EXPECT_EQ(order(reloaded, k_music), (Order{"EDID", "FNAM", "PNAM", "WNAM", "TNAM"}));
}

TEST(MusicTrackLists, RemoveEntriesAndEmptyTracksDropsSubrecord) {
  auto plugin = read(build(fixture()));
  const auto list = track_list("MUSC", "MUSC\\Tracks");
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key(k_music), list, 0), ListStatus::updated);
  EXPECT_EQ(count(plugin, k_music, "MUSC\\Tracks"), 1u);
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), words({0}));
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key(k_music), list, 0), ListStatus::updated);
  EXPECT_EQ(count(plugin, k_music, "MUSC\\Tracks"), 0u);
  EXPECT_TRUE(payloads(plugin, k_music, "TNAM").empty());
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key(k_music), list, 0),
            ListStatus::index_out_of_range);
  const auto reloaded = reload(plugin);
  EXPECT_EQ(count(reloaded, k_music, "MUSC\\Tracks"), 0u);
}

TEST(MusicTrackLists, SetByIndexKeepsCountAndRefusesMismatch) {
  auto plugin = read(build(fixture()));
  const auto list = track_list("MUSC", "MUSC\\Tracks");
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_music), list, 1, key(k_must)),
            ListStatus::updated);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_music), list, 1, key(k_must)),
            ListStatus::unchanged);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_music), list, 2, key(k_must)),
            ListStatus::index_out_of_range);
  EXPECT_EQ(fo4::schema::set_record_formlink_list_element(plugin, key(k_music), list, 0, key(k_misc)),
            ListStatus::target_type_mismatch);
  EXPECT_EQ(count(plugin, k_music, "MUSC\\Tracks"), 2u);
  EXPECT_EQ(payloads(plugin, k_music, "TNAM").at(0), words({k_must, k_must}));
}

TEST(MusicTrackLists, MustTracksAppendAndRemoveOnAbsentSubrecord) {
  auto plugin = read(build(fixture()));
  const auto list = track_list("MUST", "MUST\\Tracks");
  EXPECT_EQ(count(plugin, k_must, "MUST\\Tracks"), 0u);
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_must), list, 0, key(k_must)),
            ListStatus::updated);
  EXPECT_EQ(payloads(plugin, k_must, "SNAM").at(0), words({k_must}));
  EXPECT_EQ(order(plugin, k_must), (Order{"EDID", "SNAM"}));
  EXPECT_EQ(fo4::schema::remove_record_formlink_list_element(plugin, key(k_must), list, 0), ListStatus::updated);
  EXPECT_TRUE(payloads(plugin, k_must, "SNAM").empty());
  const auto reloaded = reload(plugin);
  EXPECT_EQ(count(reloaded, k_must, "MUST\\Tracks"), 0u);
}

TEST(MusicTrackLists, FieldPathCApiCountAndSetStillResolveOnTracks) {
  TempRoot root;
  std::filesystem::create_directories(root.path);
  fo4::write_plugin_file(read(build(fixture())), root.path / k_plugin);
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file((root.path / k_plugin).string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key music{k_plugin, k_music}, must{k_plugin, k_must}, misc{k_plugin, k_misc};
  size_t tracks = 0;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, music, "MUSC\\Tracks", &tracks), RADICLE_STATUS_OK);
  EXPECT_EQ(tracks, 2u);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 1, &must), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 2, &must), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 0, &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, music, "MUSC\\Tracks", 0, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, music, "MUSC\\Tracks", &tracks), RADICLE_STATUS_OK);
  EXPECT_EQ(tracks, 2u);
  radicle_plugin_destroy(plugin);
}

TEST(MusicTrackLists, AppendAfterLastConditionSubrecordWhenTracksAbsent) {
  auto entries = fixture();
  for (auto& entry : entries) {
    if (entry.id == k_must) {
      entry.subs = {{"EDID", zstr("Track")}, {"CITC", words({2})}, {"CTDA", filled(32, 0)},
                    {"CIS2", zstr("p")}, {"CTDA", filled(32, 0)}};
    }
  }
  auto plugin = read(build(entries));
  const auto list = track_list("MUST", "MUST\\Tracks");
  EXPECT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, key(k_must), list, 0, key(k_must)),
            ListStatus::updated);
  EXPECT_EQ(order(plugin, k_must), (Order{"EDID", "CITC", "CTDA", "CIS2", "CTDA", "SNAM"}));
}

TEST(MusicTrackLists, CuePointRowsDecodeAndWriteOneArrayElement) {
  // Regression: cue-point rows require a four-byte stride, so a wrong stride writes the value into the neighbouring cue point.
  auto entries = fixture();
  for (auto& entry : entries) {
    if (entry.id == k_must) { entry.subs.push_back({"FNAM", f32s({0.5f, 1.25f, -2.0f})}); }
  }
  auto plugin = read(build(entries));
  const auto cue = fo4::schema::scalar_field_descriptor("MUST\\CuePoint");
  ASSERT_TRUE(cue.has_value());
  EXPECT_EQ(fo4::record_field_count(plugin, key(k_must), *cue).value_or(0), 3u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(k_must), *cue, 1, "2.5"), fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(k_must), *cue, 3, "4.5"), fo4::ScalarMutationStatus::subrecord_missing);
  const auto reloaded = reload(plugin);
  EXPECT_EQ(payloads(reloaded, k_must, "FNAM"), (std::vector<Bytes>{f32s({0.5f, 2.5f, -2.0f})}));
}

TEST(MusicTrackLists, TruncatedCuePointPayloadRefusesElementWrites) {
  // Regression: a cue-point payload that is not a whole number of elements must not have its trailing bytes overwritten.
  auto entries = fixture();
  for (auto& entry : entries) {
    if (entry.id == k_must) { entry.subs.push_back({"FNAM", raw({0, 0, 0x80, 0x3F, 0x11})}); }
  }
  auto plugin = read(build(entries));
  const auto cue = fo4::schema::scalar_field_descriptor("MUST\\CuePoint");
  ASSERT_TRUE(cue.has_value());
  EXPECT_EQ(fo4::record_field_count(plugin, key(k_must), *cue).value_or(99), 0u);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key(k_must), *cue, 0, "2.5"), fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(payloads(plugin, k_must, "FNAM"), (std::vector<Bytes>{raw({0, 0, 0x80, 0x3F, 0x11})}));
}

TEST(MusicTrackLists, CApiNullInsertWritesZeroIntoPackedTracks) {
  TempRoot root;
  std::filesystem::create_directories(root.path / "out");
  fo4::write_plugin_file(read(build(fixture())), root.path / k_plugin);
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file((root.path / k_plugin).string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key music{k_plugin, k_music}, must{k_plugin, k_must};
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin, music, "MUSC\\Tracks", 0, &must), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_formlink_list_insert(plugin, music, "MUSC\\Tracks", 1, nullptr), RADICLE_STATUS_OK);
  size_t tracks = 0;
  EXPECT_EQ(radicle_plugin_formlink_list_size(plugin, music, "MUSC\\Tracks", &tracks), RADICLE_STATUS_OK);
  EXPECT_EQ(tracks, 4u);
  const auto saved = root.path / "out" / k_plugin;
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.string().c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);
  const auto reread = fo4::read_plugin_file(saved);
  EXPECT_EQ(payloads(reread, k_music, "TNAM"), (std::vector<Bytes>{words({k_must, 0, k_must, 0})}));
}
