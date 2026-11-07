#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <fo4/nif/gltf_export.hpp>
#include <fo4ide/core/Result.hpp>

namespace fo4::archive {
class AssetResolver;
}

namespace fo4ide::assets {

struct NifGltfOptions {
    bool materials{true};
    fo4::nif::GltfTextureMode textures{fo4::nif::GltfTextureMode::png};
    bool tangents{true};
    bool vertexColours{true};
    const fo4::archive::AssetResolver* assets{nullptr};
};

std::optional<fo4::nif::GltfTextureMode> parseGltfTextureMode(std::string_view name);

Result<std::string> exportNifGltf(const std::filesystem::path& nifPath,
                                  const std::filesystem::path& outputPath,
                                  bool overwrite = false,
                                  const NifGltfOptions& options = {});

}
