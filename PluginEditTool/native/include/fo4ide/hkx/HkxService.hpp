#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/core/Result.hpp>

namespace fo4ide::hkx {

enum class PoseSpace { Local, Model };

class HkxService {
public:
    Result<nlohmann::json> skeleton(const std::filesystem::path& skeletonPath) const;
    Result<nlohmann::json> pose(const std::filesystem::path& skeletonPath,
                                const std::filesystem::path& animationPath,
                                float time,
                                PoseSpace space) const;
    Result<nlohmann::json> poseBoth(const std::filesystem::path& skeletonPath,
                                    const std::filesystem::path& animationPath,
                                    float time) const;
    Result<nlohmann::json> loadClip(const std::filesystem::path& skeletonPath,
                                    const std::filesystem::path& animationPath) const;
    Result<nlohmann::json> summary(const std::filesystem::path& path) const;
    Result<nlohmann::json> inspectAnimation(const std::filesystem::path& animationPath,
                                            std::optional<std::size_t> track,
                                            std::size_t sampleLimit) const;
    Result<nlohmann::json> inspectObjects(const std::filesystem::path& path,
                                          std::string_view classPrefix,
                                          bool knownLayoutOnly,
                                          std::size_t limit) const;
    Result<nlohmann::json> rootMotion(const std::filesystem::path& skeletonPath,
                                      const std::filesystem::path& animationPath,
                                      std::int32_t bone,
                                      std::size_t sampleLimit) const;
    Result<nlohmann::json> inspectBehavior(const std::filesystem::path& path, std::size_t limit) const;
    Result<nlohmann::json> behaviorModel(const std::filesystem::path& path, std::size_t limit) const;
    Result<nlohmann::json> ragdoll(const std::filesystem::path& path, std::size_t limit) const;
    Result<nlohmann::json> mappers(const std::filesystem::path& path, bool recompute, std::size_t limit) const;
    Result<nlohmann::json> projectIndex(const std::filesystem::path& rootPath,
                                        std::string_view query,
                                        std::size_t limit) const;
    Result<nlohmann::json> skeletonDocument(const std::filesystem::path& skeletonPath) const;
    Result<nlohmann::json> editSkeleton(const std::filesystem::path& skeletonPath,
                                        std::string_view kind,
                                        std::size_t bone,
                                        std::string_view name,
                                        std::int32_t parent) const;

    Result<nlohmann::json> subgraphManifest(std::string_view idText,
                                            std::string_view behaviorPath,
                                            const std::vector<std::string>& pathPrefixes,
                                            const std::filesystem::path& dataFolder) const;

    // Constraint: the manifest must be read from `dataFolder` loose Meshes first, then that folder's .ba2 archives.
    Result<nlohmann::json> subgraph(const std::filesystem::path& pluginPath,
                                    std::string_view formIdText,
                                    const std::filesystem::path& dataFolder) const;

private:
    struct ClipSnapshot;

    Result<std::shared_ptr<const ClipSnapshot>> loadedClip(const std::filesystem::path& skeletonPath,
                                                           const std::filesystem::path& animationPath) const;

    mutable std::mutex cacheMutex_;
    mutable std::shared_ptr<const ClipSnapshot> cache_;
};

}
