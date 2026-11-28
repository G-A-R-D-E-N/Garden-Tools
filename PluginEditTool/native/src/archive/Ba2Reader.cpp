#include <fo4ide/archive/Ba2Reader.hpp>

#include <fo4/archive/ba2_open.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace fo4ide::archive {
namespace {

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
    return Ba2Reader(path, fo4::archive::open_ba2(path));
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

bool Ba2Reader::entryMatchesDisk(std::string_view innerPath) const noexcept {
    try {
        std::ifstream stream(path_, std::ios::binary);
        if (!stream) return false;
        return std::visit([&](const auto& a) {
            const auto* entry = findEntry(a, innerPath);
            if (entry == nullptr) return false;
            std::vector<char> onDisk;
            for (const auto& chunk : entry->chunks) {
                if (chunk.data.empty()) continue;
                onDisk.resize(chunk.data.size());
                stream.seekg(static_cast<std::streamoff>(chunk.file_offset));
                stream.read(onDisk.data(), static_cast<std::streamsize>(onDisk.size()));
                if (!stream ||
                    !std::equal(onDisk.begin(), onDisk.end(),
                                reinterpret_cast<const char*>(chunk.data.data()))) {
                    return false;
                }
            }
            return true;
        }, archive_);
    } catch (...) {
        return false;
    }
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
