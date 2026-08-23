#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4/archive/ba2_texture.hpp>

namespace fo4ide::archive {

// Opens a .ba2 by its BTDX header tag (GNRL or DX10) and exposes the entry
// surface both archive kinds share: paths, decompressed sizes, and extraction.
// Mirrors the C# Ba2Codec dispatch; like the oracle's path-based lookups,
// addressing entries by path requires the archive to carry a string table.
//
// Throws std::invalid_argument / fo4::archive::Ba2Error on malformed input;
// extraction of a missing path throws std::invalid_argument.
class Ba2Reader {
public:
    using Any = std::variant<fo4::archive::Ba2GeneralArchive,
                             fo4::archive::Ba2TextureArchive>;

    static Ba2Reader open(const std::filesystem::path& path);

    const std::string& filename() const noexcept { return filename_; }

    std::size_t entryCount() const noexcept;
    std::string entryPath(std::size_t index) const;
    std::uint64_t entrySize(std::size_t index) const noexcept;
    std::vector<std::byte> extract(std::size_t index) const;
    std::vector<std::byte> extract(std::string_view innerPath) const;
    bool contains(std::string_view innerPath) const;

private:
    Ba2Reader(std::filesystem::path path, Any archive);

    std::filesystem::path path_;
    std::string filename_;
    Any archive_;
};

}
