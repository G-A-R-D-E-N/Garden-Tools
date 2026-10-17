#include "fo4/c_api_sound.h"
#include "fo4/environment/sound.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/text/encoding.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>

namespace {

class SoundRealData : public testing::TestWithParam<const char*> {};

void expect_link(const std::optional<fo4::FormKey>& native, radicle_form_key abi) {
  const bool present = native && !native->owner().empty();
  EXPECT_EQ(abi.plugin != nullptr, present);
  if (present && abi.plugin) {
    EXPECT_EQ(fo4::PluginKey::from_filename(abi.plugin), native->owner());
    EXPECT_EQ(abi.local_id, native->local().value());
  } else {
    EXPECT_EQ(abi.local_id, 0u);
  }
}

void expect_text(std::optional<std::string_view> native, const char* abi) {
  EXPECT_EQ(abi != nullptr, native.has_value());
  if (native && abi) {
    EXPECT_EQ(std::string_view{abi}, fo4::text::windows1252_to_utf8(*native));
  }
}

bool same_subrecords(const fo4::Record& left, const fo4::Record& right) {
  return left.header.flags.value() == right.header.flags.value() &&
         left.header.form_version == right.header.form_version && left.compressed == right.compressed &&
         left.subrecords.size() == right.subrecords.size() &&
         std::equal(left.subrecords.begin(), left.subrecords.end(), right.subrecords.begin(),
                    [](const fo4::Subrecord& a, const fo4::Subrecord& b) {
                      return a.signature == b.signature && a.extended_size == b.extended_size && a.data == b.data;
                    });
}

TEST_P(SoundRealData, DecodesEverySoundRecordThroughNativeAndAbiAndPreservesSubrecords) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) { GTEST_SKIP() << "missing official master: " << path; }
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  const auto name = path.string(); const char* names[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(names, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto native_records = env.plugin_records(0);
  ASSERT_EQ(count, native_records.size());
  std::map<std::string, int> totals{{"SNDR", 0}, {"SOUN", 0}, {"MUSC", 0}, {"FSTP", 0}, {"FSTS", 0}};
  for (std::size_t i = 0; i < count; ++i) {
    const auto* ref = native_records[i];
    const auto type = ref->record->header.signature.to_string();
    if (type != "SNDR" && type != "SOUN" && type != "MUSC" && type != "FSTP" && type != "FSTS") continue;
    SCOPED_TRACE(ref->record->form_key.to_string()); ++totals[type];
    if (type == "SNDR") {
      const auto native = fo4::env::decode_sound_descriptor(env, *ref);
      const radicle_sound_descriptor* d = nullptr;
      ASSERT_EQ(radicle_sound_descriptor_decode(abi, records[i], &d), RADICLE_STATUS_OK);
      ASSERT_NE(d, nullptr);
      EXPECT_EQ(d->has_kind, native.kind.has_value());
      if (native.kind) { EXPECT_EQ(d->kind, *native.kind); }
      expect_link(native.category, d->category);
      expect_link(native.alternate, d->alternate);
      expect_link(native.output_model, d->output_model);
      expect_link(native.base_descriptor, d->base_descriptor);
      ASSERT_EQ(d->descriptor_count, native.descriptors.size());
      for (std::size_t j = 0; j < native.descriptors.size(); ++j) {
        expect_link(native.descriptors[j], d->descriptors[j]);
      }
      ASSERT_EQ(d->file_count, native.files.size());
      for (std::size_t j = 0; j < native.files.size(); ++j) { expect_text(native.files[j], d->files[j].path); }
      EXPECT_EQ(d->has_controls, native.controls.has_value());
      if (native.controls) {
        for (std::size_t j = 0; j < native.controls->size(); ++j) { EXPECT_EQ(d->controls[j], (*native.controls)[j]); }
      }
      EXPECT_EQ(d->has_playback, native.playback.has_value());
      if (native.playback) {
        EXPECT_EQ(d->playback.frequency_shift, native.playback->frequency_shift);
        EXPECT_EQ(d->playback.frequency_variance, native.playback->frequency_variance);
        EXPECT_EQ(d->playback.priority, native.playback->priority);
        EXPECT_EQ(d->playback.db_variance, native.playback->db_variance);
        EXPECT_EQ(d->playback.attenuation_hundredths, native.playback->attenuation_hundredths);
      }
      ASSERT_EQ(d->rate_file_count, native.rate_files.size());
      for (std::size_t j = 0; j < native.rate_files.size(); ++j) {
        EXPECT_EQ(d->rate_files[j].rpm, native.rate_files[j].rpm);
        expect_text(native.rate_files[j].file, d->rate_files[j].path);
      }
      radicle_sound_descriptor_free(d);
    } else if (type == "SOUN") {
      const auto native = fo4::env::decode_sound_marker(env, *ref);
      radicle_sound_marker_info m{};
      ASSERT_EQ(radicle_sound_marker_get_info(abi, records[i], &m), RADICLE_STATUS_OK);
      expect_link(native.descriptor, m.descriptor);
      EXPECT_EQ(m.has_repeat, native.repeat.has_value());
      if (native.repeat) {
        EXPECT_EQ(m.min_time, native.repeat->min_time);
        EXPECT_EQ(m.max_time, native.repeat->max_time);
        EXPECT_EQ(m.has_stackable, native.repeat->stackable.has_value());
        if (native.repeat->stackable) { EXPECT_EQ(m.stackable, *native.repeat->stackable); }
      }
    } else if (type == "MUSC") {
      const auto native = fo4::env::decode_music_type(env, *ref);
      const radicle_music_type* m = nullptr;
      ASSERT_EQ(radicle_music_type_decode(abi, records[i], &m), RADICLE_STATUS_OK);
      ASSERT_NE(m, nullptr);
      EXPECT_EQ(m->has_flags, native.flags.has_value());
      if (native.flags) { EXPECT_EQ(m->flags, *native.flags); }
      EXPECT_EQ(m->has_priority, native.priority.has_value());
      if (native.priority) { EXPECT_EQ(m->priority, *native.priority); }
      EXPECT_EQ(m->ducking_hundredths, native.ducking_hundredths.value_or(0));
      EXPECT_EQ(m->has_fade_duration, native.fade_duration.has_value());
      if (native.fade_duration) { EXPECT_EQ(m->fade_duration, *native.fade_duration); }
      ASSERT_EQ(m->track_count, native.tracks.size());
      for (std::size_t j = 0; j < native.tracks.size(); ++j) { expect_link(native.tracks[j], m->tracks[j]); }
      radicle_music_type_free(m);
    } else if (type == "FSTP") {
      const auto native = fo4::env::decode_footstep(env, *ref);
      radicle_footstep_info f{};
      ASSERT_EQ(radicle_footstep_get_info(abi, records[i], &f), RADICLE_STATUS_OK);
      expect_link(native.impact_set, f.impact_set);
      expect_text(native.tag, f.tag);
    } else {
      const auto native = fo4::env::decode_footstep_set(env, *ref);
      const radicle_footstep_set* f = nullptr;
      ASSERT_EQ(radicle_footstep_set_decode(abi, records[i], &f), RADICLE_STATUS_OK);
      ASSERT_NE(f, nullptr);
      std::size_t offset = 0;
      for (std::size_t category = 0; category < native.categories.size(); ++category) {
        EXPECT_EQ(f->counts[category], native.categories[category].size());
        ASSERT_LE(offset + native.categories[category].size(), f->step_count);
        for (const auto& key : native.categories[category]) { expect_link(key, f->steps[offset++]); }
      }
      EXPECT_EQ(f->step_count, offset);
      radicle_footstep_set_free(f);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  for (const auto& [type, total] : totals) RecordProperty(type, total);
  int unresolved = 0;
  int cross_master = 0;
  for (const auto* ref : native_records) {
    if (ref->record->header.signature.to_string() != "SOUN") continue;
    const auto marker = fo4::env::decode_sound_marker(env, *ref);
    if (!marker.descriptor) continue;
    if (env.winner(*marker.descriptor) == nullptr) { ++cross_master; continue; }
    try {
      const auto files = fo4::env::resolve_sound_files(env, *ref);
      if (std::none_of(files.begin(), files.end(), [](std::string_view file) { return !file.empty(); })) ++unresolved;
    } catch (const fo4::env::WorldDecodeError&) {
      ++unresolved;
    }
  }
  RecordProperty("SoundMarkerUnresolved", unresolved);
  RecordProperty("SoundMarkerCrossMaster", cross_master);
  EXPECT_EQ(unresolved, 0);
  auto plugin = env.plugin(0);
  plugin.original_range.clear();
  fo4::visit_records(plugin, [](auto& record) {
    if (record.header.signature != fo4::sig_tes4) record.original_range.clear();
  });
  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, plugin.path);
  const fo4::RecordIndex index(reread);
  for (const auto* ref : native_records) {
    if (ref->record->header.signature == fo4::sig_tes4) continue;
    SCOPED_TRACE(ref->record->form_key.to_string());
    const auto* after = index.find(ref->record->form_key);
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(same_subrecords(*after, *ref->record));
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, SoundRealData,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
