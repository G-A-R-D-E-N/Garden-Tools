#include <fo4ide/archive/RadicleArchiveBackend.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4/io/atomic_write.hpp>

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
    if (it == arguments.end() || !it->is_number_integer()) return fallback;
    return it->get<int>();
}

std::uint64_t entrySize(const fo4::archive::Ba2GeneralEntry& entry) {
    std::uint64_t total = 0;
    for (const auto& chunk : entry.chunks) total += chunk.decompressed_size;
    return total;
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

fo4::archive::Ba2GeneralArchive readArchive(const std::filesystem::path& path) {
    if (lower(path.extension().string()) != ".ba2") {
        throw std::invalid_argument("Radicle archive reading currently supports .ba2 GNRL files only");
    }
    auto archive = fo4::archive::read_ba2_general_file(path);
    if (!archive.entries.empty() && !archive.has_string_table) {
        throw std::invalid_argument("BA2 has no string table, so entries cannot be addressed by path");
    }
    return archive;
}

std::vector<const fo4::archive::Ba2GeneralEntry*> matchingEntries(
    const fo4::archive::Ba2GeneralArchive& archive,
    const PathFilter& filter) {
    std::vector<const fo4::archive::Ba2GeneralEntry*> matches;
    matches.reserve(archive.entries.size());
    for (const auto& entry : archive.entries) {
        if (filter.matches(entry.path)) matches.push_back(&entry);
    }
    return matches;
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
        relative /= std::string(component);
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
    staged.verify(bytes.size());
    std::move(staged).commit();
}

const fo4::archive::Ba2GeneralEntry& requireEntry(
    const fo4::archive::Ba2GeneralArchive& archive,
    std::string_view innerPath) {
    const auto* entry = fo4::archive::find_ba2_general_entry(archive, innerPath);
    if (entry == nullptr) throw std::invalid_argument("archive entry not found: " + std::string(innerPath));
    return *entry;
}

nlohmann::json listArchive(const nlohmann::json& arguments) {
    const auto path = std::filesystem::path(stringField(arguments, "archivePath"));
    const PathFilter filter(stringField(arguments, "filter"), stringField(arguments, "filterMode"));
    const auto limit = std::clamp(intField(arguments, "limit", 2000), 1, 100000);
    const auto archive = readArchive(path);
    const auto matches = matchingEntries(archive, filter);

    nlohmann::json rows = nlohmann::json::array();
    const auto shown = std::min<std::size_t>(matches.size(), static_cast<std::size_t>(limit));
    for (std::size_t index = 0; index < shown; ++index) {
        rows.push_back({{"path", matches[index]->path}, {"size", entrySize(*matches[index])}});
    }
    return {
        {"archiveName", path.filename().string()},
        {"totalCount", archive.entries.size()},
        {"shownCount", shown},
        {"truncated", shown < matches.size()},
        {"entries", std::move(rows)}
    };
}

nlohmann::json extractFile(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto innerPath = stringField(arguments, "innerPath");
    const auto outputPath = std::filesystem::path(stringField(arguments, "outputPath"));
    const auto archive = readArchive(archivePath);
    const auto& entry = requireEntry(archive, innerPath);
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    writeAtomically(outputPath, bytes);
    return "Extracted " + innerPath + " to " + outputPath.string();
}

nlohmann::json extractSelected(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto outputDirectory = std::filesystem::path(stringField(arguments, "outputDirectory"));
    const auto archive = readArchive(archivePath);
    const auto pathsIt = arguments.find("innerPaths");
    if (pathsIt == arguments.end() || !pathsIt->is_array() || pathsIt->empty()) {
        throw std::invalid_argument("innerPaths must contain at least one archive entry path");
    }

    struct WorkItem {
        const fo4::archive::Ba2GeneralEntry* entry;
        std::filesystem::path destination;
    };
    std::vector<WorkItem> work;
    std::set<std::string> destinations;
    for (const auto& value : *pathsIt) {
        if (!value.is_string()) throw std::invalid_argument("innerPaths values must be strings");
        const auto innerPath = value.get<std::string>();
        const auto& entry = requireEntry(archive, innerPath);
        const auto destination = destinationFor(outputDirectory, entry.path);
        const auto key = lower(destination.generic_string());
        if (!destinations.insert(key).second) {
            throw std::invalid_argument("selected entries collide at output path: " + destination.string());
        }
        work.push_back({&entry, destination});
    }

    for (const auto& item : work) {
        const auto bytes = fo4::archive::extract_ba2_general_entry(*item.entry);
        writeAtomically(item.destination, bytes);
    }
    return "Extracted " + std::to_string(work.size()) + " file(s) to " + outputDirectory.string();
}

nlohmann::json extractAll(const nlohmann::json& arguments) {
    const auto archivePath = std::filesystem::path(stringField(arguments, "archivePath"));
    const auto outputDirectory = std::filesystem::path(stringField(arguments, "outputDirectory"));
    const PathFilter filter(stringField(arguments, "filter"), stringField(arguments, "filterMode"));
    const auto limit = std::clamp(intField(arguments, "limit", 5000), 1, 100000);
    const auto archive = readArchive(archivePath);
    auto matches = matchingEntries(archive, filter);
    if (matches.size() > static_cast<std::size_t>(limit)) {
        matches.resize(static_cast<std::size_t>(limit));
    }

    std::set<std::string> destinations;
    std::vector<std::pair<const fo4::archive::Ba2GeneralEntry*, std::filesystem::path>> work;
    work.reserve(matches.size());
    for (const auto* entry : matches) {
        const auto destination = destinationFor(outputDirectory, entry->path);
        const auto key = lower(destination.generic_string());
        if (!destinations.insert(key).second) {
            throw std::invalid_argument("archive entries collide at output path: " + destination.string());
        }
        work.emplace_back(entry, destination);
    }
    for (const auto& [entry, destination] : work) {
        const auto bytes = fo4::archive::extract_ba2_general_entry(*entry);
        writeAtomically(destination, bytes);
    }
    return "Extracted " + std::to_string(work.size()) + " file(s) to " + outputDirectory.string();
}

nlohmann::json compareArchives(const nlohmann::json& arguments) {
    const auto pathA = std::filesystem::path(stringField(arguments, "archiveA"));
    const auto pathB = std::filesystem::path(stringField(arguments, "archiveB"));
    const auto archiveA = readArchive(pathA);
    const auto archiveB = readArchive(pathB);

    using EntryMap = std::map<std::string, const fo4::archive::Ba2GeneralEntry*>;
    const auto makeMap = [](const fo4::archive::Ba2GeneralArchive& archive) {
        EntryMap map;
        for (const auto& entry : archive.entries) {
            const auto key = normalizedArchivePath(entry.path);
            if (!map.emplace(key, &entry).second) {
                throw std::invalid_argument("archive contains case-insensitive duplicate path: " + entry.path);
            }
        }
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
            removed.push_back(entryA->path);
            continue;
        }
        const auto* entryB = found->second;
        if (entrySize(*entryA) != entrySize(*entryB) ||
            fo4::archive::extract_ba2_general_entry(*entryA) !=
                fo4::archive::extract_ba2_general_entry(*entryB)) {
            changed.push_back(entryB->path);
        } else {
            ++identical;
        }
    }
    for (const auto& [key, entryB] : b) {
        if (!a.contains(key)) added.push_back(entryB->path);
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
