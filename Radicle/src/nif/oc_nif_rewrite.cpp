#include "fo4/previs/oc_nif_rewrite.hpp"

#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "write_model.hpp"
#include "write_refs.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fo4::previs {
namespace {

std::optional<std::vector<std::byte>> replace_nif_block(std::span<const std::byte> nif_bytes,
                                                        std::size_t block_index,
                                                        std::span<const std::byte> new_block) {
  if (nif_bytes.empty() || new_block.empty()) return std::nullopt;
  const auto nif = fo4::nif::read_nif(nif_bytes);
  if (block_index >= nif.blocks().size()) return std::nullopt;
  if (nif.blocks()[block_index].type != "BSPackedCombinedSharedGeomDataExtra") return std::nullopt;
  auto model = fo4::nif::write_detail::load_model(nif);
  if (block_index >= model.blocks.size()) return std::nullopt;
  model.blocks[block_index].assign(new_block.begin(), new_block.end());
  auto out = fo4::nif::write_detail::serialize(model);
  fo4::nif::read_nif(out);
  return out;
}

}

std::optional<std::vector<std::byte>> replace_nif_block_at(std::span<const std::byte> nif_bytes,
                                                             std::size_t block_index,
                                                             std::span<const std::byte> new_block) {
  if (new_block.empty()) return std::nullopt;
  return replace_nif_block(nif_bytes, block_index, new_block);
}

std::optional<std::vector<std::byte>> replace_shared_geom_extra_block(std::span<const std::byte> nif_bytes,
                                                                      std::span<const std::byte> new_extra_block) {
  const auto nif = fo4::nif::read_nif(nif_bytes);
  for (std::size_t index = 0; index < nif.blocks().size(); ++index) {
    if (nif.blocks()[index].type != "BSPackedCombinedSharedGeomDataExtra") continue;
    return replace_nif_block(nif_bytes, index, new_extra_block);
  }
  return std::nullopt;
}

bool is_shape_block(std::string_view type) { return fo4::nif::is_shape_block_type(type); }

std::optional<std::size_t> first_shape_block(const fo4::nif::NifFile& nif) {
  for (std::size_t index = 0; index < nif.blocks().size(); ++index) {
    if (is_shape_block(nif.blocks()[index].type)) return index;
  }
  return std::nullopt;
}

std::uint16_t block_type_index_for(fo4::nif::write_detail::Model& model, std::string_view type) {
  for (std::size_t index = 0; index < model.block_types.size(); ++index) {
    if (model.block_types[index] == type) return static_cast<std::uint16_t>(index);
  }
  model.block_types.emplace_back(type);
  return static_cast<std::uint16_t>(model.block_types.size() - 1);
}

// Constraint: name, extra-data list, controller and the collision, skin, shader and alpha references must stay the shell's, because the STAT's index the STAT file; only flags, transform, sphere and the mesh come from the STAT.
fo4::nif::write_detail::Bytes splice_shape_mesh(const fo4::nif::write_detail::Model& shell_model,
                                                std::uint32_t shell_block,
                                                const fo4::nif::write_detail::Model& stat_model,
                                                std::uint32_t stat_block) {
  using fo4::nif::write_detail::Bytes;
  fo4::nif::write_detail::ref_offsets(shell_model, shell_block);
  fo4::nif::write_detail::ref_offsets(stat_model, stat_block);
  const auto& shell = shell_model.blocks[shell_block];
  const auto& stat = stat_model.blocks[stat_block];
  const std::size_t shell_extras = fo4::nif::write_detail::uint_at(shell, 4);
  const std::size_t stat_extras = fo4::nif::write_detail::uint_at(stat, 4);
  Bytes out;
  const auto take = [&out](const Bytes& from, std::size_t begin, std::size_t end) {
    out.insert(out.end(), from.begin() + static_cast<std::ptrdiff_t>(begin),
               from.begin() + static_cast<std::ptrdiff_t>(end));
  };
  take(shell, 0, 12 + 4 * shell_extras);
  take(stat, 12 + 4 * stat_extras, 68 + 4 * stat_extras);
  take(shell, 68 + 4 * shell_extras, 72 + 4 * shell_extras);
  take(stat, 72 + 4 * stat_extras, 88 + 4 * stat_extras);
  take(shell, 88 + 4 * shell_extras, 100 + 4 * shell_extras);
  take(stat, 100 + 4 * stat_extras, stat.size());
  return out;
}

std::optional<std::vector<std::byte>> graft_first_shape_mesh_into_shell(std::span<const std::byte> shell_nif,
                                                                         std::span<const std::byte> stat_nif) {
  if (shell_nif.empty() || stat_nif.empty()) return std::nullopt;
  const auto shell = fo4::nif::read_nif(shell_nif);
  const auto stat = fo4::nif::read_nif(stat_nif);
  const auto shell_shape = first_shape_block(shell);
  const auto stat_shape = first_shape_block(stat);
  if (!shell_shape.has_value() || !stat_shape.has_value()) return std::nullopt;
  auto shell_model = fo4::nif::write_detail::load_model(shell);
  const auto stat_model = fo4::nif::write_detail::load_model(stat);
  if (*shell_shape >= shell_model.blocks.size() || *stat_shape >= stat_model.blocks.size()) return std::nullopt;
  const auto& stat_type = stat.blocks()[*stat_shape].type;
  try {
    shell_model.blocks[*shell_shape] = splice_shape_mesh(shell_model, static_cast<std::uint32_t>(*shell_shape),
                                                         stat_model, static_cast<std::uint32_t>(*stat_shape));
    shell_model.block_type_index[*shell_shape] = block_type_index_for(shell_model, stat_type);
    fo4::nif::write_detail::ref_offsets(shell_model, static_cast<std::uint32_t>(*shell_shape));
  } catch (const fo4::parse_error&) {
    return std::nullopt;
  }
  auto out = fo4::nif::write_detail::serialize(shell_model);
  fo4::nif::read_nif(out);
  return out;
}

}
