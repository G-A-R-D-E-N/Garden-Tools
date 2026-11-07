#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fo4::previs {

enum class BakeOcStatus {
  ok,
  io_error,
  invalid_input,
  unsupported,
};

bool oc_nif_has_shared_combined_geom(std::span<const std::byte> nif_bytes);

// Pass-through bake for measured _OC.NIF inputs until Shared packed-geometry authoring lands.
BakeOcStatus bake_oc_single_source(const std::filesystem::path& source_nif,
                                   const std::filesystem::path& output_oc_nif);

BakeOcStatus bake_oc_single_source_bytes(std::span<const std::byte> source,
                                         const std::filesystem::path& output_oc_nif);

}  // namespace fo4::previs
