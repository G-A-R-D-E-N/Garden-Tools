#pragma once

#include "fo4/nif/describe.hpp"
#include "fo4/nif/nif.hpp"

#include <vector>

namespace fo4::nif {

// Requires shapes read from nif; the five checks come back in a fixed order for verify_nif to append.
std::vector<VerifyCheck> lint_checks(const NifFile& nif, const std::vector<Shape>& shapes,
                                     const SidecarProbe& sidecar_readable = {});

}  // namespace fo4::nif
