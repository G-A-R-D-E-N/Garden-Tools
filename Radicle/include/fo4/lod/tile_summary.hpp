#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>

namespace fo4::lod {

struct LodTileSummary {
  std::size_t shapes{0};
  std::size_t subindex_shapes{0};
  std::size_t texture_sets{0};

  friend bool operator==(const LodTileSummary&, const LodTileSummary&) = default;
};

LodTileSummary summarize_lod_tile(const nif::NifFile& nif);

}  // namespace fo4::lod
