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

#include <fo4/archive/ba2_reader.hpp>
#include <fo4ide/core/Base64.hpp>
#include <fo4ide/material/MaterialService.hpp>
#include <fo4ide/textures/DdsDecoder.hpp>

namespace fo4ide::assets {
namespace {

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

std::array<std::uint32_t, 5> sha1(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const auto bitLength = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80);
    while (message.size() % 64 != 56) message.push_back(0);
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<std::uint8_t>((bitLength >> (8u * i)) & 0xFFu));
    }

    std::array<std::uint32_t, 5> h{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const auto rotateLeft = [](std::uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    };
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i) {
            const auto p = offset + static_cast<std::size_t>(i) * 4u;
            w[static_cast<std::size_t>(i)] =
                (static_cast<std::uint32_t>(message[p]) << 24u) |
                (static_cast<std::uint32_t>(message[p + 1]) << 16u) |
                (static_cast<std::uint32_t>(message[p + 2]) << 8u) |
                static_cast<std::uint32_t>(message[p + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[static_cast<std::size_t>(i)] =
                rotateLeft(w[static_cast<std::size_t>(i - 3)] ^
                               w[static_cast<std::size_t>(i - 8)] ^
                               w[static_cast<std::size_t>(i - 14)] ^
                               w[static_cast<std::size_t>(i - 16)],
                           1);
        }
        auto a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f{};
            std::uint32_t k{};
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const auto temp = rotateLeft(a, 5) + f + e + k + w[static_cast<std::size_t>(i)];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    return h;
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

TextureResolver::TextureResolver()
    : TextureResolver(std::filesystem::temp_directory_path() / "FO4RE_Tex") {}

TextureResolver::TextureResolver(std::filesystem::path cacheDir)
    : cacheDir_(std::move(cacheDir)) {}

void TextureResolver::setSessionRoots(std::vector<std::filesystem::path> roots) {
    std::lock_guard<std::mutex> guard(indexMutex_);
    sessionRoots_ = std::move(roots);
    globalRootsCache_.clear();
    globalRootsLoaded_ = false;
    index_.clear();
    archivePaths_.clear();
    scannedArchives_.clear();
    scannedRoots_.clear();
    shallowScannedRoots_.clear();
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

std::mutex& TextureResolver::lockFor(const std::string& key) {
    std::lock_guard<std::mutex> guard(keyLocksMutex_);
    auto& lock = keyLocks_[key];
    if (!lock) lock = std::make_shared<std::mutex>();
    return *lock;
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

std::string TextureResolver::sha1Hex16(std::string_view text) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(text.data());
    const auto digest = sha1(std::span<const std::uint8_t>(begin, text.size()));
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(40);
    for (const auto word : digest) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex.push_back(digits[(word >> shift) & 0xFu]);
        }
    }
    return hex.substr(0, 16);
}

std::uint64_t TextureResolver::fileStamp(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
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

std::vector<std::filesystem::path> TextureResolver::globalRoots() const {
    std::lock_guard<std::mutex> guard(indexMutex_);
    if (!globalRootsLoaded_) {
        globalRootsCache_ = sessionRoots_;
        globalRootsLoaded_ = true;
    }
    return globalRootsCache_;
}

void TextureResolver::ensureRootScanned(const std::filesystem::path& root, bool recursive) {
    const auto trimmedRoot = trimmed(root.string());
    if (trimmedRoot.empty()) return;
    auto& seen = recursive ? scannedRoots_ : shallowScannedRoots_;
    if (!seen.insert(std::filesystem::path(trimmedRoot)).second) return;
    if (!std::filesystem::is_directory(trimmedRoot)) return;
    indexPhase_.store(ArchiveIndexPhase::indexing, std::memory_order_release);

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
            collect(std::filesystem::recursive_directory_iterator(trimmedRoot, ec));
        } else {
            collect(std::filesystem::directory_iterator(trimmedRoot, ec));
        }
    } catch (const std::exception&) {
        return;
    }

    std::sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) {
        return lower(a.generic_string()) < lower(b.generic_string());
    });
    archivesTotal_.fetch_add(archives.size(), std::memory_order_release);

    for (const auto& archive : archives) {
        const auto archiveKey = lower(archive.string());
        if (scannedArchives_.insert(archiveKey).second) {
            try {
                const auto reader = fo4::archive::Ba2Reader::open(archive);
                if (!reader.entries().empty() && !reader.has_name_table()) {
                    throw std::runtime_error("BA2 has no name table: " + archive.string());
                }
                const auto id = static_cast<std::uint32_t>(archivePaths_.size());
                archivePaths_.push_back(archive.string());
                for (const auto& entry : reader.entries()) {
                    const auto& entryPath = entry.path;
                    if (!endsWithInsensitive(entryPath, ".dds") &&
                        !endsWithInsensitive(entryPath, ".nif") &&
                        !endsWithInsensitive(entryPath, ".bgsm")) {
                        continue;
                    }
                    index_.try_emplace(lower(norm(entryPath)), id);
                }
            } catch (const std::exception&) {
            }
        }
        archivesDone_.fetch_add(1, std::memory_order_release);
    }
}

std::optional<std::filesystem::path> TextureResolver::scanAndLookup(
    const std::vector<std::filesystem::path>& roots,
    const std::vector<std::string>& keys,
    const std::optional<std::filesystem::path>& shallowRoot) {
    std::lock_guard<std::mutex> guard(indexMutex_);
    {
        IndexingPhaseGuard phaseGuard{indexPhase_};
        for (const auto& root : roots) ensureRootScanned(root);
        if (shallowRoot) ensureRootScanned(*shallowRoot, false);
    }
    for (const auto& key : keys) {
        const auto it = index_.find(key);
        if (it != index_.end()) return extractFromArchive(archivePaths_[it->second], key);
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> TextureResolver::extractFromArchive(
    const std::filesystem::path& archivePath,
    std::string_view innerKey) {
    try {
        const auto stamp = fileStamp(archivePath);
        const auto key = sha1Hex16(norm(archivePath.string()) + "|" +
                                   std::to_string(stamp) + "|" +
                                   std::string(innerKey));
        std::string extension = std::filesystem::path(innerKey).extension().string();
        if (extension.empty()) extension = ".dds";
        std::error_code ec;
        std::filesystem::create_directories(cacheDir_, ec);
        const auto outPath = cacheDir_ / ("ba2_" + key + extension);
        if (std::filesystem::exists(outPath)) return outPath;

        auto& lock = lockFor(key);
        std::lock_guard<std::mutex> guard(lock);
        if (std::filesystem::exists(outPath)) return outPath;

        const auto reader = fo4::archive::Ba2Reader::open(archivePath);
        const auto index = reader.find(innerKey);
        if (!index) return std::nullopt;
        const auto bytes = reader.extract(*index);
        writeTempThenRename(outPath, bytes);
        return outPath;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> TextureResolver::decodeInProcess(
    const std::filesystem::path& ddsPath,
    const std::filesystem::path& pngPath,
    std::string_view key) {
    try {
        const auto ddsBytes = readBytes(ddsPath);
        if (ddsBytes.empty()) return std::nullopt;
        const auto* begin = reinterpret_cast<const std::uint8_t*>(ddsBytes.data());
        const std::span<const std::uint8_t> dds(begin, ddsBytes.size());
        auto png = textures::ddsToPng(dds, textures::isBc5(dds));
        if (!png) return std::nullopt;

        auto& lock = lockFor(std::string(key));
        std::lock_guard<std::mutex> guard(lock);
        if (std::filesystem::exists(pngPath)) return pngPath;

        const std::span<const std::byte> pngSpan(
            reinterpret_cast<const std::byte*>(png->data()), png->size());
        writeTempThenRename(pngPath, pngSpan);
        return pngPath;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<std::filesystem::path> TextureResolver::convertToPng(
    const std::filesystem::path& ddsPath) {
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);
    const auto key = sha1Hex16(norm(ddsPath.string()) + "|" +
                               std::to_string(fileStamp(ddsPath)) + "|v3");
    const auto pngPath = cacheDir_ / (key + ".png");
    if (std::filesystem::exists(pngPath)) return pngPath;
    return decodeInProcess(ddsPath, pngPath, key);
}

std::optional<std::filesystem::path> TextureResolver::resolveDds(
    std::string_view nifPath,
    std::string_view rel,
    std::string_view textureRoot) {
    const auto normalized = norm(rel);
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
    const auto normalized = norm(rel);
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
            candidates.emplace_back(std::filesystem::path(textureRoot) / fsRel.substr(11));
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
            const auto stripped = caseInsensitivePath(root / fsRel.substr(11));
            if (!stripped.empty()) return stripped;
        } else {
            const auto prefixed = caseInsensitivePath(root / "Materials" / fsRel);
            if (!prefixed.empty()) return prefixed;
        }
    }

    return resolveFromArchives(normalized, nifDir, textureRoot, "materials");
}

std::optional<std::string> TextureResolver::resolveMaterialDiffuse(
    std::string_view nifPath,
    std::string_view bgsmRelPath,
    std::string_view textureRoot) {
    const auto bgsmFile = resolveMaterialFile(nifPath, bgsmRelPath, textureRoot);
    if (!bgsmFile) return std::nullopt;
    try {
        material::MaterialService service;
        auto file = service.load(*bgsmFile);
        if (!file) return std::nullopt;
        const auto diffuse = fo4::material::get_field<std::string>(*file, "DiffuseTexture");
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

    if (auto hit = scanAndLookup(globalRoots(), keys)) return hit;

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
    return scanAndLookup(cheap, keys, shallowRoot);
}

std::optional<std::filesystem::path> TextureResolver::getTexturePngPath(
    std::string_view nifPath,
    std::string_view relTexPath,
    std::string_view textureRoot) {
    const auto nif = trimmedQuotes(nifPath);
    auto rel = trimmedQuotes(relTexPath);
    if (rel.empty()) return std::nullopt;

    if (endsWithInsensitive(rel, ".bgsm")) {
        const auto diffuse = resolveMaterialDiffuse(nif, rel, textureRoot);
        if (!diffuse || diffuse->empty()) return std::nullopt;
        rel = *diffuse;
    }

    const auto dds = resolveDds(nif, rel, textureRoot);
    if (!dds) return std::nullopt;
    const auto png = convertToPng(*dds);
    if (png && std::filesystem::exists(*png)) return png;
    return std::nullopt;
}

std::optional<std::filesystem::path> TextureResolver::resolveMaterialPath(
    std::string_view nifPath,
    std::string_view rel,
    std::string_view textureRoot) {
    return resolveMaterialFile(trimmedQuotes(nifPath), trimmedQuotes(rel), textureRoot);
}

std::string TextureResolver::getTexturePngDataUrl(std::string_view nifPath,
                                                  std::string_view relTexPath,
                                                  std::string_view textureRoot) {
    const auto png = getTexturePngPath(nifPath, relTexPath, textureRoot);
    if (!png) return "";
    const auto pngBytes = readBytes(*png);
    if (pngBytes.empty()) return "";
    const std::span<const std::byte> span(pngBytes.data(), pngBytes.size());
    return "data:image/png;base64," + core::encodeBase64(span);
}

std::optional<std::filesystem::path> TextureResolver::resolveNif(std::string_view relPath) {
    auto rel = trimmedQuotes(relPath);
    if (rel.empty()) return std::nullopt;
    rel = norm(rel);
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

    const auto roots = globalRoots();
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
    return scanAndLookup(roots, lookupKeys(rel, "meshes"));
}

}
