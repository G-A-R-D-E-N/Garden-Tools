#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/nif/nif_write.hpp>

#include <fo4ide/assets/UvSurface.hpp>
#include <fo4ide/assets/VertexColorSurface.hpp>
#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

Result<std::string> renderGeometry(const std::filesystem::path& nifPath);
Result<std::string> renderInspect(const std::filesystem::path& nifPath);
Result<std::string> renderTree(const std::filesystem::path& nifPath);
Result<std::string> renderVerify(const std::filesystem::path& nifPath);
Result<std::string> renderVerifyJson(const std::filesystem::path& nifPath);
Result<std::string> renderSegments(const std::filesystem::path& nifPath, std::uint32_t block);
Result<std::string> renderUvSummary(const std::filesystem::path& nifPath);
Result<std::string> applyUvTransform(const std::filesystem::path& nifPath, std::uint32_t block, int channel,
                                     const UvTransform& transform, const std::filesystem::path& outputPath);
Result<std::string> renderVertexColors(const std::filesystem::path& nifPath);
Result<std::string> applyVertexColor(const std::filesystem::path& nifPath, const fo4::nif::VertexColorOp& op,
                                     const std::filesystem::path& outputPath);

}
