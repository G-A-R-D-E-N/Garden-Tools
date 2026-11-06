#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/papyrus/SecureSourceSnapshot.hpp>

namespace fo4ide::papyrus {

class ScriptIndex;

inline constexpr std::string_view kVanillaImportFolders[] = {
    "Base", "User", "CreationClub", "DLC01", "DLC02", "DLC03", "DLC04", "DLC05", "DLC06"};

// Entry names must stay below the root; zip64 archives are refused.
std::vector<SnapshotSource> readPscZip(const std::filesystem::path& zipPath);

// Results must be sorted by relative path; a missing root yields an empty list.
std::vector<SnapshotSource> readPscDirectory(const std::filesystem::path& root);

struct VanillaImportRoot {
    std::filesystem::path root;
    std::vector<SnapshotSource> sources;
};

// Extracted .psc files win over the zip, and a folder must hold at most one zip.
std::optional<VanillaImportRoot> loadVanillaImportRoot(const std::filesystem::path& dataDir,
                                                       std::string_view folder);

// Must run before compileScript so vanilla types resolve without extraction.
std::size_t addVanillaImports(ScriptIndex& index, const std::filesystem::path& dataDir,
                              std::span<const std::string_view> folders);

}  // namespace fo4ide::papyrus
