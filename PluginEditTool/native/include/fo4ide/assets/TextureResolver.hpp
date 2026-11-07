#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::assets {

enum class ArchiveIndexPhase : std::uint8_t {
    idle,      // no archive scan has run for the current session roots
    indexing,  // BA2 scan in progress (archive enumeration or per-archive reads)
    ready,     // the last scan finished; lookups no longer wait on the index build
};

struct ArchiveIndexProgress {
    ArchiveIndexPhase phase{ArchiveIndexPhase::idle};
    std::size_t done{0};
    std::size_t total{0};
};

// Port of the C# TextureService: resolves a texture reference (relative path,
// material ref, or NIF-relative) to a PNG — loose files first, then BA2
// archives under the configured data roots — decoding DDS in-process with the
// fo4ide_textures codecs instead of texconv. The index/scanned-root cache and
// per-key write locks mirror the C# static state design, so repeated lookups
// stay cheap and concurrent extraction of the same texture is serialized.
//
// Thread-safe: setSessionRoots and the resolve methods may be called from any
// thread; each cache write is atomic (temp file + rename) under its key lock.
class TextureResolver {
public:
    TextureResolver();
    explicit TextureResolver(std::filesystem::path cacheDir);

    void setSessionRoots(std::vector<std::filesystem::path> roots);

    ArchiveIndexProgress archiveIndexProgress() const noexcept;

    // Mirrors TextureService.GetTexturePngDataUrl: the PNG data URL, or ""
    // when the texture cannot be resolved or decoded.
    std::string getTexturePngDataUrl(std::string_view nifPath,
                                     std::string_view relTexPath,
                                     std::string_view textureRoot);
    // Mirrors TextureService.GetTexturePngPath: the cached PNG file, if any.
    std::optional<std::filesystem::path> getTexturePngPath(
        std::string_view nifPath,
        std::string_view relTexPath,
        std::string_view textureRoot);
    // Mirrors TextureService.ResolveNif: a loose NIF or an entry extracted
    // from a BA2 under the configured roots.
    std::optional<std::filesystem::path> resolveNif(std::string_view relPath);
    std::optional<std::filesystem::path> resolveMaterialPath(std::string_view nifPath,
                                                             std::string_view rel,
                                                             std::string_view textureRoot);

private:
    std::optional<std::filesystem::path> resolveDds(std::string_view nifPath,
                                                    std::string_view rel,
                                                    std::string_view textureRoot);
    std::optional<std::filesystem::path> resolveMaterialFile(std::string_view nifPath,
                                                             std::string_view rel,
                                                             std::string_view textureRoot);
    std::optional<std::filesystem::path> resolveFromArchives(
        std::string_view rel,
        std::string_view nifDir,
        std::string_view textureRoot,
        std::string_view topFolder);
    std::optional<std::filesystem::path> scanAndLookup(
        const std::vector<std::filesystem::path>& roots,
        const std::vector<std::string>& keys,
        const std::optional<std::filesystem::path>& shallowRoot = std::nullopt);
    void ensureRootScanned(const std::filesystem::path& root, bool recursive = true);
    std::optional<std::filesystem::path> extractFromArchive(
        const std::filesystem::path& archivePath,
        std::string_view innerKey);
    std::optional<std::filesystem::path> convertToPng(const std::filesystem::path& ddsPath);
    std::optional<std::filesystem::path> decodeInProcess(
        const std::filesystem::path& ddsPath,
        const std::filesystem::path& pngPath,
        std::string_view key);
    std::optional<std::string> resolveMaterialDiffuse(
        std::string_view nifPath,
        std::string_view bgsmRelPath,
        std::string_view textureRoot);
    std::vector<std::filesystem::path> globalRoots() const;

    std::vector<std::filesystem::path> addDataRoots(const std::filesystem::path& path);
    static std::vector<std::string> lookupKeys(std::string_view rel,
                                               std::string_view topFolder);
    static std::string norm(std::string_view path);
    static std::string sha1Hex16(std::string_view text);
    static std::uint64_t fileStamp(const std::filesystem::path& path) noexcept;

    std::mutex& lockFor(const std::string& key);

    std::filesystem::path cacheDir_;
    std::vector<std::filesystem::path> sessionRoots_;
    mutable std::vector<std::filesystem::path> globalRootsCache_;
    mutable bool globalRootsLoaded_{false};

    mutable std::mutex indexMutex_;
    std::map<std::string, std::uint32_t> index_;    // key -> archive id
    std::vector<std::string> archivePaths_;         // archive id -> archive path, in scan order
    std::set<std::string> scannedArchives_;         // lowercased archive paths
    std::set<std::filesystem::path> scannedRoots_;
    std::set<std::filesystem::path> shallowScannedRoots_;

    std::atomic<ArchiveIndexPhase> indexPhase_{ArchiveIndexPhase::idle};
    std::atomic<std::size_t> archivesDone_{0};
    std::atomic<std::size_t> archivesTotal_{0};

    // Guarded by keyLocksMutex_; entries created on demand.
    mutable std::mutex keyLocksMutex_;
    std::map<std::string, std::shared_ptr<std::mutex>> keyLocks_;
};

}
