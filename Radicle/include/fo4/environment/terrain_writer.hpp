#pragma once

#include "fo4/environment/terrain.hpp"
#include "fo4/plugin/container.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fo4 {

enum class LandMutationStatus {
  updated,
  unchanged,
  record_missing,
  record_mismatch,
  subrecord_missing,
  size_mismatch,
  value_invalid,
  height_unrepresentable,
  height_out_of_range,
  texture_missing,
  texture_mismatch,
  quadrant_invalid,
  index_out_of_range,
};

LandMutationStatus set_land_heights(Plugin& plugin,
                                    const FormKey& key,
                                    std::span<const float> heights);

LandMutationStatus set_land_normals(Plugin& plugin,
                                    const FormKey& key,
                                    std::span<const std::byte> normals);

LandMutationStatus set_land_vertex_colors(Plugin& plugin,
                                          const FormKey& key,
                                          std::span<const std::byte> colors);

LandMutationStatus set_land_base_texture(Plugin& plugin,
                                         const FormKey& key,
                                         std::uint8_t quadrant,
                                         const FormKey& texture,
                                         std::uint16_t layer);

LandMutationStatus add_land_alpha_layer(Plugin& plugin,
                                        const FormKey& key,
                                        std::uint8_t quadrant,
                                        const FormKey& texture,
                                        std::uint16_t layer,
                                        std::span<const float> alpha,
                                        std::size_t* out_index = nullptr);

LandMutationStatus remove_land_alpha_layer(Plugin& plugin,
                                           const FormKey& key,
                                           std::size_t index);

}  // namespace fo4
