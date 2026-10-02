#include <fo4ide/archive/RadicleArchiveBackend.hpp>
#include <fo4ide/core/JsonInt.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4/archive/ba2_texture.hpp>
#include <fo4/io/atomic_write.hpp>
#include <fo4ide/textures/DdsDecoder.hpp>

namespace fo4ide::archive {
namespace {

std::string lower(std::string_view value) {
    std::string out(value);
    std::ranges::transform(out, out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string normalizedArchivePath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (const unsigned char ch : path) {
        if (ch == '/' || ch == '\\') out.push_back('\\');
        else out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

std::string stringField(const nlohmann::json& arguments, std::string_view name) {
    if (!arguments.is_object()) return {};
    const auto it = arguments.find(std::string(name));
    if (it == arguments.end() || !it->is_string()) return {};
    return it->get<std::string>();
}

int intField(const nlohmann::json& arguments, std::string_view name, int fallback) {
    if (!arguments.is_object()) return fallback;
    const auto it = arguments.find(std::string(name));
    if (it == arguments.end()) return fallback;
    return core::jsonIntSaturated(*it).value_or(fallback);
}

bool boolField(const nlohmann::json& arguments, std::string_view name, bool fallback) {
    if (!arguments.is_object()) return fallback;
    const auto it = arguments.find(std::string(name));
    if (it == arguments.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
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

std::string hex8(std::uint32_t value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string text(8, '0');
    for (int i = 7; i >= 0; --i) {
        text[static_cast<std::size_t>(i)] = digits[value & 0xFu];
        value >>= 4u;
    }
    return text;
}

class PathFilter {
public:
    PathFilter(std::string filter, std::string mode) {
        if (filter.empty()) return;
        const auto normalizedMode = lower(mode);
        if (normalizedMode.empty() || normalizedMode == "simple") {
            simple_ = lower(filter);
            return;
        }
        if (normalizedMode == "wildcard") {
            std::string expression{"^"};
            for (const char ch : filter) {
                if (ch == '*') expression += ".*";
                else if (ch == '?') expression += '.';
                else {
                    if (std::string_view{".^$|()[]{}+\\"}.find(ch) != std::string_view::npos) {
                        expression.push_back('\\');
                    }
                    expression.push_back(ch);
                }
            }
            expression += '$';
            pattern_.emplace(expression, std::regex_constants::ECMAScript | std::regex_constants::icase);
            return;
        }
        if (normalizedMode == "regex") {
            pattern_.emplace(filter, std::regex_constants::ECMAScript | std::regex_constants::icase);
            return;
        }
        throw std::invalid_argument("filterMode must be simple, wildcard, or regex");
    }

    bool matches(std::string_view path) const {
        if (!simple_.empty()) return lower(path).find(simple_) != std::string::npos;
        if (pattern_) return std::regex_search(path.begin(), path.end(), *pattern_);
        return true;
    }

private:
    std::string simple_;
    std::optional<std::regex> pattern_;
};

// The C# oracle (Ba2Codec) dispatches on the header tag, and both Radicle
// archive types expose the same entry surface (path, chunks, decompressed
// sizes, zlib-compressed payloads). Erasing the GNRL/DX10 distinction keeps
// listing, extraction, comparison and previewing uniform across both.
using AnyArchive =
    std::variant<fo4::archive::Ba2GeneralArchive, fo4::archive::Ba2TextureArchive>;

AnyArchive openArchive(const std::filesystem::path& path) {
    if (lower(path.extension().string()) != ".ba2") {
        throw std::invalid_argument("Radicle archive reading currently supports .ba2 files only");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::invalid_argument("could not open archive: " + path.string());
    const auto magic = readLe32(stream);
    if (magic != 0x58445442u) {  // BTDX
        throw std::invalid_argument("not a BA2 archive: " + path.string());
    }
    (void)readLe32(stream);  // version
    const auto tag = readLe32(stream);

    auto requireAddressable = [](const auto& archive) {
        if (!archive.entries.empty() && !archive.has_string_table) {
            throw std::invalid_argument("BA2 has no string table, so entries cannot be addressed by path");
        }
    };

    if (tag == 0x4C524E47u) {  // GNRL
        auto archive = fo4::archive::read_ba2_general_file(path);
        requireAddressable(archive);
        return archive;
    }
    if (tag == 0x30315844u) {  // DX10
        auto archive = fo4::archive::read_ba2_texture_file(path);
        requireAddressable(archive);
        return archive;
    }
    throw std::invalid_argument("unsupported BA2 content format 0x" + hex8(tag) +
                                " (only GNRL and DX10 are FO4)");
}

template <typename Entry>
std::uint64_t entrySize(const Entry& entry) {
    std::uint64_t total = 0;
    for (const auto& chunk : entry.chunks) total += chunk.decompressed_size;
    return total;
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

bool invalidWindowsComponent(std::string_view component) {
    if (component.empty() || component == "." || component == "..") return true;
    if (component.back() == ' ' || component.back() == '.') return true;
    for (const unsigned char ch : component) {
        if (ch < 0x20 || std::string_view{"<>:\"|?*"}.find(static_cast<char>(ch)) != std::string_view::npos) {
            return true;
        }
    }

    auto base = lower(component);
    if (const auto dot = base.find('.'); dot != std::string::npos) base.resize(dot);
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return true;
    if (base.size() == 4 && (base.starts_with("com") || base.starts_with("lpt")) &&
        base[3] >= '1' && base[3] <= '9') return true;
    return false;
}

std::filesystem::path safeRelativePath(std::string_view innerPath) {
    if (innerPath.empty()) throw std::invalid_argument("archive entry path is empty");
    std::string normalized;
    normalized.reserve(innerPath.size());
    for (const char ch : innerPath) normalized.push_back(ch == '\\' ? '/' : ch);
    if (normalized.front() == '/' || normalized.find(':') != std::string::npos) {
        throw std::invalid_argument("unsafe absolute archive entry path: " + std::string(innerPath));
    }

    std::filesystem::path relative;
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        const auto end = normalized.find('/', begin);
        const auto length = end == std::string::npos ? normalized.size() - begin : end - begin;
        const std::string_view component(normalized.data() + begin, length);
        if (invalidWindowsComponent(component)) {
            throw std::invalid_argument("unsafe archive entry path component: " + std::string(component));
        }
        relative /= std::filesystem::path(std::u8string(component.begin(), component.end()));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return relative;
}

void rejectSymlinkPath(const std::filesystem::path& path) {
    auto current = path.root_path();
    for (const auto& component : path.relative_path()) {
        current /= component;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(current, ec);
        if (ec || !std::filesystem::exists(status)) continue;
        if (std::filesystem::is_symlink(status)) {
            throw std::invalid_argument("refusing extraction through symbolic link: " + current.string());
        }
    }
}

std::filesystem::path destinationFor(const std::filesystem::path& root,
                                     std::string_view innerPath) {
    if (root.empty()) throw std::invalid_argument("output directory is empty");
    const auto absoluteRoot = std::filesystem::absolute(root).lexically_normal();
    rejectSymlinkPath(absoluteRoot);
    const auto relative = safeRelativePath(innerPath);
    const auto destination = (absoluteRoot / relative).lexically_normal();
    rejectSymlinkPath(destination.parent_path());
    return destination;
}

// Stages and atomically replaces the destination. All staging/replace
// mechanics come from Radicle's fo4::io::StagedFile (exclusive O_NOFOLLOW
// staging, fsync, single replace primitive) so extraction shares exactly
// the same replacement semantics as plugin/BA2/string writes — there is no
// second, weaker implementation, and the destination is never absent or
// truncated at any observation point.
void writeAtomically(const std::filesystem::path& destination,
                     std::span<const std::byte> bytes) {
    if (destination.empty()) throw std::invalid_argument("output path is empty");
    const auto parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
        // Re-check immediately after creation: create_directories follows
        // symlinks, so a raced component must abort before anything is
        // staged. This narrows the check-to-use window as far as portable
        // C++ allows; the final replace never follows the destination.
        rejectSymlinkPath(std::filesystem::absolute(parent).lexically_normal());
    }
    {
        std::error_code statusError;
        const auto existingStatus = std::filesystem::symlink_status(destination, statusError);
        if (!statusError && std::filesystem::exists(existingStatus) &&
            (std::filesystem::is_symlink(existingStatus) || std::filesystem::is_directory(existingStatus))) {
            throw std::invalid_argument("refusing to replace non-regular output path: " + destination.string());
        }
    }

    auto staged = fo4::io::StagedFile::create(destination);
    staged.write(bytes);
    staged.finish();
    try {
        staged.verify(bytes.size());
    } catch (...) {
        // Abandoning the attempt must not leave .radicle-tmp litter.
        std::move(staged).cancel();
        throw;
    }
    std::move(staged).commit();
}

nlohmann::json listArchive(const nlohmann::json& arguments) {
    const auto path = std::filesystem::path(stringField(arguments, "archivePath"));
    const PathFilter filter(stringField(arguments, "filter"), stringField(arguments, "filterMode"));
    const auto limit = std::clamp(intField(arguments, "limit", 2000), 1, 100000);
    const auto archive = openArchive(path);

    nlohmann::json rows = nlohmann::json::array();
    std::size_t totalCount = 0;
    std::size_t matches = 0;
    std::visit([&](const auto& a) {
        totalCount = a.entries.size();
        for (const auto& entry : a.entries) {
            if (!filter.matches(entry.path)) continue;
            if (rows.size() < static_cast<std::size_t>(limit)) {
                rows.push_back({{"path", entry.path}, {"size", entrySize(entry)}});
            }
            ++matches;
        }
    }, archive);
    const auto shown = rows.size();
    return {
        {"archiveName", path.filename().string()},
        {"totalCount", totalCount},
        {"shownCount", shown},
        {"truncated", shown < matches},
        {"entries", std::move(rows)}
    };
}

nlohmann::json extractFile(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto innerPath = stringField(arguments, "innerPath");
    const auto outputPath = std::filesystem::path(stringField(arguments, "outputPath"));
    const auto archive = openArchive(archivePath);
    std::vector<std::byte> payload;
    std::visit([&](const auto& a) {
        const auto* entry = findEntry(a, innerPath);
        if (entry == nullptr) {
            throw std::invalid_argument("archive entry not found: " + std::string(innerPath));
        }
        payload = extractEntry(*entry);
    }, archive);
    writeAtomically(outputPath, payload);
    return "Extracted " + innerPath + " to " + outputPath.string();
}

nlohmann::json extractSelected(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto outputDirectory = std::filesystem::path(stringField(arguments, "outputDirectory"));
    const auto archive = openArchive(archivePath);
    const auto pathsIt = arguments.find("innerPaths");
    if (pathsIt == arguments.end() || !pathsIt->is_array() || pathsIt->empty()) {
        throw std::invalid_argument("innerPaths must contain at least one archive entry path");
    }

    struct WorkItem {
        std::string innerPath;
        std::filesystem::path destination;
    };
    std::vector<WorkItem> work;
    std::set<std::string> destinations;
    std::visit([&](const auto& a) {
        for (const auto& value : *pathsIt) {
            if (!value.is_string()) throw std::invalid_argument("innerPaths values must be strings");
            const auto innerPath = value.get<std::string>();
            const auto* entry = findEntry(a, innerPath);
            if (entry == nullptr) {
                throw std::invalid_argument("archive entry not found: " + innerPath);
            }
            const auto destination = destinationFor(outputDirectory, entry->path);
            const auto key = lower(destination.generic_string());
            if (!destinations.insert(key).second) {
                throw std::invalid_argument("selected entries collide at output path: " + destination.string());
            }
            work.push_back({innerPath, destination});
        }
    }, archive);

    for (const auto& item : work) {
        std::vector<std::byte> payload;
        std::visit([&](const auto& a) {
            const auto* entry = findEntry(a, item.innerPath);
            if (entry == nullptr) {
                throw std::invalid_argument("archive entry not found: " + item.innerPath);
            }
            payload = extractEntry(*entry);
        }, archive);
        writeAtomically(item.destination, payload);
    }
    return "Extracted " + std::to_string(work.size()) + " file(s) to " + outputDirectory.string();
}

nlohmann::json extractAll(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto outputDirectory = std::filesystem::path(stringField(arguments, "outputDirectory"));
    const PathFilter filter(stringField(arguments, "filter"), stringField(arguments, "filterMode"));
    const auto limit = std::clamp(intField(arguments, "limit", 5000), 1, 100000);
    const auto archive = openArchive(archivePath);

    std::vector<std::string> innerPaths;
    std::set<std::string> destinations;
    std::vector<std::filesystem::path> outputPaths;
    std::visit([&](const auto& a) {
        for (const auto& entry : a.entries) {
            if (!filter.matches(entry.path)) continue;
            if (innerPaths.size() >= static_cast<std::size_t>(limit)) break;
            const auto destination = destinationFor(outputDirectory, entry.path);
            const auto key = lower(destination.generic_string());
            if (!destinations.insert(key).second) {
                throw std::invalid_argument("archive entries collide at output path: " + destination.string());
            }
            innerPaths.push_back(entry.path);
            outputPaths.push_back(destination);
        }
    }, archive);

    for (std::size_t index = 0; index < innerPaths.size(); ++index) {
        std::vector<std::byte> payload;
        std::visit([&](const auto& a) {
            const auto* entry = findEntry(a, innerPaths[index]);
            if (entry == nullptr) {
                throw std::invalid_argument("archive entry not found: " + innerPaths[index]);
            }
            payload = extractEntry(*entry);
        }, archive);
        writeAtomically(outputPaths[index], payload);
    }
    return "Extracted " + std::to_string(innerPaths.size()) + " file(s) to " + outputDirectory.string();
}

nlohmann::json compareArchives(const nlohmann::json& arguments) {
    const auto pathA = std::filesystem::path(stringField(arguments, "archiveA"));
    const auto pathB = std::filesystem::path(stringField(arguments, "archiveB"));
    const auto archiveA = openArchive(pathA);
    const auto archiveB = openArchive(pathB);

    struct ErasedEntry {
        std::string path;
        std::uint64_t size;
        std::function<std::vector<std::byte>()> extract;
    };
    using EntryMap = std::map<std::string, ErasedEntry>;

    const auto makeMap = [](const AnyArchive& archive) {
        EntryMap map;
        std::visit([&](const auto& a) {
            for (const auto& entry : a.entries) {
                const auto key = normalizedArchivePath(entry.path);
                const auto* entryPtr = &entry;
                if (!map.emplace(key, ErasedEntry{
                                          entry.path,
                                          entrySize(entry),
                                          [entryPtr] { return extractEntry(*entryPtr); }}).second) {
                    throw std::invalid_argument("archive contains case-insensitive duplicate path: " + entry.path);
                }
            }
        }, archive);
        return map;
    };
    const auto a = makeMap(archiveA);
    const auto b = makeMap(archiveB);

    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::vector<std::string> changed;
    std::size_t identical = 0;

    for (const auto& [key, entryA] : a) {
        const auto found = b.find(key);
        if (found == b.end()) {
            removed.push_back(entryA.path);
            continue;
        }
        const auto& entryB = found->second;
        if (entryA.size != entryB.size || entryA.extract() != entryB.extract()) {
            changed.push_back(entryB.path);
        } else {
            ++identical;
        }
    }
    for (const auto& [key, entryB] : b) {
        if (!a.contains(key)) added.push_back(entryB.path);
    }

    const auto sortPaths = [](std::vector<std::string>& paths) {
        std::ranges::sort(paths, {}, [](const std::string& path) { return lower(path); });
    };
    sortPaths(added);
    sortPaths(removed);
    sortPaths(changed);

    return {
        {"archiveA", pathA.filename().string()},
        {"archiveB", pathB.filename().string()},
        {"added", added},
        {"removed", removed},
        {"changed", changed},
        {"identicalCount", identical}
    };
}

std::string encodeBase64(std::span<const std::byte> data) {
    static constexpr char k_alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t index = 0;
    while (index + 3 <= data.size()) {
        const auto a = std::to_integer<std::uint32_t>(data[index]);
        const auto b = std::to_integer<std::uint32_t>(data[index + 1]);
        const auto c = std::to_integer<std::uint32_t>(data[index + 2]);
        const auto value = (a << 16u) | (b << 8u) | c;
        out.push_back(k_alphabet[(value >> 18u) & 0x3Fu]);
        out.push_back(k_alphabet[(value >> 12u) & 0x3Fu]);
        out.push_back(k_alphabet[(value >> 6u) & 0x3Fu]);
        out.push_back(k_alphabet[value & 0x3Fu]);
        index += 3;
    }
    const auto remaining = data.size() - index;
    if (remaining == 1) {
        const auto a = std::to_integer<std::uint32_t>(data[index]);
        out.push_back(k_alphabet[(a >> 2u) & 0x3Fu]);
        out.push_back(k_alphabet[(a << 4u) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const auto a = std::to_integer<std::uint32_t>(data[index]);
        const auto b = std::to_integer<std::uint32_t>(data[index + 1]);
        const auto value = (a << 8u) | b;
        out.push_back(k_alphabet[(value >> 10u) & 0x3Fu]);
        out.push_back(k_alphabet[(value >> 4u) & 0x3Fu]);
        out.push_back(k_alphabet[(value << 2u) & 0x3Fu]);
        out.push_back('=');
    }
    return out;
}

// Preview mirrors the C# TextureService flow (resolve DDS from an archive,
// decode to PNG, hand the UI a data URL) but replaces the external texconv
// dependency with the in-process Radicle/native codecs.
nlohmann::json previewTexture(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto innerPath = stringField(arguments, "innerPath");
    const bool reconstructZ = boolField(arguments, "reconstructZ", false);

    const auto archive = openArchive(archivePath);
    std::vector<std::byte> ddsBytes;
    std::visit([&](const auto& a) {
        const auto* entry = findEntry(a, innerPath);
        if (entry == nullptr) {
            throw std::invalid_argument("archive entry not found: " + std::string(innerPath));
        }
        ddsBytes = extractEntry(*entry);
    }, archive);

    const auto* begin = reinterpret_cast<const std::uint8_t*>(ddsBytes.data());
    const std::span<const std::uint8_t> dds(begin, ddsBytes.size());
    auto png = fo4ide::textures::ddsToPng(dds, reconstructZ);
    if (!png) {
        throw std::invalid_argument(png.error().message);
    }
    const auto& pngBytes = png.value();
    const std::span<const std::byte> pngSpan(
        reinterpret_cast<const std::byte*>(pngBytes.data()), pngBytes.size());
    return {
        {"dataUrl", "data:image/png;base64," + encodeBase64(pngSpan)},
        {"bytes", pngBytes.size()}
    };
}

}

Result<nlohmann::json> RadicleArchiveBackend::invoke(
    std::string_view operation,
    const nlohmann::json& arguments) {
    try {
        if (operation == "list") return listArchive(arguments);
        if (operation == "extractFile") return extractFile(arguments);
        if (operation == "extractSelected") return extractSelected(arguments);
        if (operation == "extractAll") return extractAll(arguments);
        if (operation == "compare") return compareArchives(arguments);
        if (operation == "previewTexture") return previewTexture(arguments);
        return std::unexpected(Error{
            "ArchiveOperationUnsupported",
            "Unsupported Radicle archive operation: " + std::string(operation)
        });
    } catch (const std::regex_error& error) {
        return std::unexpected(Error{"ArchiveFilterInvalid", error.what()});
    } catch (const std::invalid_argument& error) {
        return std::unexpected(Error{"ArchiveRequestInvalid", error.what()});
    } catch (const fo4::archive::Ba2Error& error) {
        return std::unexpected(Error{"ArchiveReadFailed", error.what()});
    } catch (const std::exception& error) {
        return std::unexpected(Error{"ArchiveOperationFailed", error.what()});
    }
}

}
