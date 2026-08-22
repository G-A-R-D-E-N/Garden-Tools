#pragma once

#include "fo4/plugin/container.hpp"

namespace fo4 {

// Rebuild TES4 ONAM from the current record tree using the Fallout 4 writer
// rule evidenced in xEdit. Only master-owned, non-persistent overrides of the
// record families that participate in ONAM are emitted. Existing ONAM data is
// replaced deterministically.
void rebuild_tes4_onam(Plugin& plugin);

}  // namespace fo4
