#pragma once

#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_packfile.hpp"

namespace fo4::nif {

// Requires object 0 to be a hknpRagdollData root, at least one bone, bone_to_body with one body index per bone, material ids that are a permutation of the bodies, and every object index inside the packfile; anything else raises std::invalid_argument.
// Invariant: the root, the hkaSkeleton and the constraint data objects are rebuilt from typed fields, and every other object is copied by index with its fixups.
HknpPackfile encode_hknp_ragdoll(const HknpSystem& sys);

}  // namespace fo4::nif
