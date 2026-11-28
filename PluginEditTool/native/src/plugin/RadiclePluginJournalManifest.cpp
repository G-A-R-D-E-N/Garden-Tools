#include "RadiclePluginJournalManifest.hpp"
#include "RadiclePluginJournalLock.hpp"
#include "RadiclePluginWindowsJournalSession.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <fo4/io/path_text.hpp>
#include <fo4/text/encoding.hpp>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fo4ide::plugin::detail {
namespace {

bool hex(std::string_view value, std::size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

bool token(std::string_view value, bool underscore) {
    return !value.empty() && value.size() <= 100 &&
        std::all_of(value.begin(), value.end(), [underscore](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || (underscore && ch == '_');
        });
}

bool scriptName(std::string_view value) {
    if (value.empty() || value.size() > 200) return false;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find(':', begin);
        const auto part = value.substr(begin, end == value.npos ? value.size() - begin : end - begin);
        if (!token(part, true)) return false;
        std::string folded(part);
        std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        if (folded == "CON" || folded == "PRN" || folded == "AUX" || folded == "NUL" ||
            (folded.size() == 4 && (folded.starts_with("COM") || folded.starts_with("LPT")) &&
             folded[3] >= '1' && folded[3] <= '9')) return false;
        if (end == value.npos) return true;
        begin = end + 1;
    }
    return false;
}

bool reparse(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("cannot inspect journal destination");
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path));
#endif
}

std::filesystem::path approvedDirectory(const std::filesystem::path& directory) {
    if (!directory.is_absolute() || reparse(directory) || !std::filesystem::is_directory(directory)) {
        throw std::runtime_error("journal destination root is not an approved directory");
    }
    return std::filesystem::canonical(directory);
}

void requireSafeTarget(const std::filesystem::path& target) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(target, error);
    if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status))) return;
    if (error || reparse(target) || !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("journal destination is not a regular file");
    }
    static_cast<void>(journalIdentity(target));
}

bool sameDestination(const std::filesystem::path& first, const std::filesystem::path& second) {
#ifdef _WIN32
    const auto& left = first.native();
    const auto& right = second.native();
    if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("journal destination is too long");
    }
    const auto result = ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
        right.data(), static_cast<int>(right.size()), TRUE);
    if (result == 0) throw std::runtime_error("cannot compare journal destinations");
    return result == CSTR_EQUAL;
#else
    return first == second;
#endif
}

bool archiveFilename(std::string_view filename) {
    return !filename.empty() && filename.size() <= 255 &&
        filename.find_first_of("/\\:\0", 0, 4) == std::string_view::npos &&
        fo4::text::is_valid_utf8(filename) && fo4::text::fold(filename).ends_with(".ba2");
}

JournalArchiveReference archiveReference(const nlohmann::json& row, bool named) {
    if (!row.is_object() || row.size() != (named ? 3u : 2u) || !row.contains("root") ||
        !row["root"].is_string() || !row.contains("directories") || !row["directories"].is_array() ||
        row["directories"].size() > 64 || (named && (!row.contains("filename") || !row["filename"].is_string())))
        throw std::runtime_error("invalid journal archive reference");
    const auto root = row["root"].get<std::string>();
    if (root.empty() || root.size() > 32768 || root.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(root))
        throw std::runtime_error("invalid journal archive root");
    JournalArchiveReference result; result.rootSelector = fo4::io::from_utf8(root);
    if (!result.rootSelector.is_absolute() || result.rootSelector != result.rootSelector.lexically_normal())
        throw std::runtime_error("journal archive root must be normalized");
    for (const auto& value : row["directories"]) {
        if (!value.is_string()) throw std::runtime_error("invalid journal archive directory");
        auto part = value.get<std::string>();
        if (part.empty() || part.size() > 255 || part == "." || part == ".." ||
            part.find_first_of("/\\:\0", 0, 4) != std::string::npos || !fo4::text::is_valid_utf8(part))
            throw std::runtime_error("invalid journal archive directory");
        result.directories.push_back(std::move(part));
    }
    if (named) {
        result.filename = row["filename"].get<std::string>();
        if (!archiveFilename(result.filename)) throw std::runtime_error("invalid journal archive filename");
    }
    return result;
}

}

const JournalApprovedRoot& approvedJournalRoot(const JournalManifestEntry& entry, const JournalRoots& roots) {
    const JournalApprovedRoot* selected = nullptr;
    for (const auto& root : roots.approvedRoots) {
        if (!sameDestination(entry.rootSelector, root.path)) continue;
        if (selected) throw std::runtime_error("ambiguous journal root selector");
        selected = &root;
    }
    if (!selected || !selected->identity.lease) throw std::runtime_error("journal root is not authorized");
    return *selected;
}

bool isJournalStringName(std::string_view filename, const std::filesystem::path& plugin) {
    const auto name = fo4::text::fold(filename);
    const auto prefix = fo4::text::fold(fo4::io::utf8(plugin.stem()) + "_");
    const auto dot = name.rfind('.');
    if (!name.starts_with(prefix) || dot == std::string::npos || dot <= prefix.size()) return false;
    const auto extension = name.substr(dot + 1);
    return (extension == "strings" || extension == "dlstrings" || extension == "ilstrings") &&
           token(std::string_view(name).substr(prefix.size(), dot - prefix.size()), false);
}

JournalManifest decodeJournalManifest(std::string_view text) {
    if (text.size() > kJournalMaxManifestBytes) throw std::runtime_error("journal manifest is too large");
    const auto depthLimit = [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
        if (depth > 8) throw std::runtime_error("journal manifest nesting is too deep");
        return true;
    };
    const auto document = nlohmann::json::parse(text, depthLimit);
    if (!document.is_object() || document.size() < 4 || document.size() > 8 || !document.contains("version") ||
        !document["version"].is_number_integer() || (document["version"] != 1 && document["version"] != 2 && document["version"] != 3) ||
        !document.contains("transaction") || !document["transaction"].is_string() ||
        !document.contains("plugin") || !document["plugin"].is_string() ||
        !document.contains("files") || !document["files"].is_array() ||
        document["files"].empty() || document["files"].size() > 128) {
        throw std::runtime_error("invalid journal manifest");
    }
    JournalManifest result;
    result.version = document["version"].get<std::uint32_t>();
    result.transaction = document["transaction"].get<std::string>();
    const auto plugin = document["plugin"].get<std::string>();
    if (!hex(result.transaction, 32) || plugin.empty() || plugin.size() > 32768 ||
        plugin.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(plugin)) {
        throw std::runtime_error("invalid journal identity");
    }
    result.plugin = fo4::io::from_utf8(plugin);
    if (!result.plugin.is_absolute()) throw std::runtime_error("journal plugin identity must be absolute");
    const auto fields = 4u + (document.contains("sources") ? 1u : 0u) + (document.contains("archives") ? 1u : 0u) +
                        (document.contains("archiveOrder") ? 1u : 0u) + (document.contains("readStrings") ? 1u : 0u);
    if (document.size() != fields || document.contains("archives") != document.contains("archiveOrder") ||
        document.contains("archives") != (result.version >= 2) || document.contains("readStrings") != (result.version == 3))
        throw std::runtime_error("invalid journal manifest fields");
    if (result.version == 3) result.readStrings = decodeJournalReadStringInventories(document["readStrings"].dump(), result.plugin);
    if (document.contains("sources")) {
        if (!document["sources"].is_array() || document["sources"].size() > 16) throw std::runtime_error("invalid journal source sets");
        for (const auto& row : document["sources"]) {
            if (!row.is_object() || row.size() != 5 || !row.contains("kind") || !row["kind"].is_string() ||
                (row["kind"] != "strings" && row["kind"] != "archive-empty") ||
                !row.contains("root") || !row["root"].is_string() || !row.contains("directories") ||
                !row["directories"].is_array() || row["directories"].size() > 64 || !row.contains("names") ||
                !row["names"].is_array() || row["names"].size() > 128 || !row.contains("exists") ||
                !row["exists"].is_boolean()) throw std::runtime_error("invalid journal source set");
            JournalSourceSet source; source.kind = row["kind"].get<std::string>();
            source.exists = row["exists"].get<bool>();
            if (!source.exists && !row["names"].empty()) throw std::runtime_error("absent journal source has names");
            if (source.kind == "archive-empty" && (!source.exists || !row["names"].empty()))
                throw std::runtime_error("invalid empty archive source set");
            const auto root = row["root"].get<std::string>();
            if (root.empty() || root.size() > 32768 || root.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(root))
                throw std::runtime_error("invalid journal source root");
            source.rootSelector = fo4::io::from_utf8(root);
            if (!source.rootSelector.is_absolute() || source.rootSelector != source.rootSelector.lexically_normal())
                throw std::runtime_error("journal source root must be normalized");
            for (const auto& value : row["directories"]) {
                if (!value.is_string()) throw std::runtime_error("invalid journal source directory");
                auto part = value.get<std::string>();
                if (part.empty() || part.size() > 255 || part == "." || part == ".." ||
                    part.find_first_of("/\\:\0", 0, 4) != std::string::npos || !fo4::text::is_valid_utf8(part))
                    throw std::runtime_error("invalid journal source directory");
                source.directories.push_back(std::move(part));
            }
            std::set<std::string> folded;
            for (const auto& value : row["names"]) {
                if (!value.is_string()) throw std::runtime_error("invalid journal source name");
                auto name = value.get<std::string>();
                if (name.size() > 255 || name.find_first_of("/\\:\0", 0, 4) != std::string::npos ||
                    !fo4::text::is_valid_utf8(name) || !isJournalStringName(name, result.plugin) ||
                    !folded.insert(fo4::text::fold(name)).second) throw std::runtime_error("invalid journal source name");
                source.names.push_back(std::move(name));
            }
            std::sort(source.names.begin(), source.names.end());
            result.sources.push_back(std::move(source));
        }
    }
    if (document.contains("archives")) {
        if (!document["archives"].is_array() || document["archives"].size() > 16 ||
            !document["archiveOrder"].is_array() || document["archiveOrder"].size() > kJournalMaxArchiveProviders)
            throw std::runtime_error("invalid journal archive inventories");
        std::size_t providerCount = 0;
        std::uint64_t totalArchiveBytes = 0;
        for (const auto& row : document["archives"]) {
            if (!row.is_object() || row.size() != 3 || !row.contains("root") || !row.contains("directories") ||
                !row.contains("providers") || !row["providers"].is_array() ||
                row["providers"].size() > kJournalMaxArchiveProviders - providerCount)
                throw std::runtime_error("invalid journal archive inventory");
            const auto reference = archiveReference(nlohmann::json{{"root", row["root"]}, {"directories", row["directories"]}}, false);
            JournalArchiveInventory inventory{reference.rootSelector, reference.directories, {}};
            std::set<std::string> foldedNames;
            std::string previous;
            for (const auto& value : row["providers"]) {
                if (!value.is_object() || value.size() != 3 || !value.contains("filename") || !value["filename"].is_string() ||
                    !value.contains("size") || !value["size"].is_number_integer() ||
                    (!value["size"].is_number_unsigned() && value["size"].get<std::int64_t>() < 0) ||
                    !value.contains("sha256") || !value["sha256"].is_string())
                    throw std::runtime_error("invalid journal archive provider");
                JournalArchiveProvider provider{value["filename"].get<std::string>(), value["size"].get<std::uint64_t>(), value["sha256"].get<std::string>()};
                if (!archiveFilename(provider.filename) || !hex(provider.sha256, 64) || provider.size > kJournalMaxArchiveBytes ||
                    (!previous.empty() && provider.filename <= previous) ||
                    !foldedNames.insert(fo4::text::fold(provider.filename)).second ||
                    totalArchiveBytes > 4 * kJournalMaxArchiveBytes - provider.size)
                    throw std::runtime_error("invalid or oversized journal archive provider");
                totalArchiveBytes += provider.size; previous = provider.filename;
                inventory.providers.push_back(std::move(provider));
            }
            providerCount += inventory.providers.size();
            result.archives.push_back(std::move(inventory));
        }
        for (const auto& row : document["archiveOrder"]) {
            auto reference = archiveReference(row, true);
            if (std::any_of(result.archiveOrder.begin(), result.archiveOrder.end(), [&](const auto& existing) {
                return sameDestination(existing.rootSelector, reference.rootSelector) && existing.directories == reference.directories &&
                       fo4::text::equals_folded(existing.filename, reference.filename);
            })) throw std::runtime_error("duplicate journal archive provider order");
            result.archiveOrder.push_back(std::move(reference));
        }
    }
    bool hasPlugin = false;
    std::uint64_t total = 0;
    for (const auto& row : document["files"]) {
        if (!row.is_object() || row.size() < 8 || row.size() > 11 || !row.contains("kind") || !row["kind"].is_string() ||
            !row.contains("name") || !row["name"].is_string() || !row.contains("payload") || !row["payload"].is_string() ||
            !row.contains("originalExists") || !row["originalExists"].is_boolean() ||
            !row.contains("originalHash") || !row["originalHash"].is_string() ||
            !row.contains("originalSize") || !row["originalSize"].is_number_integer() ||
            (!row["originalSize"].is_number_unsigned() && row["originalSize"].get<std::int64_t>() < 0) ||
            !row.contains("finalHash") || !row["finalHash"].is_string() ||
            !row.contains("finalSize") || !row["finalSize"].is_number_integer() ||
            (row["finalSize"].is_number_integer() && !row["finalSize"].is_number_unsigned() && row["finalSize"].get<std::int64_t>() < 0)) {
            throw std::runtime_error("invalid journal entry");
        }
        JournalManifestEntry entry;
        entry.kind = row["kind"].get<std::string>();
        entry.name = row["name"].get<std::string>();
        entry.payload = row["payload"].get<std::string>();
        entry.originalExists = row["originalExists"].get<bool>();
        entry.originalHash = row["originalHash"].get<std::string>();
        entry.originalSize = row["originalSize"].get<std::uint64_t>();
        entry.finalHash = row["finalHash"].get<std::string>();
        entry.finalSize = row["finalSize"].get<std::uint64_t>();
        if (row.contains("verifyOnly")) {
            if (!row["verifyOnly"].is_boolean() || !row["verifyOnly"].get<bool>() || entry.finalSize != 0)
                throw std::runtime_error("invalid journal precondition");
            entry.verifyOnly = true;
        }
        const auto expectedFields = 8u + (entry.verifyOnly ? 1u : 0u) + (row.contains("root") ? 2u : 0u);
        if (row.size() != expectedFields) throw std::runtime_error("invalid journal entry fields");
        if (row.contains("root")) {
            if (!row.contains("root") || !row["root"].is_string() ||
                !row.contains("directories") || !row["directories"].is_array() || row["directories"].size() > 64)
                throw std::runtime_error("invalid journal root selector");
            const auto root = row["root"].get<std::string>();
            if (root.empty() || root.size() > 32768 || root.find('\0') != std::string::npos || !fo4::text::is_valid_utf8(root))
                throw std::runtime_error("invalid journal root selector");
            entry.rootSelector = fo4::io::from_utf8(root);
            if (!entry.rootSelector.is_absolute() || entry.rootSelector != entry.rootSelector.lexically_normal())
                throw std::runtime_error("journal root selector must be normalized");
            for (const auto& value : row["directories"]) {
                if (!value.is_string()) throw std::runtime_error("invalid journal directory");
                auto part = value.get<std::string>();
                if (part.empty() || part.size() > 255 || part == "." || part == ".." ||
                    part.find_first_of("/\\:\0", 0, 4) != std::string::npos || !fo4::text::is_valid_utf8(part))
                    throw std::runtime_error("invalid journal directory");
                entry.directories.push_back(std::move(part));
            }
        }

        if (entry.originalSize > kJournalMaxFileBytes || entry.finalSize > kJournalMaxFileBytes ||
            total > kJournalMaxTotalBytes - entry.originalSize ||
            total + entry.originalSize > kJournalMaxTotalBytes - entry.finalSize) {
            throw std::runtime_error("journal transaction is too large");
        }
        total += entry.originalSize + entry.finalSize;
        const bool pluginEntry = entry.kind == "plugin" && entry.name.empty();
        const bool tableEntry = (entry.kind == "strings" || entry.kind == "dlstrings" || entry.kind == "ilstrings") && token(entry.name, false);
        const bool scriptEntry = (entry.kind == "psc" || entry.kind == "pex") && scriptName(entry.name);
        if ((!pluginEntry && !tableEntry && !scriptEntry) || (pluginEntry && (!entry.originalExists || hasPlugin || !entry.rootSelector.empty() || entry.verifyOnly)) ||
            entry.payload != result.transaction + "_" + std::to_string(result.files.size()) + ".payload" ||
            !hex(entry.finalHash, 64) || (entry.originalExists ? !hex(entry.originalHash, 64) :
                (!entry.originalHash.empty() || entry.originalSize != 0))) {
            throw std::runtime_error("invalid journal destination or digest");
        }
        hasPlugin = hasPlugin || pluginEntry;
        result.files.push_back(std::move(entry));
    }
    if (!hasPlugin) throw std::runtime_error("journal plugin destination is missing");
    return result;
}

std::vector<std::filesystem::path> resolveJournalTargets(const JournalManifest& manifest,
                                                        const JournalRoots& roots) {
#if defined(_WIN32)
    const auto plugin = WindowsJournalSessionState::pluginIdentity(roots);
    if (!WindowsJournalSessionState::samePath(plugin, manifest.plugin)) throw std::runtime_error("journal plugin identity is not authorized");
#else
    const auto plugin = journalIdentity(roots.plugin);
    const auto recorded = journalIdentity(manifest.plugin);
    if (!sameJournalIdentity(plugin, recorded)) throw std::runtime_error("journal plugin identity is not authorized");
#endif
    auto extension = fo4::io::utf8(plugin.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension != ".esp" && extension != ".esm" && extension != ".esl") {
        throw std::runtime_error("journal plugin has an invalid extension");
    }
    std::vector<std::filesystem::path> targets;
    targets.reserve(manifest.files.size());
    const auto append = [&](const std::filesystem::path& target) {
        const auto normalized = target.lexically_normal();
        if (std::any_of(targets.begin(), targets.end(), [&](const auto& existing) {
                return sameDestination(existing, normalized);
            })) {
            throw std::runtime_error("journal destinations overlap");
        }
        targets.push_back(normalized);
    };
    bool hasPlugin = false;
    for (const auto& entry : manifest.files) {
        if (entry.kind == "plugin" && entry.name.empty()) {
            if (hasPlugin) throw std::runtime_error("duplicate journal plugin destination");
            hasPlugin = true;
            append(plugin);
            continue;
        }
        if (!entry.rootSelector.empty()) {
            const auto& approved = approvedJournalRoot(entry, roots);
            auto directory = approved.path;
            for (const auto& part : entry.directories) directory /= fo4::io::from_utf8(part);
            if (entry.kind == "strings" || entry.kind == "dlstrings" || entry.kind == "ilstrings") {
                const auto tableExtension = entry.kind == "strings" ? ".STRINGS" : entry.kind == "dlstrings" ? ".DLSTRINGS" : ".ILSTRINGS";
                append(directory / fo4::io::from_utf8(fo4::io::utf8(plugin.stem()) + "_" + entry.name + tableExtension));
            } else if (entry.kind == "psc" || entry.kind == "pex") {
                std::size_t begin = 0;
                for (;;) {
                    const auto end = entry.name.find(':', begin);
                    if (end == std::string::npos) { directory /= entry.name.substr(begin) + (entry.kind == "psc" ? ".psc" : ".pex"); break; }
                    directory /= entry.name.substr(begin, end - begin);
                    begin = end + 1;
                }
                append(directory);
            } else throw std::runtime_error("invalid selected journal destination");
            continue;
        }
        if ((entry.kind == "strings" || entry.kind == "dlstrings" || entry.kind == "ilstrings") &&
            token(entry.name, false) && roots.stringsDirectory.has_value()) {
            const auto directory = approvedDirectory(*roots.stringsDirectory);
            const auto tableExtension = entry.kind == "strings" ? ".STRINGS" :
                entry.kind == "dlstrings" ? ".DLSTRINGS" : ".ILSTRINGS";
            const auto fileName = fo4::io::utf8(plugin.stem()) + "_" + entry.name + tableExtension;
            const auto target = directory / fo4::io::from_utf8(fileName);
            requireSafeTarget(target);
            append(target);
            continue;
        }
        const bool source = entry.kind == "psc";
        const bool compiled = entry.kind == "pex";
        const auto& configured = source ? roots.scriptSourceDirectory : roots.scriptCompiledDirectory;
        if ((!source && !compiled) || !scriptName(entry.name) || !configured.has_value()) {
            throw std::runtime_error("journal destination kind is not authorized");
        }
        auto directory = approvedDirectory(*configured);
        bool missing = false;
        std::size_t begin = 0;
        for (;;) {
            const auto end = entry.name.find(':', begin);
            const auto part = entry.name.substr(begin, end == std::string::npos ? end : end - begin);
            if (end == std::string::npos) {
                const auto target = directory / (part + (source ? ".psc" : ".pex"));
                if (!missing) requireSafeTarget(target);
                append(target);
                break;
            }
            directory /= part;
            if (!missing) {
                std::error_code error;
                const auto status = std::filesystem::symlink_status(directory, error);
                if (error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status))) {
                    missing = true;
                } else if (error || reparse(directory) || !std::filesystem::is_directory(status)) {
                    throw std::runtime_error("journal script namespace directory is unsafe");
                }
            }
            begin = end + 1;
        }
    }
    if (!hasPlugin) throw std::runtime_error("journal plugin destination is missing");
    return targets;
}

}
