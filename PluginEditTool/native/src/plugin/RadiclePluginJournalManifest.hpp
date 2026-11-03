#pragma once

#include "RadiclePluginJournalDestination.hpp"
#include "RadiclePluginJournalReadStrings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::detail {

class Ba2ReadRoot;

inline constexpr std::uint64_t kJournalMaxFileBytes = 512ull * 1024 * 1024;
inline constexpr std::uint64_t kJournalMaxTotalBytes = 2ull * 1024 * 1024 * 1024;
inline constexpr std::size_t kJournalMaxManifestBytes = 1024 * 1024;
inline constexpr std::uint64_t kJournalMaxArchiveBytes = 64ull * 1024 * 1024 * 1024;
inline constexpr std::size_t kJournalMaxArchiveProviders = 128;

struct JournalManifestEntry {
    std::string kind;
    std::string name;
    std::string payload;
    bool originalExists{false};
    std::string originalHash;
    std::uint64_t originalSize{0};
    std::string finalHash;
    std::uint64_t finalSize{0};
    std::filesystem::path rootSelector;
    std::vector<std::string> directories;
    bool verifyOnly{false};
};

struct JournalSourceSet {
    std::string kind;
    std::filesystem::path rootSelector;
    std::vector<std::string> directories;
    std::vector<std::string> names;
    bool exists{true};
};

struct JournalArchiveProvider {
    std::string filename;
    std::uint64_t size{0};
    std::string sha256;
};

struct JournalArchiveInventory {
    std::filesystem::path rootSelector;
    std::vector<std::string> directories;
    std::vector<JournalArchiveProvider> providers;
};

struct JournalArchiveReference {
    std::filesystem::path rootSelector;
    std::vector<std::string> directories;
    std::string filename;
};

struct JournalManifest {
    std::uint32_t version{1};
    std::string transaction;
    std::filesystem::path plugin;
    std::vector<JournalManifestEntry> files;
    std::vector<JournalSourceSet> sources;
    std::vector<JournalArchiveInventory> archives;
    std::vector<JournalArchiveReference> archiveOrder;
    std::vector<JournalReadStringInventory> readStrings;
};

struct JournalApprovedRoot {
    std::filesystem::path path;
    JournalDirectoryIdentity identity;
};

struct JournalApprovedReadRoot {
    std::filesystem::path path;
    std::shared_ptr<const Ba2ReadRoot> lease;
};

struct JournalRoots {
    std::filesystem::path plugin;
    std::vector<JournalApprovedRoot> approvedRoots;
    std::vector<JournalApprovedReadRoot> approvedReadRoots;
    std::vector<JournalArchiveReference> archiveDirectories;
    std::vector<JournalArchiveReference> archiveProviderOrder;
    std::vector<JournalArchiveReference> readStringDirectories;
    std::optional<std::filesystem::path> stringsDirectory;
    std::optional<std::filesystem::path> scriptSourceDirectory;
    std::optional<std::filesystem::path> scriptCompiledDirectory;
    std::optional<JournalDirectoryIdentity> pluginDirectoryIdentity;
    std::optional<JournalDirectoryIdentity> stringsDirectoryIdentity;
    std::optional<JournalDirectoryIdentity> scriptSourceDirectoryIdentity;
    std::optional<JournalDirectoryIdentity> scriptCompiledDirectoryIdentity;
};

const JournalApprovedRoot& approvedJournalRoot(const JournalManifestEntry& entry, const JournalRoots& roots);
bool isJournalStringName(std::string_view filename, const std::filesystem::path& plugin);
JournalManifest decodeJournalManifest(std::string_view text);
std::vector<std::filesystem::path> resolveJournalTargets(const JournalManifest& manifest,
                                                        const JournalRoots& roots);

}
