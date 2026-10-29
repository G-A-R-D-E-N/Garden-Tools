#include "fo4/swf/swf_sound_edit.hpp"

#include "fo4/swf/swf_characters.hpp"
#include "fo4/swf/swf_sound.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fo4::swf {

namespace {

constexpr std::size_t kMaxSoundDataBytes = 64u * 1024u * 1024u;

constexpr std::uint8_t kMaxSoundRate = 3;

}  // namespace

SwfSoundEditStatus replace_sound_data(SwfFile& file, const SwfSound& header,
                                      std::span<const std::byte> data) {
  SwfCharacterModel model;
  std::string message;
  if (!build_character_model(file, model, message)) {
    return SwfSoundEditStatus::malformed_file;
  }
  const SwfCharacter* character = find_character(model, header.sound_id);
  if (character == nullptr) {
    return SwfSoundEditStatus::unknown_sound;
  }
  if (file.tags[character->tag_index].code != tag::kDefineSound) {
    return SwfSoundEditStatus::not_a_define_sound;
  }
  if (data.size() > kMaxSoundDataBytes) {
    return SwfSoundEditStatus::data_too_large;
  }
  if (header.rate > kMaxSoundRate) {
    return SwfSoundEditStatus::bad_rate;
  }
  std::vector<std::byte> payload = encode_define_sound(header, data);
  SwfDefineSoundData decoded;
  if (decode_define_sound(payload, decoded) != SwfSoundStatus::ok ||
      decoded.header.format != header.format) {
    return SwfSoundEditStatus::unknown_compression;
  }

  SwfTagPlan plan;
  plan.replacements.push_back(SwfTagReplacement{character->tag_index, std::move(payload)});
  SwfTagPlanResult applied;
  if (!apply_tag_plan(file, plan, applied)) {
    return SwfSoundEditStatus::encode_failed;
  }
  return SwfSoundEditStatus::ok;
}

SwfSoundEditStatus retarget_start_sound(SwfFile& file, std::size_t tag_index,
                                        std::uint16_t new_sound_id) {
  if (tag_index >= file.tags.size()) {
    return SwfSoundEditStatus::unknown_tag;
  }
  const SwfTag& target = file.tags[tag_index];
  if (target.code != tag::kStartSound) {
    return SwfSoundEditStatus::not_a_start_sound;
  }
  SwfStartSound start;
  if (decode_start_sound(target.payload, start) != SwfSoundStatus::ok) {
    return SwfSoundEditStatus::malformed_tag;
  }
  SwfCharacterModel model;
  std::string message;
  if (!build_character_model(file, model, message)) {
    return SwfSoundEditStatus::malformed_file;
  }
  const SwfCharacter* sound = find_character(model, new_sound_id);
  if (sound == nullptr || sound->kind != SwfCharacterKind::sound) {
    return SwfSoundEditStatus::unknown_sound;
  }
  std::vector<std::byte> payload;
  payload.push_back(static_cast<std::byte>(new_sound_id & 0xFFu));
  payload.push_back(static_cast<std::byte>(new_sound_id >> 8));
  payload.insert(payload.end(), target.payload.begin() + 2, target.payload.end());

  SwfTagPlan plan;
  plan.replacements.push_back(SwfTagReplacement{tag_index, std::move(payload)});
  SwfTagPlanResult applied;
  if (!apply_tag_plan(file, plan, applied)) {
    return SwfSoundEditStatus::encode_failed;
  }
  return SwfSoundEditStatus::ok;
}

}  // namespace fo4::swf
