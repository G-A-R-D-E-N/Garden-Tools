#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fo4ide/assets/MaterialJson.hpp>

namespace fo4::archive {
class AssetResolver;
}

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

// Thread-safe snapshot of cumulative totals since construction; diff two snapshots for a window.
struct TextureResolverStats {
    std::uint64_t indexNs{0};
    std::uint64_t archiveOpens{0};
    std::uint64_t indexedEntries{0};
    std::uint64_t indexLockWaitNs{0};
    std::uint64_t indexLookupNs{0};
    std::uint64_t lookups{0};
    std::uint64_t memoHits{0};
    std::uint64_t memoMisses{0};
    std::uint64_t resolveNs{0};
    std::uint64_t extractCount{0};
    std::uint64_t extractCacheHits{0};
    std::uint64_t extractNs{0};
    std::uint64_t extractKeyWaitNs{0};
    std::uint64_t extractInflateNs{0};
    std::uint64_t extractWriteNs{0};
    std::uint64_t textureCalls{0};
    std::uint64_t decodeNs{0};
    std::uint64_t pngWriteNs{0};
    std::uint64_t base64Ns{0};
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
// Each BA2 is parsed once when indexed and its reader is held; the index is read under a shared lock and extraction runs outside it.
class TextureResolver {
public:
    TextureResolver();
    explicit TextureResolver(std::filesystem::path cacheDir);

    void setSessionRoots(std::vector<std::filesystem::path> roots);

    ArchiveIndexProgress archiveIndexProgress() const noexcept;
    TextureResolverStats stats() const noexcept;
    // Test hook: called before each archive entry is decompressed, outside the index lock.
    void setExtractObserverForTesting(std::function<void(std::string_view)> observer);
    void setScanObserverForTesting(std::function<void()> observer);
    // The BGSM parse memo is thread-safe and shared by every material lookup through this resolver.
    BgsmMemo& bgsmMemo() noexcept;

    // Mirrors TextureService.GetTexturePngDataUrl: the PNG data URL, or ""
    // when the texture cannot be resolved or decoded.
    std::string getTexturePngDataUrl(std::string_view nifPath,
                                     std::string_view relTexPath,
                                     std::string_view textureRoot,
                                     bool reconstructZ = false);
    // Mirrors TextureService.GetTexturePngPath: the cached PNG file, if any.
    std::optional<std::filesystem::path> getTexturePngPath(
        std::string_view nifPath,
        std::string_view relTexPath,
        std::string_view textureRoot,
        bool reconstructZ = false);
    // Mirrors TextureService.ResolveNif: a loose NIF or an entry extracted
    // from a BA2 under the configured roots.
    std::optional<std::filesystem::path> resolveNif(std::string_view relPath);
    std::optional<std::filesystem::path> resolveTextureFile(std::string_view nifPath,
                                                            std::string_view rel,
                                                            std::string_view textureRoot);
    std::optional<std::filesystem::path> resolveMaterialPath(std::string_view nifPath,
                                                             std::string_view rel,
                                                             std::string_view textureRoot);
    std::optional<std::filesystem::path> resolveMaterialFile(std::string_view nifPath,
                                                             std::string_view rel,
                                                             std::string_view textureRoot);
    std::shared_ptr<const fo4::archive::AssetResolver> assetResolver();
    const std::filesystem::path& cacheDir() const noexcept { return cacheDir_; }

private:
    struct ArchiveRecord;
    struct IndexSlot {
        std::uint32_t archive{0};
        std::size_t entry{0};
    };
    struct IndexHit {
        std::shared_ptr<const ArchiveRecord> archive;
        std::size_t entry{0};
        std::size_t keyIndex{0};
    };
    struct Counters {
        std::atomic<std::uint64_t> indexNs{0};
        std::atomic<std::uint64_t> archiveOpens{0};
        std::atomic<std::uint64_t> indexedEntries{0};
        std::atomic<std::uint64_t> indexLockWaitNs{0};
        std::atomic<std::uint64_t> indexLookupNs{0};
        std::atomic<std::uint64_t> lookups{0};
        std::atomic<std::uint64_t> memoHits{0};
        std::atomic<std::uint64_t> memoMisses{0};
        std::atomic<std::uint64_t> resolveNs{0};
        std::atomic<std::uint64_t> extractCount{0};
        std::atomic<std::uint64_t> extractCacheHits{0};
        std::atomic<std::uint64_t> extractNs{0};
        std::atomic<std::uint64_t> extractKeyWaitNs{0};
        std::atomic<std::uint64_t> extractInflateNs{0};
        std::atomic<std::uint64_t> extractWriteNs{0};
        std::atomic<std::uint64_t> textureCalls{0};
        std::atomic<std::uint64_t> decodeNs{0};
        std::atomic<std::uint64_t> pngWriteNs{0};
        std::atomic<std::uint64_t> base64Ns{0};
    };

    std::optional<std::filesystem::path> resolveDds(std::string_view nifPath,
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
        const std::optional<std::filesystem::path>& shallowRoot,
        std::uint64_t epoch);
    bool rootsScanned(const std::vector<std::filesystem::path>& roots,
                      const std::optional<std::filesystem::path>& shallowRoot) const;
    void scanRoots(const std::vector<std::filesystem::path>& roots,
                   const std::optional<std::filesystem::path>& shallowRoot,
                   std::uint64_t epoch);
    bool ensureRootScanned(const std::filesystem::path& root, bool recursive = true);
    std::optional<IndexHit> lookupIndex(const std::vector<std::pair<std::filesystem::path, bool>>& requested,
                                        const std::vector<std::string>& keys);
    bool refreshDue(const std::vector<std::filesystem::path>& roots,
                    const std::optional<std::filesystem::path>& shallowRoot) const;
    void refreshRoots(const std::vector<std::filesystem::path>& roots,
                      const std::optional<std::filesystem::path>& shallowRoot,
                      std::uint64_t epoch);
    std::optional<std::filesystem::path> extractFromArchive(const ArchiveRecord& archive,
                                                            std::size_t entryIndex,
                                                            std::string_view innerKey);
    std::optional<std::filesystem::path> convertToPng(const std::filesystem::path& ddsPath,
                                                      bool reconstructZ);
    std::optional<std::filesystem::path> decodeInProcess(
        const std::vector<std::byte>& ddsBytes,
        const std::filesystem::path& pngPath,
        std::string_view key,
        bool reconstructZ);
    std::optional<std::string> resolveMaterialDiffuse(
        std::string_view nifPath,
        std::string_view bgsmRelPath,
        std::string_view textureRoot);
    struct MemoEntry {
        std::optional<std::filesystem::path> value;
        std::chrono::steady_clock::time_point at;
    };
    template <typename Compute>
    std::optional<std::filesystem::path> memo(std::string key, Compute&& compute);
    std::vector<std::filesystem::path> globalRoots(std::uint64_t* epoch = nullptr) const;
    std::shared_lock<std::shared_mutex> readLock() const;

    std::vector<std::filesystem::path> addDataRoots(const std::filesystem::path& path);
    static std::vector<std::string> lookupKeys(std::string_view rel,
                                               std::string_view topFolder);
    static std::string norm(std::string_view path);
    static std::string memoKey(std::string_view nifPath,
                               std::string_view rel,
                               std::string_view textureRoot);
    static std::string sha1Hex16(std::string_view text);
    static std::uint64_t fileStamp(const std::filesystem::path& path) noexcept;
    static std::uint64_t fileSize(const std::filesystem::path& path) noexcept;

    std::mutex& lockFor(const std::string& key);

    std::filesystem::path cacheDir_;
    std::vector<std::filesystem::path> sessionRoots_;  // guarded by the index lock
    std::uint64_t sessionEpoch_{0};

    struct RootArchive {
        std::uint64_t stamp{0};
        std::uint64_t size{0};
        std::uint32_t id{0};
    };
    struct RootIndex {
        std::map<std::string, IndexSlot> entries;  // lowercased entry path -> archive id and entry index
        std::map<std::string, RootArchive> archives;  // lowercased archive path -> stamp, size and archive id
    };
    struct RootState {
        std::uint64_t stamp{0};
        std::chrono::steady_clock::time_point scannedAt;
    };

    mutable std::shared_mutex indexMutex_;
    std::map<std::pair<std::string, bool>, RootIndex> rootIndex_;  // trimmed root path and recursive flag -> its index; written under the exclusive lock
    std::vector<std::shared_ptr<const ArchiveRecord>> archives_;  // archive id -> held reader, null once replaced; read under the shared lock
    std::map<std::pair<std::filesystem::path, bool>, RootState> scannedRoots_;  // root and recursive flag

    std::atomic<ArchiveIndexPhase> indexPhase_{ArchiveIndexPhase::idle};
    std::atomic<std::size_t> archivesDone_{0};
    std::atomic<std::size_t> archivesTotal_{0};

    // Invariant: a negative expires with the root refresh interval, and the memo is cleared when the roots or their archives change.
    mutable std::mutex memoMutex_;
    std::uint64_t memoGeneration_{0};
    std::unordered_map<std::string, MemoEntry> pathMemo_;

    // Guarded by keyLocksMutex_; entries created on demand.
    mutable std::mutex keyLocksMutex_;
    std::map<std::string, std::shared_ptr<std::mutex>> keyLocks_;

    BgsmMemo materials_;
    std::function<void(std::string_view)> extractObserver_;
    std::function<void()> scanObserver_;
    mutable Counters counters_;
};

}  // namespace fo4ide::assets
