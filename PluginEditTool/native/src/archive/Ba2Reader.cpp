#include <fo4ide/archive/Ba2Reader.hpp>

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace fo4ide::archive {
namespace {

constexpr std::uint32_t k_magic = 0x58445442u;  // BTDX
constexpr std::uint32_t k_gnrl = 0x4C524E47u;   // GNRL
constexpr std::uint32_t k_dx10 = 0x30315844u;   // DX10

std::string lower(std::string_view value) {
    std::string out(value);
    for (auto& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::uint32_t readLe32(std::istream& stream) {
    std::uint8_t bytes[4]{};
    stream.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!stream) throw std::invalid_argument("could not read BA2 header");
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

const fo4::archive::Ba2GeneralEntry* findEntry(const fo4::archive::Ba2GeneralArchive& archive,
                                               std::string_view path) {
    return fo4::archive::find_ba2_general_entry(archive, path);
}

const fo4::archive::Ba2TextureEntry* findEntry(const fo4::archive::Ba2TextureArchive& archive,
                                               std::string_view path) {
    return fo4::archive::find_ba2_texture_entry(archive, path);
}

template <typename Entry>
std::vector<std::byte> extractEntry(const Entry& entry) {
    if constexpr (std::is_same_v<Entry, fo4::archive::Ba2GeneralEntry>) {
        return fo4::archive::extract_ba2_general_entry(entry);
    } else {
        return fo4::archive::extract_ba2_texture_entry(entry);
    }
}

}  // namespace

Ba2Reader::Ba2Reader(std::filesystem::path path, Any archive)
    : path_(std::move(path)),
      filename_(path_.filename().string()),
      archive_(std::move(archive)) {}

Ba2Reader Ba2Reader::open(const std::filesystem::path& path) {
    if (lower(path.extension().string()) != ".ba2") {
        throw std::invalid_argument("Radicle archive reading currently supports .ba2 files only");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::invalid_argument("could not open archive: " + path.string());
    const auto magic = readLe32(stream);
    if (magic != k_magic) {
        throw std::invalid_argument("not a BA2 archive: " + path.string());
    }
    (void)readLe32(stream);  // version
    const auto tag = readLe32(stream);

    if (tag == k_gnrl) {
        auto archive = fo4::archive::read_ba2_general_file(path);
        if (!archive.entries.empty() && !archive.has_string_table) {
            throw std::invalid_argument("BA2 has no string table, so entries cannot be addressed by path");
        }
        return Ba2Reader(path, std::move(archive));
    }
    if (tag == k_dx10) {
        auto archive = fo4::archive::read_ba2_texture_file(path);
        if (!archive.entries.empty() && !archive.has_string_table) {
            throw std::invalid_argument("BA2 has no string table, so entries cannot be addressed by path");
        }
        return Ba2Reader(path, std::move(archive));
    }
    throw std::invalid_argument("unsupported BA2 content type (only GNRL and DX10 are FO4)");
}

std::size_t Ba2Reader::entryCount() const noexcept {
    return std::visit([](const auto& a) { return a.entries.size(); }, archive_);
}

std::string Ba2Reader::entryPath(std::size_t index) const {
    std::string path;
    std::visit([&](const auto& a) {
        if (index >= a.entries.size()) {
            throw std::out_of_range("BA2 entry index out of range");
        }
        path = a.entries[index].path;
    }, archive_);
    return path;
}

std::uint64_t Ba2Reader::entrySize(std::size_t index) const noexcept {
    std::uint64_t size = 0;
    std::visit([&](const auto& a) {
        if (index < a.entries.size()) {
            for (const auto& chunk : a.entries[index].chunks) {
                size += chunk.decompressed_size;
            }
        }
    }, archive_);
    return size;
}

std::vector<std::byte> Ba2Reader::extract(std::size_t index) const {
    std::vector<std::byte> bytes;
    std::visit([&](const auto& a) {
        if (index >= a.entries.size()) {
            throw std::out_of_range("BA2 entry index out of range");
        }
        bytes = extractEntry(a.entries[index]);
    }, archive_);
    return bytes;
}

bool Ba2Reader::contains(std::string_view innerPath) const {
    return std::visit([&](const auto& a) {
        return findEntry(a, innerPath) != nullptr;
    }, archive_);
}

std::vector<std::byte> Ba2Reader::extract(std::string_view innerPath) const {
    std::vector<std::byte> bytes;
    std::visit([&](const auto& a) {
        const auto* entry = findEntry(a, innerPath);
        if (entry == nullptr) {
            throw std::invalid_argument("archive entry not found: " + std::string(innerPath));
        }
        bytes = extractEntry(*entry);
    }, archive_);
    return bytes;
}

}
