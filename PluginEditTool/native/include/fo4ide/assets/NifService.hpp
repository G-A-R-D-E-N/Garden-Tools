#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::assets {

enum class NifCollisionShape : std::uint8_t { none, box, hull, mesh };

std::optional<NifCollisionShape> parseCollisionShape(std::string_view text);

struct NifImportOptions {
    std::string material;
    std::string diffuseTexture;
    std::string normalTexture;
    NifCollisionShape collision{NifCollisionShape::none};
    bool fromBlender{false};
    double massKg{0.0};
    std::filesystem::path skeleton;
};

class NifService {
public:
    NifService() = default;

    Result<std::string> inspect(const std::filesystem::path& nifPath) const;
    Result<std::string> geometry(const std::filesystem::path& nifPath) const;
    Result<std::string> verify(const std::filesystem::path& nifPath) const;
    Result<std::string> tree(const std::filesystem::path& nifPath) const;
    Result<std::string> segments(const std::filesystem::path& nifPath, std::uint32_t block) const;
    Result<std::string> fix(const std::filesystem::path& nifPath,
                            const std::filesystem::path& outputPath, std::string_view check = {}) const;
    Result<std::string> shapeOp(const std::filesystem::path& nifPath,
                                const std::filesystem::path& outputPath,
                                std::string_view op,
                                std::vector<std::uint32_t> blocks,
                                float creaseDegrees,
                                bool overwrite = false,
                                float ratio = 0.5f,
                                std::uint32_t maxInfluences = 4,
                                float pruneThreshold = 0.0f,
                                std::optional<std::uint32_t> sourceBlock = std::nullopt) const;
    Result<std::string> importObj(const std::filesystem::path& objPath,
                                  const std::filesystem::path& outputPath,
                                  const NifImportOptions& options = {}) const;
    Result<std::string> importGltf(const std::filesystem::path& gltfPath,
                                   const std::filesystem::path& outputPath,
                                   const NifImportOptions& options = {},
                                   bool overwrite = false) const;
    Result<std::string> applyEdits(const std::filesystem::path& nifPath,
                                   const nlohmann::json& edits,
                                   const std::filesystem::path& outputPath = {}) const;
    Result<std::string> connectPoints(const std::filesystem::path& nifPath) const;
    Result<std::string> applyConnectPoints(const std::filesystem::path& nifPath,
                                           const std::string& opsText,
                                           const std::filesystem::path& outputPath) const;
    Result<std::string> createTextureSet(const std::filesystem::path& nifPath,
                                         std::uint32_t block,
                                         const std::vector<std::string>& textures,
                                         const std::filesystem::path& outputPath = {}) const;

private:
    Result<void> requireFile(const std::filesystem::path& path, std::string_view label) const;
};

}
