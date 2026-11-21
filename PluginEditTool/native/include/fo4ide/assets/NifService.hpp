#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// Invariant: each model op reads only its own fields; an absent number is nullopt so a missing argument is not read as zero.
struct NifModelOpArgs {
    std::vector<std::uint32_t> tris;
    std::optional<float> distance;
    std::vector<std::uint32_t> loopA;
    std::vector<std::uint32_t> loopB;
    std::optional<std::array<float, 3>> normal;
    std::optional<float> offset;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
    std::optional<float> width;
};

// Invariant: value holds the numbers of one key exactly as the channel stores them (see NifService::blockOp set_keys).
struct NifKeyArg {
    float time{0.0f};
    std::vector<float> value;
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
                            const std::filesystem::path& outputPath, std::string_view check = {},
                            bool overwrite = false) const;
    Result<std::string> shapeOp(const std::filesystem::path& nifPath,
                                const std::filesystem::path& outputPath,
                                std::string_view op,
                                std::vector<std::uint32_t> blocks,
                                float creaseDegrees,
                                bool overwrite = false,
                                float ratio = 0.5f,
                                std::uint32_t maxInfluences = 4,
                                float pruneThreshold = 0.0f,
                                std::optional<std::uint32_t> sourceBlock = std::nullopt,
                                const NifModelOpArgs& model = {}) const;
    Result<std::string> importObj(const std::filesystem::path& objPath,
                                  const std::filesystem::path& outputPath,
                                  const NifImportOptions& options = {}, bool overwrite = false) const;
    Result<std::string> importGltf(const std::filesystem::path& gltfPath,
                                   const std::filesystem::path& outputPath,
                                   const NifImportOptions& options = {},
                                   bool overwrite = false) const;
    Result<std::string> applyEdits(const std::filesystem::path& nifPath,
                                   const nlohmann::json& edits,
                                   const std::filesystem::path& outputPath = {},
                                   std::string_view expectedSource = {}) const;
    Result<std::string> connectPoints(const std::filesystem::path& nifPath) const;
    Result<std::string> influences(const std::filesystem::path& nifPath, std::uint32_t block,
                                   std::uint32_t vertex) const;
    Result<std::string> influencedBy(const std::filesystem::path& nifPath, std::uint32_t block,
                                     std::int32_t bone, float minimum = 0.01f) const;
    Result<std::string> validateSkin(const std::filesystem::path& nifPath, std::uint32_t block) const;
    Result<std::string> applyConnectPoints(const std::filesystem::path& nifPath,
                                           const std::string& opsText,
                                           const std::filesystem::path& outputPath,
                                           bool overwrite = false) const;
    Result<std::string> createTextureSet(const std::filesystem::path& nifPath,
                                         std::uint32_t block,
                                         const std::vector<std::string>& textures,
                                         const std::filesystem::path& outputPath = {},
                                         std::string_view expectedSource = {},
                                         bool overwrite = false) const;
    Result<std::string> attachAlpha(const std::filesystem::path& nifPath,
                                    std::uint32_t block,
                                    std::uint16_t flags,
                                    std::uint8_t threshold,
                                    const std::filesystem::path& outputPath = {},
                                    std::string_view expectedSource = {},
                                    bool overwrite = false) const;
    Result<std::string> removeBlock(const std::filesystem::path& nifPath,
                                    std::uint32_t block,
                                    const std::filesystem::path& outputPath = {},
                                    std::string_view expectedSource = {},
                                    bool overwrite = false) const;
    Result<std::string> blockOp(const std::filesystem::path& nifPath,
                                std::string_view op,
                                std::optional<std::uint32_t> block,
                                std::optional<std::uint32_t> child,
                                const std::filesystem::path& donorPath,
                                const std::vector<std::uint32_t>& blocks,
                                const std::filesystem::path& outputPath = {},
                                std::string_view expectedSource = {},
                                bool overwrite = false,
                                const std::vector<std::vector<std::uint32_t>>& groups = {},
                                const std::filesystem::path& posePath = {},
                                std::string_view pose = {},
                                std::string_view channel = {},
                                std::optional<std::uint32_t> interpolation = std::nullopt,
                                const std::vector<NifKeyArg>& keys = {}) const;
    Result<std::string> listPoses(const std::filesystem::path& posePath,
                                  const std::filesystem::path& nifPath = {},
                                  std::optional<std::uint32_t> block = std::nullopt) const;

private:
    Result<void> requireFile(const std::filesystem::path& path, std::string_view label) const;
};

}
