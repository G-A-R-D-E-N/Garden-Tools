#include "fo4/nif/nif.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"
#include "nif_internal.hpp"

#include <cstdint>

namespace fo4::nif {

std::uint32_t read_bsx_flags(const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks().at(block);
  if (info.type != "BSXFlags") {
    throw parse_error(info.offset, "block is not a BSXFlags");
  }
  auto cursor = detail::block_cursor(nif, block);
  cursor.read_i32();
  return cursor.read_u32();
}

bool is_editor_marker(std::uint32_t bsx_flags) noexcept {
  return (bsx_flags & k_bsx_editor_marker_bit) != 0;
}

}  // namespace fo4::nif
