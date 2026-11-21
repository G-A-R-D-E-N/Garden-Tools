#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4/nif/nif_write.hpp>
#include <fo4/nif/skin.hpp>

#include <fo4ide/assets/UvSurface.hpp>
#include <fo4ide/assets/VertexColorSurface.hpp>
#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

Result<std::string> renderGeometry(const std::filesystem::path& nifPath);
Result<std::string> renderInspect(const std::filesystem::path& nifPath);
Result<std::string> renderTree(const std::filesystem::path& nifPath);
Result<std::string> renderVerify(const std::filesystem::path& nifPath);
Result<std::string> renderVerifyJson(const std::filesystem::path& nifPath);
std::vector<std::string> renderNewVerifyFailures(const std::filesystem::path& candidate,
                                                 const std::optional<std::filesystem::path>& baseline);
Result<std::string> renderSegments(const std::filesystem::path& nifPath, std::uint32_t block);
Result<std::string> renderUvSummary(const std::filesystem::path& nifPath);
bool isInPlaceSave(const std::filesystem::path& nifPath, const std::filesystem::path& outputPath);
Error outputExistsError(const std::filesystem::path& target);
Result<std::string> commitUvTransform(const std::filesystem::path& nifPath, const std::vector<std::byte>& source,
                                      const std::vector<std::byte>& bytes, const std::filesystem::path& outputPath,
                                      bool overwrite = false);
Result<std::string> applyUvTransform(const std::filesystem::path& nifPath, std::uint32_t block, int channel,
                                     const UvTransform& transform, const std::filesystem::path& outputPath,
                                     bool overwrite = false);
Result<std::string> renderVertexColors(const std::filesystem::path& nifPath);
Result<std::string> applyVertexColor(const std::filesystem::path& nifPath, const fo4::nif::VertexColorOp& op,
                                     const std::filesystem::path& outputPath, std::string_view expectedSource,
                                     bool overwrite = false);
Result<std::string> applySkinEdit(const std::filesystem::path& nifPath, const fo4::nif::SkinEditRequest& request,
                                  const std::filesystem::path& outputPath, std::string_view expectedSource = {},
                                  bool overwrite = false);
Result<std::string> applySkinTool(const std::filesystem::path& nifPath, std::string_view op, const nlohmann::json& body,
                                  const std::filesystem::path& outputPath, std::string_view expectedSource = {},
                                  bool overwrite = false);

}
