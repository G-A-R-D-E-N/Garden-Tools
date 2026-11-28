#pragma once

#include "fo4/nif/nif.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fo4::archive {
class AssetResolver;
}

namespace fo4::nif {

// Invariant: uri keeps the texture path as a relative URI, copy and png write the file beside the .glb, and embed stores DDS in the BIN chunk.
enum class GltfTextureMode : std::uint8_t { uri, copy, png, embed };

struct GltfOptions {
  bool materials{true};
  GltfTextureMode textures{GltfTextureMode::uri};
  bool tangents{true};
  bool vertex_colours{true};
  const archive::AssetResolver* assets{nullptr};
};

struct GltfFile {
  std::string path;  // relative to the .glb, '/' separated
  std::vector<std::byte> bytes;
};

struct GltfExport {
  std::vector<std::byte> glb;
  std::vector<GltfFile> files;
  std::vector<std::string> warnings;
};

// Invariant: the glb is one binary glTF 2.0 scene in metres, Y-up, with node transforms and inverse binds in NIF units; HKX clips are not written.
GltfExport export_glb(const NifFile& nif, const GltfOptions& options = {});

}  // namespace fo4::nif
