#pragma once

#include "fo4/swf/swf.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4::swf {

enum class SwfSoundEditStatus : std::uint8_t {
  ok,
  unknown_tag,
  unknown_sound,
  not_a_define_sound,
  not_a_start_sound,
  unknown_compression,
  bad_rate,
  data_too_large,
  malformed_file,
  malformed_tag,
  encode_failed,
};

/* Requires a DefineSound whose id is header.sound_id; header.data_size is derived from data and ignored. */
SwfSoundEditStatus replace_sound_data(SwfFile& file, const SwfSound& header,
                                      std::span<const std::byte> data);

/* Requires a StartSound at tag_index and a DefineSound with new_sound_id; its SOUNDINFO bytes are kept. */
SwfSoundEditStatus retarget_start_sound(SwfFile& file, std::size_t tag_index,
                                        std::uint16_t new_sound_id);

}  // namespace fo4::swf
