#pragma once

#include "fo4/previs/uvd_tile_body.hpp"
#include "fo4/previs/uvd_tile_check.hpp"
#include "fo4/previs/uvd_tile_header.hpp"

namespace fo4::previs::detail {

void validate_uvd_bodies(const UvdTileModel& model, const UvdBodies& bodies, UvdTileClaims& claims);
void validate_uvd_header(const UvdTileModel& model, const UvdHeader& header, UvdTileClaims& claims);

}  // namespace fo4::previs::detail
