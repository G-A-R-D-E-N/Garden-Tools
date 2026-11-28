#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

class TextureResolver;

class NifMaterialLink {
public:
    explicit NifMaterialLink(TextureResolver& textures);

    Result<std::string> compare(const std::filesystem::path& nifPath, int shape, std::string_view textureRoot) const;
    Result<std::string> textureEdits(const std::filesystem::path& nifPath, int shape, std::string_view textureRoot) const;
    Result<std::string> shapes(const std::filesystem::path& nifPath, std::string_view material) const;

private:
    TextureResolver& textures_;
};

}
