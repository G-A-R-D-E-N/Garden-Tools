#pragma once

#include "fo4/environment/world.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace fo4::env {

struct SoundPlayback {
  std::int8_t frequency_shift{0};
  std::int8_t frequency_variance{0};
  std::uint8_t priority{0};
  std::uint8_t db_variance{0};
  std::uint16_t attenuation_hundredths{0};
};

struct SoundRateFile {
  std::uint32_t rpm{0};
  std::string_view file;
};

struct SoundDescriptorData {
  std::optional<std::uint32_t> kind;
  std::optional<FormKey> category;
  std::optional<FormKey> alternate;
  std::optional<FormKey> output_model;
  std::optional<FormKey> base_descriptor;
  std::vector<FormKey> descriptors;
  std::vector<std::string_view> files;
  std::optional<std::array<std::uint8_t, 4>> controls;
  std::optional<SoundPlayback> playback;
  std::vector<SoundRateFile> rate_files;
};

struct SoundRepeat {
  float min_time{0};
  float max_time{0};
  std::optional<std::uint8_t> stackable;
};

struct SoundMarkerData {
  std::optional<FormKey> descriptor;
  std::optional<SoundRepeat> repeat;
};

struct MusicTypeData {
  std::optional<std::uint32_t> flags;
  std::optional<std::uint16_t> priority;
  std::optional<std::uint16_t> ducking_hundredths;
  std::optional<float> fade_duration;
  std::vector<FormKey> tracks;
};

struct FootstepData {
  std::optional<FormKey> impact_set;
  std::optional<std::string_view> tag;
};

struct FootstepSetData {
  std::array<std::vector<FormKey>, 5> categories;
};

SoundDescriptorData decode_sound_descriptor(const Environment& env, const RecordRef& ref);
std::vector<SoundRateFile> decode_sound_rate_files(const Record& record);
SoundMarkerData decode_sound_marker(const Environment& env, const RecordRef& ref);
std::optional<SoundRepeat> decode_sound_repeat(const Record& record);
MusicTypeData decode_music_type(const Environment& env, const RecordRef& ref);
FootstepData decode_footstep(const Environment& env, const RecordRef& ref);
FootstepSetData decode_footstep_set(const Environment& env, const RecordRef& ref);
std::array<std::uint32_t, 5> decode_footstep_counts(const Record& record);
std::vector<std::string_view> resolve_sound_files(const Environment& env, const RecordRef& ref);
std::optional<std::size_t> select_rate_file_index(const std::vector<SoundRateFile>& rate_files, std::uint32_t rpm);
std::size_t select_standard_file_index(std::size_t count, std::size_t draw_index,
                                       std::optional<std::size_t> previous_index);
double sound_frequency_factor(std::int8_t frequency_shift);
bool sound_is_looping(std::uint8_t loop_control);
double sound_sidechain_send(std::uint8_t sidechain_control);
double sound_small_rumble_send(std::uint8_t rumble_control);
double sound_big_rumble_send(std::uint8_t rumble_control);

}
