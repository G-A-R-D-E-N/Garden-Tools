#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RadiclePluginPrevisPrep.hpp"

namespace fo4ide::plugin::previs_assets {

// Constraint: a configured write root must win over the patch plugin's own folder, and Data is the last resort.
std::filesystem::path resolveOutputRoot(const std::filesystem::path& dataDirectory,
                                        const std::filesystem::path& configuredRoot,
                                        const std::filesystem::path& pluginPath);

// Constraint: vanilla STAT meshes live inside the general archives, so models must be read through the Data reader.
previs_prep::ReadModelBytes archiveModelReader(const std::filesystem::path& dataDirectory);

// Constraint: retail cluster tomes live under Vis in general archives as often as on disk.
std::vector<std::filesystem::path> previsAssetRoots(
    const std::filesystem::path& dataDirectory,
    std::span<const std::filesystem::path> mo2AssetRootsLowToHigh,
    const std::filesystem::path& previsOutputRoot);

std::optional<std::vector<std::byte>> readVisTome(const std::filesystem::path& dataDirectory,
                                   std::string_view pluginFolder,
                                   std::uint32_t cellLow24,
                                   std::span<const std::string> pluginLoadOrder,
                                   std::span<const std::filesystem::path> mo2AssetRootsLowToHigh,
                                   const std::filesystem::path& previsOutputRoot = {});

// Constraint: a shipped _OC lives inside a general archive, so a shell is a Data-relative path and its bytes, never a filename the caller had to type.
struct OcShell {
  std::string path;
  std::vector<std::byte> bytes;
};

// Constraint: an empty request resolves the cell's own templates from the loose tree and the archives, so generate needs no manual template path.
// Constraint: a non-empty ownerFolders keeps only shells under those plugin folders, best first, since the cell prefix alone does not name a plugin.
std::vector<OcShell> ocShells(const std::filesystem::path& dataDirectory,
                             std::string_view requested,
                             std::string_view cellPrefix,
                             const std::vector<std::string>& ownerFolders = {});

struct MaterialRun {
  std::string material;
  std::vector<std::size_t> groupIndices;
};

// Constraint: the CK ships one _OC per shader material, so a material run of the ordered plan must be one file.
std::vector<MaterialRun> materialRuns(const previs_prep::Prepared* prepared, std::size_t groupCount);

}  // namespace fo4ide::plugin::previs_assets
