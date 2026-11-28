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

struct AnimationIndexQuery {
    std::filesystem::path dataFolder;
    std::filesystem::path mo2Folder;
    std::string profile;
    std::string kind;
    std::string project;
    std::string race;
    std::string character;
    std::string query;
    std::string resolvePath;
    std::size_t offset{0};
    std::size_t limit{200};
    bool refresh{false};
};

struct GltfSaveRequest {
    std::filesystem::path templatePath;
    std::filesystem::path skeletonPath;
    std::filesystem::path gltfPath;
    std::filesystem::path outputPath;
    std::string animation;
    std::string skin;
    bool replaceAnnotations{false};
    bool replaceMotion{false};
    bool overwrite{false};
};

// Invariant: an empty path is a document or input that was not given; ragdoll and mapper left empty mean the skeleton file.
struct CharacterCheckRequest {
    std::filesystem::path skeletonPath;
    std::filesystem::path ragdollPath;
    std::filesystem::path mapperPath;
    std::filesystem::path bodyNifPath;
    std::filesystem::path skeletonNifPath;
    std::string projectHkx;
    std::filesystem::path dataFolder;
    std::filesystem::path mo2InstanceDir;
    std::string mo2Profile;
};

// Invariant: a document is planned when its source or its target is given; an empty target means in place, which only a skeleton save allows and only with overwrite.
struct CharacterSavePlanRequest {
    std::filesystem::path skeletonPath;
    std::filesystem::path skeletonOutPath;
    std::filesystem::path ragdollPath;
    std::filesystem::path ragdollOutPath;
    std::filesystem::path mapperPath;
    std::filesystem::path mapperOutPath;
    std::filesystem::path skinPath;
    std::filesystem::path skinOutPath;
    nlohmann::json skeletonEdits;
    // Invariant: false refuses any document whose out path is its own source, an empty skeleton out path included.
    bool overwrite{false};
};

// Invariant: a stage is saved when its source is given; skeleton edits may be empty (a copy), ragdoll, mapper and skin edits may not.
struct CharacterSaveStageArgs {
    std::filesystem::path source;
    std::filesystem::path target;
    nlohmann::json edits;
    std::string expectedSourceSha256;
};

struct CharacterSaveArgs {
    CharacterSaveStageArgs skeleton;
    CharacterSaveStageArgs ragdoll;
    CharacterSaveStageArgs mapper;
    CharacterSaveStageArgs skin;
    std::size_t mapperIndex{0};
    bool overwrite{false};
};

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
    Result<nlohmann::json> readBehaviorAuthoring(const std::filesystem::path& path) const;
    Result<nlohmann::json> validateBehaviorGraph(const std::filesystem::path& path) const;
    Result<nlohmann::json> editBehaviorGraph(const std::filesystem::path& path,
                                             const nlohmann::json& edits,
                                             const std::filesystem::path& outPath,
                                             bool overwrite) const;
    Result<nlohmann::json> ragdoll(const std::filesystem::path& path, std::size_t limit) const;
    Result<nlohmann::json> mappers(const std::filesystem::path& path, bool recompute, std::size_t limit) const;
    Result<nlohmann::json> readScene(const std::filesystem::path& path) const;
    Result<nlohmann::json> editMapper(const std::filesystem::path& path,
                                      std::size_t mapper,
                                      const nlohmann::json& edits,
                                      const std::filesystem::path& outPath,
                                      bool overwrite,
                                      std::string_view expectedSource,
                                      std::size_t limit) const;
    Result<nlohmann::json> projectIndex(const std::filesystem::path& rootPath,
                                        std::string_view query,
                                        std::size_t limit) const;
    Result<nlohmann::json> skeletonDocument(const std::filesystem::path& skeletonPath) const;
    Result<nlohmann::json> editSkeleton(const std::filesystem::path& skeletonPath,
                                        std::string_view kind,
                                        std::size_t bone,
                                        std::string_view name,
                                        std::int32_t parent) const;
    Result<nlohmann::json> previewSkeleton(const std::filesystem::path& skeletonPath,
                                           const nlohmann::json& edits) const;
    // Constraint: an empty outputPath saves in place; expectedHash is the source_hash the caller last saw, empty skips the check.
    // Constraint: overwrite must be true to replace a different existing destination; the panel never sets it.
    Result<nlohmann::json> saveSkeleton(const std::filesystem::path& skeletonPath,
                                        const nlohmann::json& edits,
                                        const std::filesystem::path& outputPath,
                                        std::string_view expectedHash,
                                        bool overwrite = false) const;
    // Constraint: an empty outPath previews; an existing outPath, the source included, needs overwrite.
    Result<nlohmann::json> writeSkeleton(const std::filesystem::path& skeletonPath,
                                         const nlohmann::json& edits,
                                         const std::filesystem::path& outPath,
                                         bool overwrite,
                                         std::string_view expectedHash) const;

    // Constraint: a ragdoll save writes a new file only after the edited bytes were re-read and matched; a refusal writes nothing.
    Result<nlohmann::json> saveRagdoll(const std::filesystem::path& path,
                                       const std::filesystem::path& outPath,
                                       const nlohmann::json& editsJson,
                                       bool overwrite,
                                       std::string_view expectedSource) const;
    // Constraint: a preview is stateless and bounded, opens its own simulation from the file and never writes anything.
    Result<nlohmann::json> previewRagdoll(const std::filesystem::path& path, const nlohmann::json& options) const;

    // Invariant: an empty outPath is a preview that writes nothing; ops are run in order and any refusal writes nothing.
    // Constraint: an unset spline keeps the source's storage class; an unset budget keeps the Radicle default.
    struct ClipSaveOptions {
        std::optional<bool> spline;
        std::optional<float> maxPositionError;
        std::optional<float> maxRotationError;
        std::optional<float> maxScaleError;
    };

    Result<nlohmann::json> clipEdit(const std::filesystem::path& path,
                                    const nlohmann::json& ops,
                                    const ClipSaveOptions& save,
                                    const std::filesystem::path& outPath,
                                    std::string_view expectedSource,
                                    bool overwrite) const;

    // Invariant: an empty outPath previews and writes nothing; a write needs an outPath other than the source and a conversion that ended Ok.
    // Constraint: an empty targetVersion means the file's own version.
    Result<nlohmann::json> convertAsset(const std::filesystem::path& path,
                                        std::string_view targetVersion,
                                        const std::filesystem::path& outPath,
                                        std::string_view expectedSource,
                                        bool overwrite) const;

    // Constraint: with an MO2 instance the assets are read through its enabled mods, highest priority first, and a data folder then replaces the instance's game Data folder.
    Result<nlohmann::json> subgraphManifest(std::string_view idText,
                                            std::string_view behaviorPath,
                                            const std::vector<std::string>& pathPrefixes,
                                            const std::filesystem::path& dataFolder,
                                            const std::filesystem::path& mo2InstanceDir = {},
                                            std::string_view mo2Profile = {}) const;

    // Requires hkxPath to be a Data-relative path below meshes; the chain is project, character, behaviour, skeleton and animations, each resolved loose or in an archive.
    Result<nlohmann::json> projectChain(std::string_view hkxPath,
                                        const std::filesystem::path& dataFolder,
                                        const std::filesystem::path& mo2InstanceDir,
                                        std::string_view mo2Profile) const;

    Result<nlohmann::json> projectCheck(std::string_view hkxPath,
                                        const std::filesystem::path& dataFolder,
                                        const std::filesystem::path& mo2InstanceDir,
                                        std::string_view mo2Profile,
                                        std::size_t fileLimit,
                                        std::string_view reportFormat = {}) const;

    // Constraint: both calls only read; the report and the plan are computed by fo4::hkx::check_character and plan_character_save in Radicle.
    Result<nlohmann::json> characterCheck(const CharacterCheckRequest& request) const;
    Result<nlohmann::json> characterSavePlan(const CharacterSavePlanRequest& request) const;
    Result<nlohmann::json> characterSave(const CharacterSaveArgs& request) const;

    // Constraint: the manifest must be read from `dataFolder` loose Meshes first, then that folder's .ba2 archives.
    Result<nlohmann::json> subgraph(const std::filesystem::path& pluginPath,
                                    std::string_view formIdText,
                                    const std::filesystem::path& dataFolder) const;

    // Invariant: the index is built once per Data folder, MO2 folder and profile and reused until `refresh` is set.
    Result<nlohmann::json> animationIndex(const AnimationIndexQuery& request) const;

    // Constraint: the template is only read; the result goes to a new output path and never replaces an existing one unless overwrite is set.
    Result<nlohmann::json> saveGltfAnimation(const GltfSaveRequest& request) const;

    // Requires a data-relative .hkx, .hkt or .nif path; a loose winner is returned in place and an archive winner is written under the OS temp directory in FO4RE_Hkx (never evicted), never under a Data or mod folder.
    Result<nlohmann::json> extractAnimationAsset(const AnimationIndexQuery& sources, std::string_view path) const;

    // Constraint: the read-only inspection calls below never write; implemented in HkxInspectService.cpp.
    Result<nlohmann::json> inspectObject(const std::filesystem::path& path, std::size_t object, std::size_t limit) const;
    Result<nlohmann::json> objectTree(const std::filesystem::path& path, std::size_t depth, std::size_t limit) const;
    Result<nlohmann::json> packfileDiagnostics(const std::filesystem::path& path) const;
    Result<nlohmann::json> repackCheck(const std::filesystem::path& path) const;
    Result<nlohmann::json> searchHkx(const std::filesystem::path& target, std::string_view query, std::size_t limit) const;
    Result<nlohmann::json> auditChannels(const std::filesystem::path& skeletonPath,
                                         const std::filesystem::path& animations,
                                         std::size_t limit) const;
    Result<nlohmann::json> symbolUsage(const std::filesystem::path& path, std::size_t limit) const;

private:
    struct ClipSnapshot;
    struct IndexSnapshot;

    Result<std::shared_ptr<const IndexSnapshot>> indexSnapshot(const AnimationIndexQuery& request) const;

    Result<std::shared_ptr<const ClipSnapshot>> loadedClip(const std::filesystem::path& skeletonPath,
                                                           const std::filesystem::path& animationPath) const;

    mutable std::mutex cacheMutex_;
    mutable std::shared_ptr<const ClipSnapshot> cache_;
    mutable std::mutex indexMutex_;
    mutable std::shared_ptr<const IndexSnapshot> indexCache_;
};

Result<std::vector<std::byte>> readHkxBytes(const std::filesystem::path& path);
Result<void> checkSaveTarget(const std::filesystem::path& target, std::string_view what);
Result<void> publishHkxBytes(const std::filesystem::path& target, const std::vector<std::byte>& bytes, bool overwrite,
                             std::string_view failure);

}
