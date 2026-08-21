#pragma once

#include "fo4/signature.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace fo4 {

std::vector<std::size_t> vmad_formid_offsets(std::span<const std::byte> data,
                                             Signature record_signature);

// Returns nullopt when the VMAD uses a property layout Radicle cannot safely
// remap yet. Malformed/truncated VMAD remains tolerant and returns an empty
// vector so read-only inspection retains its previous behavior.
std::optional<std::vector<std::size_t>> vmad_formid_offsets_for_remap(
    std::span<const std::byte> data,
    Signature record_signature);

}  // namespace fo4
