#pragma once

#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <string_view>

namespace fo4 {

enum class SoundDescriptorMutationStatus {
  updated,
  unchanged,
  record_missing,
  record_mismatch,
  discriminator_unproven,
  already_present,
  subrecord_missing,
  index_out_of_range,
  payload_unproven,
};

// Constraint: file names are the SNDR ANAM array, inserted after the last ANAM and before the descriptors.
SoundDescriptorMutationStatus add_sound_descriptor_file(Plugin& plugin,
                                                        const FormKey& descriptor,
                                                        std::string_view file,
                                                        std::size_t* out_index = nullptr);

SoundDescriptorMutationStatus remove_sound_descriptor_file(Plugin& plugin,
                                                           const FormKey& descriptor,
                                                           std::size_t index);

SoundDescriptorMutationStatus set_sound_descriptor_file(Plugin& plugin,
                                                        const FormKey& descriptor,
                                                        std::size_t index,
                                                        std::string_view file);

// Constraint: the BNAM base-descriptor arm exists only when CNAM is AutoWeapon; other kinds keep the values struct.
SoundDescriptorMutationStatus add_sound_descriptor_base(Plugin& plugin,
                                                        const FormKey& descriptor,
                                                        const FormKey& target);

SoundDescriptorMutationStatus remove_sound_descriptor_base(Plugin& plugin,
                                                           const FormKey& descriptor);

}  // namespace fo4
