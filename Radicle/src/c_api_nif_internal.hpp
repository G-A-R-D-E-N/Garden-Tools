#pragma once

#include "fo4/nif/collision.hpp"
#include "fo4/nif/nif.hpp"

#include <vector>

// Invariant: radicle_nif_read fills every member once and nothing mutates them, so they must stay read-only.
struct radicle_nif {
  fo4::nif::NifFile file;
  std::vector<fo4::nif::Shape> shapes;
  std::vector<fo4::nif::CollisionBlob> collisions;
};
