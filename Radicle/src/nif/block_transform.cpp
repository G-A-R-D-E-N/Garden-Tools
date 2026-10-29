#include "fo4/nif/nif.hpp"

#include "nif_internal.hpp"

namespace fo4::nif {

Transform block_transform(const NifFile& nif, std::uint32_t block) {
  auto cursor = detail::block_cursor(nif, block);
  return detail::read_av_header(cursor, nif).transform;
}

}  // namespace fo4::nif
