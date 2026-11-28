#include <fo4ide/assets/TextureResolver.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <fo4/archive/assets.hpp>
#include <fo4/archive/ba2_reader.hpp>
#include <fo4ide/assets/AssetPath.hpp>
#include <fo4ide/core/Base64.hpp>
#include <fo4ide/core/StageTimer.hpp>
#include <fo4ide/material/MaterialService.hpp>
#include <fo4ide/textures/DdsDecoder.hpp>

namespace fo4ide::assets {
namespace {

constexpr auto kRootRefreshInterval = std::chrono::milliseconds(500);
constexpr std::uint32_t kNoArchive = 0xFFFFFFFFu;

std::string lower(std::string_view value) {
    std::string out(value);
    for (auto& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

bool startsWithInsensitive(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin(),
                      [](unsigned char x, unsigned char y) {
                          return std::tolower(x) == std::tolower(y);
                      });
}

bool endsWithInsensitive(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](unsigned char x, unsigned char y) {
                          return std::tolower(x) == std::tolower(y);
                      });
}

std::string trimmed(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string trimmedQuotes(std::string_view value) {
    auto out = trimmed(value);
    while (!out.empty() && out.front() == '"') out.erase(out.begin());
    while (!out.empty() && out.back() == '"') out.pop_back();
    return out;
}

std::filesystem::path trimmedRootKey(const std::filesystem::path& root) {
    return std::filesystem::path(trimmed(root.string()));
}

bool hasUncRoot(std::string_view view) {
    std::string path(view);
    for (auto& ch : path) {
        if (ch == '/') ch = '\\';
    }
    if (path.starts_with("\\\\?\\")) return true;
    if (!path.starts_with("\\\\")) return false;
    const auto host = path.find('\\', 2);
    if (host == std::string::npos || host == 2) return false;
    const auto share = path.find('\\', host + 1);
    return share != std::string::npos && share + 1 < path.size();
}

std::filesystem::path caseInsensitivePath(std::filesystem::path path) {
    auto text = path.generic_string();
    for (auto& ch : text) {
        if (ch == '\\') ch = '/';
    }
    path = std::filesystem::path(text);
    if (std::filesystem::exists(path)) return path;
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    auto result = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        const auto name = component.string();
        const auto direct = result / name;
        if (std::filesystem::exists(direct)) {
            result = direct;
            continue;
        }
        std::error_code ec;
        bool matched = false;
        std::filesystem::directory_iterator it(result, ec);
        const std::filesystem::directory_iterator end;
        for (; it != end && !ec; it.increment(ec)) {
            if (iequals(it->path().filename().string(), name)) {
                result = it->path();
                matched = true;
                break;
            }
        }
        if (!matched) return {};
    }
    return result;
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const auto end = stream.tellg();
    if (end < 0) return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream) return {};
    }
    return bytes;
}

std::vector<std::filesystem::path> archivesUnder(const std::filesystem::path& root, bool recursive) {
    std::error_code ec;
    std::vector<std::filesystem::path> archives;
    const auto collect = [&](auto it) {
        const decltype(it) end;
        while (it != end && !ec) {
            if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".ba2") {
                archives.push_back(it->path());
            }
            it.increment(ec);
        }
    };
    try {
        if (recursive) {
            collect(std::filesystem::recursive_directory_iterator(root, ec));
        } else {
            collect(std::filesystem::directory_iterator(root, ec));
        }
    } catch (const std::exception&) {
        return {};
    }
    std::sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) {
        return lower(a.generic_string()) < lower(b.generic_string());
    });
    return archives;
}

void writeTempThenRename(const std::filesystem::path& destination,
                         std::span<const std::byte> bytes) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temp = destination;
    temp += ".tmp";
    temp += std::to_string(ticks);
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("could not open cache temp file: " + temp.string());
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream) throw std::runtime_error("could not write cache temp file: " + temp.string());
    }
    std::error_code ec;
    std::filesystem::rename(temp, destination, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("could not move cache file into place: " + destination.string());
    }
}

struct IndexingPhaseGuard {
    std::atomic<ArchiveIndexPhase>& phase;
    ~IndexingPhaseGuard() {
        if (phase.load(std::memory_order_relaxed) == ArchiveIndexPhase::indexing) {
            phase.store(ArchiveIndexPhase::ready, std::memory_order_release);
        }
    }
};

}  // namespace

struct TextureResolver::ArchiveRecord {
    ArchiveRecord(std::filesystem::path archivePath, std::uint64_t timestamp, fo4::archive::Ba2Reader source)
        : path(std::move(archivePath)), stamp(timestamp), reader(std::move(source)) {}

    std::filesystem::path path;
    std::uint64_t stamp;
    fo4::archive::Ba2Reader reader;
};

TextureResolver::TextureResolver()
    : TextureResolver(std::filesystem::temp_directory_path() / "FO4RE_Tex") {}

TextureResolver::TextureResolver(std::filesystem::path cacheDir)
    : cacheDir_(std::move(cacheDir)) {}

void TextureResolver::setSessionRoots(std::vector<std::filesystem::path> roots) {
    std::unique_lock<std::shared_mutex> guard(indexMutex_);
    {
        std::lock_guard<std::mutex> memoGuard(memoMutex_);
        ++memoGeneration_;
        pathMemo_.clear();
    }
    ++sessionEpoch_;
    sessionRoots_ = std::move(roots);
    rootIndex_.clear();
    archives_.clear();
    scannedRoots_.clear();
    archivesDone_.store(0, std::memory_order_relaxed);
    archivesTotal_.store(0, std::memory_order_relaxed);
    indexPhase_.store(ArchiveIndexPhase::idle, std::memory_order_release);
}

ArchiveIndexProgress TextureResolver::archiveIndexProgress() const noexcept {
    const auto phase = indexPhase_.load(std::memory_order_acquire);
    const auto done = archivesDone_.load(std::memory_order_acquire);
    const auto total = archivesTotal_.load(std::memory_order_acquire);
    return {phase, done, total};
}

TextureResolverStats TextureResolver::stats() const noexcept {
    const auto load = [](const std::atomic<std::uint64_t>& value) {
        return value.load(std::memory_order_relaxed);
    };
    TextureResolverStats out;
    out.indexNs = load(counters_.indexNs);
    out.archiveOpens = load(counters_.archiveOpens);
    out.indexedEntries = load(counters_.indexedEntries);
    out.indexLockWaitNs = load(counters_.indexLockWaitNs);
    out.indexLookupNs = load(counters_.indexLookupNs);
    out.lookups = load(counters_.lookups);
    out.memoHits = load(counters_.memoHits);
    out.memoMisses = load(counters_.memoMisses);
    out.resolveNs = load(counters_.resolveNs);
    out.extractCount = load(counters_.extractCount);
    out.extractCacheHits = load(counters_.extractCacheHits);
    out.extractNs = load(counters_.extractNs);
    out.extractKeyWaitNs = load(counters_.extractKeyWaitNs);
    out.extractInflateNs = load(counters_.extractInflateNs);
    out.extractWriteNs = load(counters_.extractWriteNs);
    out.textureCalls = load(counters_.textureCalls);
    out.decodeNs = load(counters_.decodeNs);
    out.pngWriteNs = load(counters_.pngWriteNs);
    out.base64Ns = load(counters_.base64Ns);
    return out;
}

void TextureResolver::setExtractObserverForTesting(std::function<void(std::string_view)> observer) {
    extractObserver_ = std::move(observer);
}

void TextureResolver::setScanObserverForTesting(std::function<void()> observer) {
    scanObserver_ = std::move(observer);
}

BgsmMemo& TextureResolver::bgsmMemo() noexcept {
    return materials_;
}

std::mutex& TextureResolver::lockFor(const std::string& key) {
    std::lock_guard<std::mutex> guard(keyLocksMutex_);
    auto& lock = keyLocks_[key];
    if (!lock) lock = std::make_shared<std::mutex>();
    return *lock;
}

std::shared_lock<std::shared_mutex> TextureResolver::readLock() const {
    const auto start = core::StageClock::now();
    std::shared_lock<std::shared_mutex> lock(indexMutex_);
    counters_.indexLockWaitNs += core::elapsedNs(start);
    return lock;
}

std::string TextureResolver::norm(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (const unsigned char ch : path) {
        out.push_back(ch == '/' ? '\\' : static_cast<char>(ch));
    }
    std::size_t begin = 0;
    while (begin < out.size() && out[begin] == '\\') ++begin;
    out.erase(0, begin);
    return out;
}

std::string TextureResolver::memoKey(std::string_view nifPath,
                                     std::string_view rel,
                                     std::string_view textureRoot) {
    std::string dir;
    if (!nifPath.empty()) {
        dir = std::filesystem::absolute(nifPath).lexically_normal().parent_path().string();
    }
    return lower(norm(dir)) + "|" + lower(norm(rel)) + "|" + lower(norm(textureRoot));
}

std::uint64_t TextureResolver::fileStamp(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
}

std::uint64_t TextureResolver::fileSize(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return 0;
    return static_cast<std::uint64_t>(size);
}

std::vector<std::filesystem::path> TextureResolver::addDataRoots(
    const std::filesystem::path& path) {
    std::vector<std::filesystem::path> roots;
    if (path.empty()) return roots;
    if (startsWithInsensitive(path.generic_string(), cacheDir_.generic_string())) return roots;

    auto dir = path;
    for (int guard = 0; guard < 24; ++guard) {
        roots.push_back(dir);
        if (iequals(dir.filename().string(), "Data")) return roots;
        const auto parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    return {};
}

std::vector<std::string> TextureResolver::lookupKeys(std::string_view rel,
                                                     std::string_view topFolder) {
    const auto normalized = norm(rel);
    std::vector<std::string> keys{lower(normalized)};
    const std::string prefix = std::string(topFolder) + "\\";
    if (!startsWithInsensitive(normalized, prefix)) {
        keys.push_back(lower(prefix + normalized));
    }
    return keys;
}

std::vector<std::filesystem::path> TextureResolver::globalRoots(std::uint64_t* epoch) const {
    const auto lock = readLock();
    if (epoch) *epoch = sessionEpoch_;
    return sessionRoots_;
}

std::shared_ptr<const fo4::archive::AssetResolver> TextureResolver::assetResolver() {
    const auto roots = globalRoots();
    std::vector<std::filesystem::path> archives;
    for (auto root = roots.rbegin(); root != roots.rend(); ++root) {
        if (root->empty()) continue;
        const auto found = archivesUnder(*root, true);
        archives.insert(archives.end(), found.begin(), found.end());
    }
    std::vector<std::filesystem::path> readable;
    for (const auto& path : archives) {
        try {
            fo4::archive::Ba2Reader::open(path);
            readable.push_back(path);
        } catch (const std::exception&) {
        }
    }
    return std::make_shared<fo4::archive::AssetResolver>(fo4::archive::AssetResolver::open_roots(roots, readable));
}

// Requires the exclusive index lock; returns whether the index gained, replaced or lost an archive.
bool TextureResolver::ensureRootScanned(const std::filesystem::path& root, bool recursive) {
    const auto trimmedRoot = trimmedRootKey(root);
    if (trimmedRoot.empty()) return false;
    const auto scanKey = std::pair{trimmedRoot, recursive};
    if (scannedRoots_.contains(scanKey)) return false;
    scannedRoots_[scanKey] = {fileStamp(trimmedRoot), std::chrono::steady_clock::now()};
    auto& scanned = rootIndex_[{trimmedRoot.string(), recursive}];
    const bool isDirectory = std::filesystem::is_directory(trimmedRoot);
    if (!isDirectory && scanned.archives.empty()) return false;
    indexPhase_.store(ArchiveIndexPhase::indexing, std::memory_order_release);

    std::vector<std::filesystem::path> found;
    if (isDirectory) found = archivesUnder(trimmedRoot, recursive);
    std::map<std::string, std::filesystem::path> present;
    for (const auto& archive : found) present.emplace(lower(archive.string()), archive);

    bool dropped = false;
    for (const auto& [archiveKey, known] : scanned.archives) {
        const auto current = present.find(archiveKey);
        if (current == present.end() || known.stamp != fileStamp(current->second) ||
            known.size != fileSize(current->second)) {
            dropped = true;
            break;
        }
    }
    if (dropped) {
        for (const auto& [archiveKey, known] : scanned.archives) {
            if (known.id < archives_.size()) archives_[known.id].reset();
        }
        scanned.entries.clear();
        scanned.archives.clear();
    }

    std::vector<std::filesystem::path> fresh;
    for (const auto& archive : found) {
        if (!scanned.archives.contains(lower(archive.string()))) fresh.push_back(archive);
    }
    archivesTotal_.fetch_add(fresh.size(), std::memory_order_release);

    for (const auto& archive : fresh) {
        const auto archiveKey = lower(archive.string());
        const auto stamp = fileStamp(archive);
        counters_.archiveOpens++;
        RootArchive indexed{stamp, fileSize(archive), kNoArchive};
        try {
            auto reader = fo4::archive::Ba2Reader::open(archive);
            if (!reader.entries().empty() && !reader.has_name_table()) {
                throw std::runtime_error("BA2 has no name table: " + archive.string());
            }
            const auto id = static_cast<std::uint32_t>(archives_.size());
            auto record = std::make_shared<const ArchiveRecord>(archive, stamp, std::move(reader));
            std::uint64_t added = 0;
            const auto& entries = record->reader.entries();
            for (std::size_t entry = 0; entry < entries.size(); ++entry) {
                const auto& entryPath = entries[entry].path;
                if (!endsWithInsensitive(entryPath, ".dds") &&
                    !endsWithInsensitive(entryPath, ".nif") &&
                    !endsWithInsensitive(entryPath, ".bgsm")) {
                    continue;
                }
                const auto [it, inserted] =
                    scanned.entries.try_emplace(lower(norm(entryPath)), IndexSlot{id, entry});
                if (inserted) {
                    ++added;
                } else if (archiveKey > lower(archives_[it->second.archive]->path.string())) {
                    it->second = IndexSlot{id, entry};
                    ++added;
                }
            }
            archives_.push_back(std::move(record));
            indexed.id = id;
            counters_.indexedEntries += added;
        } catch (const std::exception&) {
        }
        scanned.archives.emplace(archiveKey, indexed);
        archivesDone_.fetch_add(1, std::memory_order_release);
    }

    const bool changed = dropped || !fresh.empty();
    if (changed) {
        std::lock_guard<std::mutex> memoGuard(memoMutex_);
        ++memoGeneration_;
        pathMemo_.clear();
    }
    return changed;
}

bool TextureResolver::rootsScanned(const std::vector<std::filesystem::path>& roots,
                                   const std::optional<std::filesystem::path>& shallowRoot) const {
    const auto lock = readLock();
    for (const auto& root : roots) {
        const auto key = trimmedRootKey(root);
        if (!key.empty() && !scannedRoots_.contains({key, true})) return false;
    }
    if (shallowRoot) {
        const auto key = trimmedRootKey(*shallowRoot);
        if (!key.empty() && !scannedRoots_.contains({key, false})) return false;
    }
    return true;
}

void TextureResolver::scanRoots(const std::vector<std::filesystem::path>& roots,
                                const std::optional<std::filesystem::path>& shallowRoot,
                                std::uint64_t epoch) {
    const auto start = core::StageClock::now();
    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    counters_.indexLockWaitNs += core::elapsedNs(start);
    if (epoch != sessionEpoch_) return;
    core::ScopedNs timed(counters_.indexNs);
    IndexingPhaseGuard phaseGuard{indexPhase_};
    for (const auto& root : roots) ensureRootScanned(root);
    if (shallowRoot) ensureRootScanned(*shallowRoot, false);
}

bool TextureResolver::refreshDue(const std::vector<std::filesystem::path>& roots,
                                 const std::optional<std::filesystem::path>& shallowRoot) const {
    const auto lock = readLock();
    const auto now = std::chrono::steady_clock::now();
    const auto due = [&](const std::filesystem::path& root, bool recursive) {
        const auto key = trimmedRootKey(root);
        if (key.empty()) return false;
        const auto scanned = scannedRoots_.find({key, recursive});
        if (scanned == scannedRoots_.end()) return false;
        return scanned->second.stamp != fileStamp(key) || now - scanned->second.scannedAt >= kRootRefreshInterval;
    };
    for (const auto& root : roots) {
        if (due(root, true)) return true;
    }
    return shallowRoot && due(*shallowRoot, false);
}

void TextureResolver::refreshRoots(const std::vector<std::filesystem::path>& roots,
                                   const std::optional<std::filesystem::path>& shallowRoot,
                                   std::uint64_t epoch) {
    const auto start = core::StageClock::now();
    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    counters_.indexLockWaitNs += core::elapsedNs(start);
    if (epoch != sessionEpoch_) return;
    core::ScopedNs timed(counters_.indexNs);
    IndexingPhaseGuard phaseGuard{indexPhase_};
    const auto now = std::chrono::steady_clock::now();
    const auto refresh = [&](const std::filesystem::path& root, bool recursive) {
        const auto key = trimmedRootKey(root);
        if (key.empty()) return;
        const auto scanned = scannedRoots_.find({key, recursive});
        if (scanned == scannedRoots_.end()) return;
        if (scanned->second.stamp == fileStamp(key) && now - scanned->second.scannedAt < kRootRefreshInterval) return;
        scannedRoots_.erase(scanned);
        ensureRootScanned(key, recursive);
    };
    for (const auto& root : roots) refresh(root, true);
    if (shallowRoot) refresh(*shallowRoot, false);
}

std::optional<TextureResolver::IndexHit> TextureResolver::lookupIndex(
    const std::vector<std::pair<std::filesystem::path, bool>>& requested,
    const std::vector<std::string>& keys) {
    const auto lock = readLock();
    core::ScopedNs timed(counters_.indexLookupNs);
    counters_.lookups++;
    for (const auto& [root, recursive] : requested) {
        const auto scanned = rootIndex_.find({trimmedRootKey(root).string(), recursive});
        if (scanned == rootIndex_.end()) continue;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto it = scanned->second.entries.find(keys[i]);
            if (it != scanned->second.entries.end()) {
                return IndexHit{archives_[it->second.archive], it->second.entry, i};
            }
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> TextureResolver::scanAndLookup(
    const std::vector<std::filesystem::path>& roots,
    const std::vector<std::string>& keys,
    const std::optional<std::filesystem::path>& shallowRoot,
    std::uint64_t epoch) {
    if (scanObserver_) scanObserver_();
    if (!rootsScanned(roots, shallowRoot)) scanRoots(roots, shallowRoot, epoch);
    std::vector<std::pair<std::filesystem::path, bool>> requested;
    for (const auto& root : roots) requested.emplace_back(root, true);
    if (shallowRoot) requested.emplace_back(*shallowRoot, false);
    const auto attempt = [&]() -> std::optional<std::filesystem::path> {
        const auto hit = lookupIndex(requested, keys);
        if (!hit) return std::nullopt;
        return extractFromArchive(*hit->archive, hit->entry, keys[hit->keyIndex]);
    };
    if (auto found = attempt()) return found;
    if (!refreshDue(roots, shallowRoot)) return std::nullopt;
    refreshRoots(roots, shallowRoot, epoch);
    return attempt();
}

std::optional<std::filesystem::path> TextureResolver::extractFromArchive(
    const ArchiveRecord& archive,
    std::size_t entryIndex,
    std::string_view innerKey) {
    counters_.extractCount++;
    core::ScopedNs timed(counters_.extractNs);
    try {
        if (extractObserver_) extractObserver_(innerKey);
        std::vector<std::byte> bytes;
        {
            core::ScopedNs inflate(counters_.extractInflateNs);
            bytes = archive.reader.extract(entryIndex);
        }
        const std::string_view content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        const auto key = sha1Hex16(norm(archive.path.string()) + "|" + sha1Hex16(content) + "|" +
                                   std::string(innerKey));
        std::string extension = std::filesystem::path(innerKey).extension().string();
        if (extension.empty()) extension = ".dds";
        std::error_code ec;
        std::filesystem::create_directories(cacheDir_, ec);
        const auto outPath = cacheDir_ / ("ba2_" + key + extension);
        if (std::filesystem::exists(outPath)) {
            counters_.extractCacheHits++;
            return outPath;
        }

        const auto waitStart = core::StageClock::now();
        auto& lock = lockFor(key);
        std::lock_guard<std::mutex> guard(lock);
        counters_.extractKeyWaitNs += core::elapsedNs(waitStart);
        if (std::filesystem::exists(outPath)) {
            counters_.extractCacheHits++;
            return outPath;
        }

        core::ScopedNs write(counters_.extractWriteNs);
        writeTempThenRename(outPath, bytes);
        return outPath;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> TextureResolver::decodeInProcess(
    const std::vector<std::byte>& ddsBytes,
    const std::filesystem::path& pngPath,
    std::string_view key,
    bool reconstructZ) {
    try {
        const auto decodeStart = core::StageClock::now();
        const auto* begin = reinterpret_cast<const std::uint8_t*>(ddsBytes.data());
        const std::span<const std::uint8_t> dds(begin, ddsBytes.size());
        auto png = textures::ddsToPng(dds, reconstructZ);
        counters_.decodeNs += core::elapsedNs(decodeStart);
        if (!png) return std::nullopt;

        auto& lock = lockFor(std::string(key));
        std::lock_guard<std::mutex> guard(lock);
        if (std::filesystem::exists(pngPath)) return pngPath;

        const std::span<const std::byte> pngSpan(
            reinterpret_cast<const std::byte*>(png->data()), png->size());
        core::ScopedNs write(counters_.pngWriteNs);
        writeTempThenRename(pngPath, pngSpan);
        return pngPath;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> TextureResolver::convertToPng(
    const std::filesystem::path& ddsPath,
    bool reconstructZ) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);
    const auto ddsBytes = readBytes(ddsPath);
    if (ddsBytes.empty()) return std::nullopt;
    const std::string_view content(reinterpret_cast<const char*>(ddsBytes.data()), ddsBytes.size());
    const auto key = sha1Hex16(norm(ddsPath.string()) + "|" + sha1Hex16(content) + "|v4|" +
                               (reconstructZ ? "z1" : "z0"));
    const auto pngPath = cacheDir_ / (key + ".png");
    if (std::filesystem::exists(pngPath)) return pngPath;
    return decodeInProcess(ddsBytes, pngPath, key, reconstructZ);
}

std::optional<std::filesystem::path> TextureResolver::resolveDds(
    std::string_view nifPath,
    std::string_view rel,
    std::string_view textureRoot) {
    if (isAbsoluteOnAnyHost(rel)) {
        const auto exact = caseInsensitivePath(norm(rel));
        if (!exact.empty()) return exact;
    }
    const auto normalized = norm(stripDevAbsolutePrefix(rel, {"materials\\", "textures\\"}));
    if (std::filesystem::path(normalized).is_absolute()) {
        const auto rooted = caseInsensitivePath(normalized);
        if (!rooted.empty()) return rooted;
    }

    std::string nifDir;
    if (!nifPath.empty()) {
        const auto full = std::filesystem::absolute(nifPath).lexically_normal();
        nifDir = full.parent_path().string();
    }

    const auto fsRel = [](std::string_view value) {
        std::string out(value);
        for (auto& ch : out) {
            if (ch == '\\') ch = '/';
        }
        return out;
    }(normalized);

    std::vector<std::filesystem::path> candidates;
    if (!textureRoot.empty() && std::filesystem::is_directory(textureRoot)) {
        candidates.emplace_back(std::filesystem::path(textureRoot) / fsRel);
        if (startsWithInsensitive(normalized, "textures\\")) {
            candidates.emplace_back(std::filesystem::path(textureRoot) / fsRel.substr(9));
        }
        candidates.emplace_back(std::filesystem::path(textureRoot) / "Textures" / fsRel);
        candidates.emplace_back(
            std::filesystem::path(textureRoot) / std::filesystem::path(fsRel).filename());
    }

    auto dir = std::filesystem::path(nifDir);
    while (!dir.empty()) {
        candidates.emplace_back(dir / fsRel);
        candidates.emplace_back(dir / "Textures" / fsRel);
        const auto parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    if (!nifDir.empty()) {
        candidates.emplace_back(
            std::filesystem::path(nifDir) / std::filesystem::path(fsRel).filename());
    }

    for (const auto& candidate : candidates) {
        const auto resolved = caseInsensitivePath(candidate);
        if (!resolved.empty()) return resolved;
    }

    for (const auto& root : globalRoots()) {
        if (root.empty()) continue;
        const auto direct = caseInsensitivePath(root / fsRel);
        if (!direct.empty()) return direct;
        if (startsWithInsensitive(normalized, "textures\\")) {
            const auto stripped = caseInsensitivePath(root / fsRel.substr(9));
            if (!stripped.empty()) return stripped;
        } else {
            const auto prefixed = caseInsensitivePath(root / "Textures" / fsRel);
            if (!prefixed.empty()) return prefixed;
        }
    }

    return resolveFromArchives(normalized, nifDir, textureRoot, "textures");
}

std::optional<std::filesystem::path> TextureResolver::resolveMaterialFile(
    std::string_view nifPath,
    std::string_view rel,
    std::string_view textureRoot) {
    if (isAbsoluteOnAnyHost(rel)) {
        const auto exact = caseInsensitivePath(norm(rel));
        if (!exact.empty()) return exact;
    }
    const auto normalized = norm(stripDevAbsolutePrefix(rel, {"materials\\", "textures\\"}));
    if (std::filesystem::path(normalized).is_absolute()) {
        const auto rooted = caseInsensitivePath(normalized);
        if (!rooted.empty()) return rooted;
    }

    std::string nifDir;
    if (!nifPath.empty()) {
        const auto full = std::filesystem::absolute(nifPath).lexically_normal();
        nifDir = full.parent_path().string();
    }

    const auto fsRel = [](std::string_view value) {
        std::string out(value);
        for (auto& ch : out) {
            if (ch == '\\') ch = '/';
        }
        return out;
    }(normalized);

    std::vector<std::filesystem::path> candidates;
    if (!textureRoot.empty() && std::filesystem::is_directory(textureRoot)) {
        candidates.emplace_back(std::filesystem::path(textureRoot) / fsRel);
        if (startsWithInsensitive(normalized, "materials\\")) {
            candidates.emplace_back(std::filesystem::path(textureRoot) / fsRel.substr(10));
        }
        candidates.emplace_back(std::filesystem::path(textureRoot) / "Materials" / fsRel);
    }

    auto dir = std::filesystem::path(nifDir);
    while (!dir.empty()) {
        candidates.emplace_back(dir / fsRel);
        candidates.emplace_back(dir / "Materials" / fsRel);
        const auto parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }

    for (const auto& candidate : candidates) {
        const auto resolved = caseInsensitivePath(candidate);
        if (!resolved.empty()) return resolved;
    }

    for (const auto& root : globalRoots()) {
        if (root.empty()) continue;
        const auto direct = caseInsensitivePath(root / fsRel);
        if (!direct.empty()) return direct;
        if (startsWithInsensitive(normalized, "materials\\")) {
            const auto stripped = caseInsensitivePath(root / fsRel.substr(10));
            if (!stripped.empty()) return stripped;
        } else {
            const auto prefixed = caseInsensitivePath(root / "Materials" / fsRel);
            if (!prefixed.empty()) return prefixed;
        }
    }

    return resolveFromArchives(normalized, nifDir, textureRoot, "materials");
}

template <typename Compute>
std::optional<std::filesystem::path> TextureResolver::memo(std::string key, Compute&& compute) {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(memoMutex_);
        const auto found = pathMemo_.find(key);
        if (found != pathMemo_.end() &&
            (found->second.value || std::chrono::steady_clock::now() - found->second.at < kRootRefreshInterval)) {
            counters_.memoHits++;
            return found->second.value;
        }
        counters_.memoMisses++;
        generation = memoGeneration_;
    }
    std::optional<std::filesystem::path> value = std::forward<Compute>(compute)();
    // Invariant: an extracted or converted cache file is never memoized, its content may change under an unchanged path.
    const bool fromCache = value && startsWithInsensitive(value->generic_string(), cacheDir_.generic_string());
    std::lock_guard<std::mutex> guard(memoMutex_);
    if (generation == memoGeneration_ && !fromCache) {
        pathMemo_.insert_or_assign(std::move(key), MemoEntry{value, std::chrono::steady_clock::now()});
    }
    return value;
}

std::optional<std::string> TextureResolver::resolveMaterialDiffuse(
    std::string_view nifPath,
    std::string_view bgsmRelPath,
    std::string_view textureRoot) {
    const auto bgsmFile = resolveMaterialPath(nifPath, bgsmRelPath, textureRoot);
    if (!bgsmFile) return std::nullopt;
    try {
        const auto file = materials_.load(*bgsmFile);
        if (!file) return std::nullopt;
        const char* slot = file->format == fo4::material::MaterialFormat::bgem ? "BaseTexture" : "DiffuseTexture";
        const auto diffuse = fo4::material::get_field<std::string>(*file, slot);
        if (!diffuse) return std::nullopt;
        return std::string(fo4::material::text(*diffuse));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> TextureResolver::resolveFromArchives(
    std::string_view rel,
    std::string_view nifDir,
    std::string_view textureRoot,
    std::string_view topFolder) {
    const auto keys = lookupKeys(rel, topFolder);

    std::uint64_t epoch = 0;
    const auto session = globalRoots(&epoch);
    if (auto hit = scanAndLookup(session, keys, std::nullopt, epoch)) return hit;

    std::vector<std::filesystem::path> cheap;
    if (!nifDir.empty()) {
        auto roots = addDataRoots(nifDir);
        cheap.insert(cheap.end(), roots.begin(), roots.end());
    }
    std::optional<std::filesystem::path> shallowRoot;
    if (!textureRoot.empty()) {
        auto roots = addDataRoots(textureRoot);
        if (roots.empty()) shallowRoot = std::filesystem::path(textureRoot);
        cheap.insert(cheap.end(), roots.begin(), roots.end());
    }
    return scanAndLookup(cheap, keys, shallowRoot, epoch);
}

std::optional<std::filesystem::path> TextureResolver::getTexturePngPath(
    std::string_view nifPath,
    std::string_view relTexPath,
    std::string_view textureRoot,
    bool reconstructZ) {
    const auto nif = trimmedQuotes(nifPath);
    const auto rel = trimmedQuotes(relTexPath);
    if (rel.empty()) return std::nullopt;

    core::ScopedNs timed(counters_.resolveNs);
    auto texRel = rel;
    if (endsWithInsensitive(texRel, ".bgsm") || endsWithInsensitive(texRel, ".bgem")) {
        const auto diffuse = resolveMaterialDiffuse(nif, texRel, textureRoot);
        if (!diffuse || diffuse->empty()) return std::nullopt;
        texRel = *diffuse;
    }

    const auto dds = memo("dds|" + memoKey(nif, texRel, textureRoot), [&] {
        return resolveDds(nif, texRel, textureRoot);
    });
    if (!dds) return std::nullopt;
    const auto png = convertToPng(*dds, reconstructZ);
    if (png && std::filesystem::exists(*png)) return png;
    return std::nullopt;
}

std::optional<std::filesystem::path> TextureResolver::resolveTextureFile(
    std::string_view nifPath,
    std::string_view rel,
    std::string_view textureRoot) {
    return resolveDds(trimmedQuotes(nifPath), trimmedQuotes(rel), textureRoot);
}

std::optional<std::filesystem::path> TextureResolver::resolveMaterialPath(
    std::string_view nifPath,
    std::string_view rel,
    std::string_view textureRoot) {
    const auto nif = trimmedQuotes(nifPath);
    const auto file = trimmedQuotes(rel);
    return memo("mat|" + memoKey(nif, file, textureRoot), [&] {
        return resolveMaterialFile(nif, file, textureRoot);
    });
}

std::string TextureResolver::getTexturePngDataUrl(std::string_view nifPath,
                                                  std::string_view relTexPath,
                                                  std::string_view textureRoot,
                                                  bool reconstructZ) {
    counters_.textureCalls++;
    const auto png = getTexturePngPath(nifPath, relTexPath, textureRoot, reconstructZ);
    if (!png) return "";
    const auto pngBytes = readBytes(*png);
    if (pngBytes.empty()) return "";
    const std::span<const std::byte> span(pngBytes.data(), pngBytes.size());
    core::ScopedNs encode(counters_.base64Ns);
    return "data:image/png;base64," + core::encodeBase64(span);
}

std::optional<std::filesystem::path> TextureResolver::resolveNif(std::string_view relPath) {
    auto rel = trimmedQuotes(relPath);
    if (rel.empty() || hasUncRoot(rel)) return std::nullopt;
    if (isAbsoluteOnAnyHost(rel)) {
        const auto exact = caseInsensitivePath(norm(rel));
        if (!exact.empty()) return exact;
    }
    rel = norm(stripDevAbsolutePrefix(rel, {"meshes\\"}));

    core::ScopedNs timed(counters_.resolveNs);
    return memo("nif|" + lower(rel), [&]() -> std::optional<std::filesystem::path> {
        if (std::filesystem::path(rel).is_absolute()) {
            const auto rooted = caseInsensitivePath(rel);
            if (!rooted.empty()) return rooted;
        }

        const auto fsRel = [](std::string_view value) {
            std::string out(value);
            for (auto& ch : out) {
                if (ch == '\\') ch = '/';
            }
            return out;
        }(rel);

        std::uint64_t epoch = 0;
        const auto roots = globalRoots(&epoch);
        for (const auto& root : roots) {
            if (root.empty()) continue;
            const auto direct = caseInsensitivePath(root / fsRel);
            if (!direct.empty()) return direct;
            if (startsWithInsensitive(rel, "meshes\\")) {
                const auto stripped = caseInsensitivePath(root / fsRel.substr(7));
                if (!stripped.empty()) return stripped;
            } else {
                const auto prefixed = caseInsensitivePath(root / "Meshes" / fsRel);
                if (!prefixed.empty()) return prefixed;
            }
        }
        return scanAndLookup(roots, lookupKeys(rel, "meshes"), std::nullopt, epoch);
    });
}

}
