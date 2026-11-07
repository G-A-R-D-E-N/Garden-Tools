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

// ponytail: single-source copy until Shared packed-geometry authoring lands in Radicle NIF.
BakeOcStatus bake_oc_single_source(const std::filesystem::path& source_nif,
                                   const std::filesystem::path& output_oc_nif);

BakeOcStatus bake_oc_single_source_bytes(std::span<const std::byte> source,
                                         const std::filesystem::path& output_oc_nif);

}  // namespace fo4::previs
