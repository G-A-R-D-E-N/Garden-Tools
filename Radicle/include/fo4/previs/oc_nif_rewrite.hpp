#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace fo4::previs {

std::optional<std::vector<std::byte>> replace_shared_geom_extra_block(std::span<const std::byte> nif_bytes,
                                                                      std::span<const std::byte> new_extra_block);

std::optional<std::vector<std::byte>> replace_nif_block_at(std::span<const std::byte> nif_bytes,
                                                           std::size_t block_index,
                                                           std::span<const std::byte> new_block);

std::optional<std::vector<std::byte>> graft_first_shape_mesh_into_shell(std::span<const std::byte> shell_nif,
                                                                         std::span<const std::byte> stat_nif);

}  // namespace fo4::previs
