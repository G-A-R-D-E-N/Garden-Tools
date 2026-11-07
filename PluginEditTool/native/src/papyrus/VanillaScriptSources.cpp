#include <fo4ide/papyrus/VanillaScriptSources.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include <zlib.h>

#include <fo4ide/papyrus/ScriptIndex.hpp>

namespace fo4ide::papyrus {

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kEndOfCentralDir = 0x06054b50u;
constexpr std::uint32_t kCentralHeader = 0x02014b50u;
constexpr std::uint32_t kLocalHeader = 0x04034b50u;
constexpr std::size_t kEndRecordSize = 22;
constexpr std::size_t kMaxCommentSize = 0xFFFF;
constexpr std::size_t kCentralFixedSize = 46;
constexpr std::size_t kLocalFixedSize = 30;
constexpr std::uint16_t kStored = 0;
constexpr std::uint16_t kDeflated = 8;
constexpr std::size_t kMaxEntryBytes = 64u << 20;
constexpr std::size_t kMaxTotalBytes = 512u << 20;

void need(bool ok, std::string_view what) {
    if (!ok) throw std::runtime_error("zip: " + std::string(what));
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    need(at <= bytes.size() && bytes.size() - at >= 2, "truncated field");
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    need(at <= bytes.size() && bytes.size() - at >= 4, "truncated field");
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::vector<std::uint8_t> readFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(size);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return bytes;
}

std::string readFileText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string lowerAscii(std::string text) {
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

bool hasPscExtension(std::string_view name) {
    return lowerAscii(std::string(name)).ends_with(".psc");
}

std::size_t findEndOfCentralDir(const std::vector<std::uint8_t>& bytes) {
    need(bytes.size() >= kEndRecordSize, "file too small");
    const std::size_t lowest = bytes.size() > kEndRecordSize + kMaxCommentSize
                                   ? bytes.size() - kEndRecordSize - kMaxCommentSize
                                   : 0;
    for (std::size_t at = bytes.size() - kEndRecordSize + 1; at-- > lowest;) {
        if (readU32(bytes, at) == kEndOfCentralDir) return at;
    }
    throw std::runtime_error("zip: no end of central directory");
}

// Entry names must not be absolute, drive-letter, backslash or dot-component paths.
void checkEntryName(const std::string& name) {
    need(!name.empty() && name.front() != '/' && name.find(':') == std::string::npos &&
             name.find('\\') == std::string::npos,
         "unsafe entry name " + name);
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto slash = name.find('/', start);
        const auto part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        need(!part.empty() && part != "." && part != "..", "unsafe entry name " + name);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
}

std::string inflateEntry(const std::vector<std::uint8_t>& bytes, std::size_t localOffset,
                         std::uint16_t method, std::size_t compressedSize,
                         std::size_t uncompressedSize, const std::string& name) {
    need(readU32(bytes, localOffset) == kLocalHeader, "bad local header for " + name);
    const auto nameSize = readU16(bytes, localOffset + 26);
    const auto extraSize = readU16(bytes, localOffset + 28);
    const std::size_t dataAt = localOffset + kLocalFixedSize + nameSize + extraSize;
    need(dataAt <= bytes.size() && compressedSize <= bytes.size() - dataAt,
         "entry data outside the file: " + name);
    const auto* data = bytes.data() + dataAt;
    if (method == kStored) {
        need(compressedSize == uncompressedSize, "stored size mismatch for " + name);
        return std::string(reinterpret_cast<const char*>(data), compressedSize);
    }
    need(method == kDeflated, "unsupported compression method for " + name);
    std::string out(uncompressedSize, '\0');
    z_stream stream{};
    need(inflateInit2(&stream, -MAX_WBITS) == Z_OK, "zlib init failed");
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(compressedSize);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(uncompressedSize);
    const int rc = inflate(&stream, Z_FINISH);
    const bool complete = rc == Z_STREAM_END && stream.total_out == uncompressedSize;
    inflateEnd(&stream);
    need(complete, "inflate size mismatch for " + name);
    return out;
}

std::vector<fs::path> directoryFiles(const fs::path& root, std::string_view extension) {
    std::vector<fs::path> found;
    std::error_code error;
    if (!fs::is_directory(root, error)) return found;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        if (lowerAscii(entry.path().extension().string()) == extension) found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

}  // namespace

std::vector<SnapshotSource> readPscZip(const fs::path& zipPath) {
    const auto bytes = readFileBytes(zipPath);
    const auto end = findEndOfCentralDir(bytes);
    const auto count = readU16(bytes, end + 10);
    const auto centralSize = readU32(bytes, end + 12);
    const auto centralOffset = readU32(bytes, end + 16);
    need(count != 0xFFFF && centralSize != 0xFFFFFFFFu && centralOffset != 0xFFFFFFFFu,
         "zip64 archives are not supported");
    need(centralOffset <= bytes.size() && centralSize <= bytes.size() - centralOffset,
         "central directory outside the file");

    std::vector<SnapshotSource> out;
    std::size_t cursor = centralOffset;
    std::size_t total = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        need(readU32(bytes, cursor) == kCentralHeader, "bad central directory entry");
        const auto method = readU16(bytes, cursor + 10);
        const auto compressedSize = readU32(bytes, cursor + 20);
        const auto uncompressedSize = readU32(bytes, cursor + 24);
        const auto nameSize = readU16(bytes, cursor + 28);
        const auto extraSize = readU16(bytes, cursor + 30);
        const auto commentSize = readU16(bytes, cursor + 32);
        const auto localOffset = readU32(bytes, cursor + 42);
        need(cursor + kCentralFixedSize + nameSize <= bytes.size(), "truncated entry name");
        std::string name(reinterpret_cast<const char*>(bytes.data() + cursor + kCentralFixedSize), nameSize);
        cursor += kCentralFixedSize + nameSize + extraSize + commentSize;

        if (name.empty() || name.back() == '/' || !hasPscExtension(name)) continue;
        checkEntryName(name);
        need(compressedSize != 0xFFFFFFFFu && uncompressedSize != 0xFFFFFFFFu,
             "zip64 entry " + name);
        need(uncompressedSize <= kMaxEntryBytes, "entry too large: " + name);
        total += uncompressedSize;
        need(total <= kMaxTotalBytes, "archive too large");
        out.push_back(SnapshotSource{fs::path(name),
                                     inflateEntry(bytes, localOffset, method, compressedSize,
                                                  uncompressedSize, name)});
    }
    return out;
}

std::vector<SnapshotSource> readPscDirectory(const fs::path& root) {
    std::vector<SnapshotSource> out;
    std::error_code error;
    if (!fs::is_directory(root, error)) return out;
    std::vector<std::pair<fs::path, fs::path>> files;
    for (const auto& entry :
         fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file() || lowerAscii(entry.path().extension().string()) != ".psc") continue;
        files.emplace_back(fs::relative(entry.path(), root), entry.path());
    }
    std::sort(files.begin(), files.end());
    for (auto& [relative, full] : files) {
        out.push_back(SnapshotSource{relative, readFileText(full)});
    }
    return out;
}

std::optional<VanillaImportRoot> loadVanillaImportRoot(const fs::path& dataDir,
                                                       std::string_view folder) {
    need(!folder.empty() && folder.find_first_of("/\\:") == std::string_view::npos,
         "invalid import folder name");
    const auto root = dataDir / "Scripts" / "Source" / std::string(folder);
    auto sources = readPscDirectory(root);
    if (!sources.empty()) return VanillaImportRoot{root, std::move(sources)};

    const auto zips = directoryFiles(root, ".zip");
    if (zips.empty()) return std::nullopt;
    need(zips.size() == 1, "more than one zip in " + root.string());
    return VanillaImportRoot{root, readPscZip(zips.front())};
}

std::size_t addVanillaImports(ScriptIndex& index, const fs::path& dataDir,
                              std::span<const std::string_view> folders) {
    std::size_t added = 0;
    for (const auto folder : folders) {
        auto import = loadVanillaImportRoot(dataDir, folder);
        if (!import) continue;
        index.addSnapshot(import->root, import->sources);
        ++added;
    }
    return added;
}

}  // namespace fo4ide::papyrus
