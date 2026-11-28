#include "RadiclePluginJournalSession.hpp"
#include "RadiclePluginJournalDestination.hpp"
#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginWindowsJournalSession.hpp"

#include <fo4/archive/ba2_reader.hpp>
#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/text/encoding.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4ide::plugin::detail {
#ifdef FO4IDE_TEST_HOOKS
namespace { JournalCommitHook commitHook = nullptr; }
void setJournalCommitHook(JournalCommitHook hook) { commitHook = hook; }
#endif
#ifdef _WIN32
namespace { WindowsJournalSessionHook windowsHook = nullptr; }
#ifdef FO4IDE_TEST_HOOKS
void setWindowsJournalSessionHook(WindowsJournalSessionHook hook) { windowsHook = hook; }
#endif
#endif
namespace {

bool journalSuffix(std::string_view name, std::string_view suffix) {
#ifdef _WIN32
    return name.size() >= suffix.size() && std::equal(suffix.begin(), suffix.end(), name.end() - suffix.size(),
        [](unsigned char expected, unsigned char actual) { return expected == std::tolower(actual); });
#else
    return name.ends_with(suffix);
#endif
}

std::unique_ptr<JournalDestination> destination(const JournalManifestEntry& entry,
                                                const JournalRoots& roots,
                                                const std::filesystem::path& resolved, bool create = false) {
    if (!entry.rootSelector.empty()) {
        const auto& approved = approvedJournalRoot(entry, roots);
        auto directories = entry.directories;
        if (entry.kind == "psc" || entry.kind == "pex") {
            std::size_t begin = 0;
            for (;;) {
                const auto end = entry.name.find(':', begin);
                if (end == std::string::npos) break;
                directories.push_back(entry.name.substr(begin, end - begin));
                begin = end + 1;
            }
        }
        return std::make_unique<JournalDestination>(approved.path, directories,
            fo4::io::utf8(resolved.filename()), create, approved.identity);
    }
    if (entry.kind == "psc" || entry.kind == "pex") {
        const auto& base = entry.kind == "psc" ? roots.scriptSourceDirectory : roots.scriptCompiledDirectory;
        const auto& identity = entry.kind == "psc" ? roots.scriptSourceDirectoryIdentity : roots.scriptCompiledDirectoryIdentity;
        if (!base || !identity) throw std::runtime_error("journal script root is not authorized");
        std::vector<std::string> directories;
        std::size_t begin = 0;
        for (;;) {
            const auto end = entry.name.find(':', begin);
            if (end == std::string::npos) break;
            directories.push_back(entry.name.substr(begin, end - begin));
            begin = end + 1;
        }
        const auto file = entry.name.substr(begin) + (entry.kind == "psc" ? ".psc" : ".pex");
        return std::make_unique<JournalDestination>(*base, directories, file, true, identity);
    }
    const auto& identity = entry.kind == "plugin" ? roots.pluginDirectoryIdentity : roots.stringsDirectoryIdentity;
    if (!identity) throw std::runtime_error("journal destination root is not authorized");
#if defined(_WIN32)
    if (entry.kind == "plugin") return std::make_unique<JournalDestination>(roots.plugin.parent_path(), std::vector<std::string>{},
        fo4::io::utf8(roots.plugin.filename()), false, identity);
#endif
    return std::make_unique<JournalDestination>(resolved.parent_path(), std::vector<std::string>{},
                                                fo4::io::utf8(resolved.filename()), false, identity);
}

bool matches(const std::optional<std::vector<std::byte>>& bytes, bool exists,
             std::uint64_t size, std::string_view hash) {
    if (bytes.has_value() != exists) return false;
    return !bytes || (bytes->size() == size && fo4::io::sha256_hex(*bytes) == hash);
}

std::vector<std::byte> payload(const JournalRoot& root, const JournalManifestEntry& entry) {
    auto bytes = root.readFile(entry.payload, static_cast<std::size_t>(entry.finalSize));
    if (bytes.size() != entry.finalSize || fo4::io::sha256_hex(bytes) != entry.finalHash) {
        throw std::runtime_error("journal payload does not match its manifest");
    }
    return bytes;
}

bool orphanName(std::string_view name) {
    if (name.size() < 36) return false;
    for (std::size_t i = 0; i < 32; ++i) {
        const char ch = name[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    if (name.substr(32) == ".tmp" || name.substr(32) == ".authority") return true;
    if (name[32] != '_' || !name.ends_with(".payload")) return false;
    const auto number = name.substr(33, name.size() - 33 - 8);
    if (number.empty() || number.size() > 3 || (number.size() > 1 && number.front() == '0') ||
        !std::all_of(number.begin(), number.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; })) return false;
    unsigned index = 0;
    for (const char ch : number) index = index * 10 + static_cast<unsigned>(ch - '0');
    return index < 128;
}

bool ownsManifest(const JournalRoot& root, std::string_view name, const JournalRoots& roots) {
    try {
        const auto bytes = root.readFile(name, kJournalMaxManifestBytes);
        const auto manifest = decodeJournalManifest(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
#if defined(_WIN32)
        return WindowsJournalSessionState::samePath(manifest.plugin, WindowsJournalSessionState::pluginIdentity(roots));
#else
        return sameJournalIdentity(journalIdentity(manifest.plugin), journalIdentity(roots.plugin));
#endif
    } catch (const std::exception& error) {
        throw JournalRecoveryRequired("journal manifest cannot be attributed to a plugin: " + std::string(error.what()));
    }
}

#if defined(_WIN32)
bool ownsCompletion(const JournalRoot& root, std::string_view name, const JournalRoots& roots) {
    try {
        WindowsJournalDirectory journal(root.nativeHandle());
        const auto document = WindowsJournalSessionState::completionDocument(journal, name);
        const auto manifest = decodeJournalManifest(document);
        return WindowsJournalSessionState::samePath(manifest.plugin, WindowsJournalSessionState::pluginIdentity(roots));
    } catch (const std::exception& error) {
        throw JournalRecoveryRequired("journal completion cannot be attributed to a plugin: " + std::string(error.what()));
    }
}
#endif

void cleanupUnpublished(JournalRoot& root) {
    const auto files = root.listFiles(4096);
    if (std::any_of(files.begin(), files.end(), [](const auto& name) { return journalSuffix(name, ".manifest") || journalSuffix(name, ".complete"); })) return;
    for (const auto& name : files) if (orphanName(name)) root.removeFile(name);
}

std::string transactionId() {
    std::random_device random;
    static constexpr char digits[] = "0123456789abcdef";
    std::string id;
    id.reserve(32);
    for (int index = 0; index < 32; ++index) id.push_back(digits[random() & 15u]);
    return id;
}

}

JournalSession::JournalSession(const std::filesystem::path& root)
    : root_(root), lock_(root_, "global.lock") {}

bool journalHasRecoveryFiles(const std::filesystem::path& root) {
    std::error_code ec;
    std::filesystem::directory_iterator entries(root, ec);
    if (ec) return ec != std::errc::no_such_file_or_directory;
    for (const std::filesystem::directory_iterator end; entries != end && !ec; entries.increment(ec)) {
        const auto name = fo4::io::utf8(entries->path().filename());
        if (journalSuffix(name, ".manifest") || journalSuffix(name, ".complete")) return true;
    }
    return static_cast<bool>(ec);
}

namespace {
bool sameArchiveReference(const JournalArchiveReference& first, const JournalArchiveReference& second) {
    return sameJournalIdentity(first.rootSelector, second.rootSelector) &&
           first.directories == second.directories && first.filename == second.filename;
}

void validateArchiveInventories(const JournalManifest& manifest, const JournalRoots& roots) {
    if (manifest.archives.size() != roots.archiveDirectories.size() ||
        manifest.archiveOrder.size() != roots.archiveProviderOrder.size())
        throw std::runtime_error("journal archive provider configuration changed");
    for (std::size_t index = 0; index < manifest.archiveOrder.size(); ++index) {
        if (!sameArchiveReference(manifest.archiveOrder[index], roots.archiveProviderOrder[index]))
            throw std::runtime_error("journal archive provider order changed");
        for (std::size_t previous = 0; previous < index; ++previous)
            if (sameArchiveReference(manifest.archiveOrder[index], manifest.archiveOrder[previous]))
                throw std::runtime_error("duplicate journal archive provider");
    }
    for (std::size_t index = 0; index < manifest.archives.size(); ++index) {
        const auto& inventory = manifest.archives[index];
        const JournalArchiveReference current{inventory.rootSelector, inventory.directories, {}};
        if (!sameArchiveReference(current, roots.archiveDirectories[index]))
            throw std::runtime_error("journal archive directory authorization changed");
        for (std::size_t previous = 0; previous < index; ++previous) {
            const auto& earlier = manifest.archives[previous];
            if (sameArchiveReference(current, {earlier.rootSelector, earlier.directories, {}}))
                throw std::runtime_error("duplicate journal archive directory");
        }
        const JournalApprovedReadRoot* approved = nullptr;
        for (const auto& root : roots.approvedReadRoots) {
            if (!sameJournalIdentity(root.path, inventory.rootSelector)) continue;
            if (approved) throw std::runtime_error("ambiguous journal archive read root");
            approved = &root;
        }
        if (!approved || !approved->lease || !sameJournalIdentity(approved->path, approved->lease->path()))
            throw std::runtime_error("journal archive read root is not authorized");
        std::filesystem::path relative;
        for (const auto& component : inventory.directories) relative /= fo4::io::from_utf8(component);
        auto names = approved->lease->list(relative, 4096);
        std::erase_if(names, [](const auto& name) { return !fo4::text::fold(name).ends_with(".ba2"); });
        if (names.size() != inventory.providers.size()) throw std::runtime_error("journal archive inventory changed");
        for (std::size_t entry = 0; entry < names.size(); ++entry) {
            const auto& provider = inventory.providers[entry];
            if (names[entry] != provider.filename) throw std::runtime_error("journal archive inventory changed");
            auto source = approved->lease->open(relative / fo4::io::from_utf8(provider.filename), kJournalMaxArchiveBytes);
            if (source->size() != provider.size) throw std::runtime_error("journal archive size changed");
            const auto snapshot = fo4::archive::Ba2Snapshot::open(std::move(source));
            if (!snapshot.has_name_table() || snapshot.archive_sha256() != provider.sha256)
                throw std::runtime_error("journal archive content changed or cannot be enumerated");
        }
    }
    for (const auto& reference : manifest.archiveOrder) {
        const auto found = std::any_of(manifest.archives.begin(), manifest.archives.end(), [&](const auto& inventory) {
            if (!sameJournalIdentity(inventory.rootSelector, reference.rootSelector) || inventory.directories != reference.directories) return false;
            return std::any_of(inventory.providers.begin(), inventory.providers.end(), [&](const auto& provider) { return provider.filename == reference.filename; });
        });
        if (!found) throw std::runtime_error("journal archive provider is outside its inventory");
    }
}

void validateReadStringSources(const JournalManifest& manifest, const JournalRoots& roots,
                               const std::vector<std::filesystem::path>& targets) {
    if (manifest.version != 3) return;
    if (manifest.readStrings.size() != roots.readStringDirectories.size())
        throw std::runtime_error("journal read-only string directories changed");
    for (std::size_t i = 0; i < manifest.readStrings.size(); ++i) {
        const auto& inventory = manifest.readStrings[i];
        if (!sameArchiveReference({inventory.rootSelector, inventory.directories, {}}, roots.readStringDirectories[i]))
            throw std::runtime_error("journal read-only string directory is not authorized");
    }
    auto expected = manifest.readStrings;
    auto current = captureJournalReadStringInventories(roots.readStringDirectories, roots.approvedReadRoots, manifest.plugin);
    static_cast<void>(encodeJournalReadStringInventories(expected, manifest.plugin));
    static_cast<void>(encodeJournalReadStringInventories(current, manifest.plugin));
    for (std::size_t i = 0; i < expected.size(); ++i) {
        auto directory = expected[i].rootSelector;
        for (const auto& part : expected[i].directories) directory /= fo4::io::from_utf8(part);
        for (std::size_t j = 0; j < manifest.files.size(); ++j) {
            const auto& entry = manifest.files[j];
            if (entry.kind != "strings" && entry.kind != "dlstrings" && entry.kind != "ilstrings") continue;
            if (!sameJournalIdentity(directory, targets[j].parent_path())) continue;
            if (!expected[i].exists && !entry.originalExists && !entry.verifyOnly) expected[i].exists = current[i].exists;
            const auto isTarget = [&](const JournalReadStringFile& file) {
                return sameJournalIdentity(directory / fo4::io::from_utf8(file.filename), targets[j]);
            };
            const auto original = std::find_if(expected[i].files.begin(), expected[i].files.end(), isTarget);
            if ((original != expected[i].files.end()) != entry.originalExists ||
                (original != expected[i].files.end() &&
                 (original->size != entry.originalSize || original->sha256 != entry.originalHash)))
                throw std::runtime_error("journal read inventory disagrees with destination baseline");
            std::erase_if(expected[i].files, isTarget);
            std::erase_if(current[i].files, isTarget);
        }
    }
    if (encodeJournalReadStringInventories(current, manifest.plugin) !=
        encodeJournalReadStringInventories(expected, manifest.plugin))
        throw std::runtime_error("loose string inventory changed");
}

void validateSourceSets(const JournalManifest& manifest, const JournalRoots& roots,
                        const std::vector<std::filesystem::path>& targets) {
    for (const auto& source : manifest.sources) {
        JournalManifestEntry selector; selector.rootSelector = source.rootSelector;
        const auto& approved = approvedJournalRoot(selector, roots);
        auto directory = approved.path;
        for (const auto& part : source.directories) directory /= fo4::io::from_utf8(part);
        const auto createsDirectory = [&] {
            for (std::size_t index = 0; index < targets.size(); ++index) {
                const auto& entry = manifest.files[index];
                if (!entry.originalExists && !entry.verifyOnly && targets[index].parent_path() == directory &&
                    (entry.kind == "strings" || entry.kind == "dlstrings" || entry.kind == "ilstrings")) return true;
            }
            return false;
        };
        std::vector<std::string> names;
        try { names = JournalDestination::listDirectory(approved.path, source.directories, approved.identity); }
        catch (const JournalDirectoryAbsent&) { if (!source.exists) continue; throw; }
        if (!source.exists && !createsDirectory()) throw std::runtime_error("journal source directory appeared");
        if (source.kind == "archive-empty") {
            for (auto name : names) {
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name.ends_with(".ba2")) throw std::runtime_error("journal archive provider set changed");
            }
            continue;
        }
        std::erase_if(names, [&](const auto& name) { return !isJournalStringName(name, manifest.plugin); });
        for (std::size_t index = 0; index < targets.size(); ++index) {
            const auto& entry = manifest.files[index];
            if (entry.originalExists || entry.verifyOnly || targets[index].parent_path() != directory ||
                (entry.kind != "strings" && entry.kind != "dlstrings" && entry.kind != "ilstrings")) continue;
            const auto name = fo4::io::utf8(targets[index].filename());
            if (std::find(source.names.begin(), source.names.end(), name) != source.names.end())
                throw std::runtime_error("journal absent destination contradicts source set");
            std::erase(names, name);
        }
        std::sort(names.begin(), names.end());
        if (names != source.names) throw std::runtime_error("journal source set changed");
        for (const auto& name : source.names) {
            const auto path = (directory / fo4::io::from_utf8(name)).lexically_normal();
            if (std::find(targets.begin(), targets.end(), path) == targets.end())
                throw std::runtime_error("journal source set has an unverified member");
        }
    }
}
}

void JournalSession::publish(const JournalRoots& roots, const std::vector<JournalInput>& files,
                             const std::vector<JournalSourceSet>& sources,
                             const std::vector<JournalArchiveInventory>& archives,
                             const std::vector<JournalReadStringInventory>& readStrings) {
    if (files.empty() || files.size() > 128) throw std::runtime_error("invalid journal transaction size");
    if (archives.size() > 16 || roots.archiveDirectories.size() > 16 || roots.archiveProviderOrder.size() > kJournalMaxArchiveProviders)
        throw std::runtime_error("journal archive inventory is too large");
    std::size_t archiveCount = 0;
    std::uint64_t archiveBytes = 0;
    for (const auto& inventory : archives) {
        if (inventory.rootSelector.native().size() > 32768 || inventory.directories.size() > 64 ||
            inventory.providers.size() > kJournalMaxArchiveProviders - archiveCount)
            throw std::runtime_error("journal archive inventory is too large");
        archiveCount += inventory.providers.size();
        for (const auto& directory : inventory.directories) if (directory.size() > 255)
            throw std::runtime_error("journal archive directory is too long");
        for (const auto& provider : inventory.providers) {
            if (provider.filename.size() > 255 || provider.sha256.size() != 64 || provider.size > kJournalMaxArchiveBytes ||
                archiveBytes > 4 * kJournalMaxArchiveBytes - provider.size)
                throw std::runtime_error("journal archive provider is invalid or too large");
            archiveBytes += provider.size;
        }
    }
    for (const auto& provider : roots.archiveProviderOrder) {
        if (provider.rootSelector.native().size() > 32768 || provider.directories.size() > 64 || provider.filename.size() > 255)
            throw std::runtime_error("journal archive provider order is too large");
        for (const auto& directory : provider.directories) if (directory.size() > 255)
            throw std::runtime_error("journal archive directory is too long");
    }
    std::uint64_t total = 0;
    for (const auto& file : files) {
        if (file.originalSize > kJournalMaxFileBytes || file.finalBytes.size() > kJournalMaxFileBytes ||
            total > kJournalMaxTotalBytes - file.originalSize ||
            total + file.originalSize > kJournalMaxTotalBytes - file.finalBytes.size()) {
            throw std::runtime_error("journal transaction is too large");
        }
        total += file.originalSize + file.finalBytes.size();
    }
    cleanupUnpublished(root_);
#if defined(_WIN32)
    const auto existingFiles = root_.listFiles(kJournalMaxCleanupEntries);
#else
    const auto existingFiles = root_.listFiles();
#endif
    for (const auto& name : existingFiles) {
        if (journalSuffix(name, ".manifest") && ownsManifest(root_, name, roots)) throw JournalRecoveryRequired("a journal transaction requires recovery");
#if defined(_WIN32)
        if (journalSuffix(name, ".complete") && ownsCompletion(root_, name, roots)) throw JournalRecoveryRequired("a completed Windows transaction requires cleanup");
#endif
    }
#if defined(_WIN32)
    if (std::count_if(existingFiles.begin(), existingFiles.end(), [](const auto& name) { return journalSuffix(name, ".authority"); }) >= 8 ||
        existingFiles.size() + 19 * files.size() + 4 > kJournalMaxCleanupEntries)
        throw JournalRecoveryRequired("uncommitted Windows journal files require manual attention");
#endif
    const auto id = transactionId();
    const auto plugin = journalIdentity(roots.plugin);
    nlohmann::json rows = nlohmann::json::array();
    for (std::size_t index = 0; index < files.size(); ++index) {
        const auto& file = files[index];
        nlohmann::json row{{"kind", file.kind}, {"name", file.name},
            {"payload", id + "_" + std::to_string(index) + ".payload"},
            {"originalExists", file.originalExists}, {"originalHash", file.originalHash},
            {"originalSize", file.originalSize}, {"finalHash", fo4::io::sha256_hex(file.finalBytes)},
            {"finalSize", file.finalBytes.size()}};
        if (file.verifyOnly) {
            if (!file.finalBytes.empty()) throw std::runtime_error("verify-only entry cannot include final bytes");
            row["verifyOnly"] = true;
        }
        if (!file.rootSelector.empty()) {
            row["root"] = fo4::io::utf8(file.rootSelector);
            row["directories"] = file.directories;
        } else if (!file.directories.empty()) {
            throw std::runtime_error("journal directories require a root selector");
        }
        rows.push_back(std::move(row));
    }
    const bool hasReadStrings = !readStrings.empty() || !roots.readStringDirectories.empty();
    const bool hasArchives = hasReadStrings || !archives.empty() || !roots.archiveDirectories.empty() || !roots.archiveProviderOrder.empty();
    nlohmann::json record{{"version", hasReadStrings ? 3 : hasArchives ? 2 : 1}, {"transaction", id},
        {"plugin", fo4::io::utf8(plugin)}, {"files", std::move(rows)}};
    if (!sources.empty()) {
        record["sources"] = nlohmann::json::array();
        for (const auto& source : sources) record["sources"].push_back({{"kind", source.kind},
            {"root", fo4::io::utf8(source.rootSelector)}, {"directories", source.directories},
            {"names", source.names}, {"exists", source.exists}});
    }
    if (hasArchives) {
        record["archives"] = nlohmann::json::array();
        for (const auto& inventory : archives) {
            nlohmann::json providers = nlohmann::json::array();
            for (const auto& provider : inventory.providers)
                providers.push_back({{"filename", provider.filename}, {"size", provider.size}, {"sha256", provider.sha256}});
            record["archives"].push_back({{"root", fo4::io::utf8(inventory.rootSelector)},
                {"directories", inventory.directories}, {"providers", std::move(providers)}});
        }
        record["archiveOrder"] = nlohmann::json::array();
        for (const auto& provider : roots.archiveProviderOrder) record["archiveOrder"].push_back({
            {"root", fo4::io::utf8(provider.rootSelector)}, {"directories", provider.directories}, {"filename", provider.filename}});
    }
    if (hasReadStrings) record["readStrings"] = nlohmann::json::parse(encodeJournalReadStringInventories(readStrings, plugin));
    const auto document = record.dump();
    const auto manifest = decodeJournalManifest(document);
    const auto paths = resolveJournalTargets(manifest, roots);
    validateSourceSets(manifest, roots, paths);
    validateArchiveInventories(manifest, roots);
    validateReadStringSources(manifest, roots, paths);
    std::vector<std::unique_ptr<JournalDestination>> targets;
    targets.reserve(files.size());
    for (std::size_t index = 0; index < files.size(); ++index) {
        const auto& entry = manifest.files[index];
        std::unique_ptr<JournalDestination> target;
        try {
            target = destination(entry, roots, paths[index]);
        } catch (const JournalDirectoryAbsent&) {
            if (entry.originalExists || entry.rootSelector.empty()) throw;
            targets.push_back(nullptr);
            continue;
        }
        const auto current = target->read(static_cast<std::size_t>(entry.originalSize));
        if (!matches(current, entry.originalExists, entry.originalSize, entry.originalHash)) {
            throw std::runtime_error("journal destination changed since it was staged");
        }
        targets.push_back(std::move(target));
    }
#if defined(_WIN32)
    WindowsJournalDirectory windowsJournal(root_.nativeHandle());
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (targets[index]) continue;
        const auto& entry = manifest.files[index];
        if (entry.kind != "strings" && entry.kind != "dlstrings" && entry.kind != "ilstrings")
            throw std::runtime_error("journal destination directory is absent");
        targets[index] = destination(entry, roots, paths[index], true);
    }
    const auto authority = WindowsJournalSessionState::authorize(windowsJournal, document, manifest, paths, targets);
    if (windowsHook) windowsHook(WindowsJournalSessionStep::preflightComplete, 0, {}, {});
#endif
    std::vector<std::string> written;
    bool publicationAttempted = false;
    try {
        for (std::size_t index = 0; index < files.size(); ++index) {
            root_.writeFile(manifest.files[index].payload, files[index].finalBytes);
            written.push_back(manifest.files[index].payload);
        }
        const auto temporary = id + ".tmp";
#if defined(_WIN32)
        root_.writeFile(id + ".authority", std::as_bytes(std::span(authority.data(), authority.size())));
        written.push_back(id + ".authority");
#endif
        root_.writeFile(temporary, std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(document.data()), document.size()));
        written.push_back(temporary);
        publicationAttempted = true;
        root_.publishFile(temporary, id + ".manifest");
#ifdef FO4IDE_TEST_HOOKS
        if (commitHook) commitHook(0);
#endif
#if defined(_WIN32)
        targets.clear();
        if (windowsHook) windowsHook(WindowsJournalSessionStep::prepared, 0, {}, {});
#endif
        replay(id + ".manifest", roots);
    } catch (...) {
        const auto failure = std::current_exception();
        if (publicationAttempted) {
            try { std::rethrow_exception(failure); }
            catch (const std::exception& error) { throw JournalRecoveryRequired("journal publication or recovery requires attention: " + std::string(error.what())); }
            catch (...) { throw JournalRecoveryRequired("journal publication or recovery requires attention"); }
        }
        try {
            for (auto it = written.rbegin(); it != written.rend(); ++it) root_.removeFile(*it);
        } catch (const std::exception& error) {
            throw std::runtime_error("could not clean unpublished journal files: " + std::string(error.what()));
        }
        std::rethrow_exception(failure);
    }
}

bool JournalSession::recover(const std::vector<JournalRoots>& approved, const JournalPrepareRecovery& prepare) {
    try {
        cleanupUnpublished(root_);
        std::vector<std::string> pending;
        const auto ownedByApproved = [&](const std::string& name) {
            return std::any_of(approved.begin(), approved.end(), [&](const JournalRoots& roots) {
#if defined(_WIN32)
                if (journalSuffix(name, ".complete")) return ownsCompletion(root_, name, roots);
#endif
                return ownsManifest(root_, name, roots);
            });
        };
#if defined(_WIN32)
        std::vector<std::string> completions;
        for (const auto& name : root_.listFiles(kJournalMaxCleanupEntries)) {
            if (journalSuffix(name, ".manifest") && ownedByApproved(name)) pending.push_back(name);
            if (journalSuffix(name, ".complete") && ownedByApproved(name)) completions.push_back(name);
        }
        if (pending.empty() && completions.empty()) return false;
        if (pending.size() > 1 || completions.size() > 1) throw std::runtime_error("multiple Windows transactions require manual attention");
        const bool completionOnly = pending.empty();
        WindowsJournalDirectory windowsJournal(root_.nativeHandle());
        const auto recoveredDocument = completionOnly ? WindowsJournalSessionState::completionDocument(windowsJournal, completions.front()) : std::string{};
        const auto bytes = completionOnly ? std::vector<std::byte>(reinterpret_cast<const std::byte*>(recoveredDocument.data()),
            reinterpret_cast<const std::byte*>(recoveredDocument.data() + recoveredDocument.size())) : root_.readFile(pending.front(), kJournalMaxManifestBytes);
#else
        for (const auto& name : root_.listFiles()) {
            if (journalSuffix(name, ".manifest") && ownedByApproved(name)) pending.push_back(name);
        }
        if (pending.empty()) return false;
        if (pending.size() != 1) throw std::runtime_error("multiple journal transactions require manual attention");
        const auto bytes = root_.readFile(pending.front(), kJournalMaxManifestBytes);
#endif
        if (bytes.empty()) throw std::runtime_error("empty journal manifest");
        const auto manifest = decodeJournalManifest(std::string_view(
            reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (!pending.empty() && pending.front() != manifest.transaction + ".manifest") {
            throw std::runtime_error("journal manifest name does not match its transaction");
        }
#if defined(_WIN32)
        if (!completions.empty() && completions.front() != manifest.transaction + ".complete") throw std::runtime_error("Windows completion transaction is ambiguous");
        const auto recorded = manifest.plugin;
#else
        const auto recorded = journalIdentity(manifest.plugin);
#endif
        const JournalRoots* selected = nullptr;
        for (const auto& roots : approved) {
#if defined(_WIN32)
            if (!WindowsJournalSessionState::samePath(recorded, WindowsJournalSessionState::pluginIdentity(roots))) continue;
#else
            if (!sameJournalIdentity(recorded, journalIdentity(roots.plugin))) continue;
#endif
            if (selected != nullptr) throw std::runtime_error("journal plugin authorization is ambiguous");
            selected = &roots;
        }
        if (selected == nullptr) return false;
#if defined(_WIN32)
        if (completionOnly) {
            const auto paths = resolveJournalTargets(manifest, *selected);
            WindowsJournalSessionState::Targets targets;
            for (std::size_t index = 0; index < paths.size(); ++index) targets.push_back(destination(manifest.files[index], *selected, paths[index]));
            const auto revalidate = [&] {
                if (!WindowsJournalSessionState::cleanup(windowsJournal, recoveredDocument, manifest, paths, targets, nullptr, {}, true))
                    throw std::runtime_error("Windows completion evidence disappeared");
            };
            revalidate();
            WindowsJournalSessionState::cleanup(windowsJournal, recoveredDocument, manifest, paths, targets, windowsHook, revalidate);
            return true;
        }
#endif
        auto prepared = *selected;
        std::vector<JournalPeerSnapshot> peers;
        if (prepare) {
            const auto plugin = std::find_if(manifest.files.begin(), manifest.files.end(), [](const auto& entry) { return entry.kind == "plugin"; });
            if (plugin == manifest.files.end()) throw std::runtime_error("journal has no plugin payload");
            const auto finalPlugin = payload(root_, *plugin);
            JournalReadConfiguration readConfig{prepared.approvedReadRoots, prepared.archiveDirectories, prepared.archiveProviderOrder, {}, {}};
            readConfig.readStringDirectories = prepared.readStringDirectories;
            prepare(manifest, finalPlugin, readConfig);
            prepared.approvedReadRoots = std::move(readConfig.approvedReadRoots);
            prepared.archiveDirectories = std::move(readConfig.archiveDirectories);
            prepared.archiveProviderOrder = std::move(readConfig.archiveProviderOrder);
            prepared.readStringDirectories = std::move(readConfig.readStringDirectories);
            peers = std::move(readConfig.peers);
        }
        replay(pending.front(), prepared, peers);
        return true;
    } catch (const JournalRecoveryRequired&) {
        throw;
    } catch (const std::exception& error) {
        throw JournalRecoveryRequired(error.what());
    }
}

void JournalSession::replay(std::string_view manifestName, const JournalRoots& roots,
                            const std::vector<JournalPeerSnapshot>& peers) {
    const auto bytes = root_.readFile(manifestName, kJournalMaxManifestBytes);
    if (bytes.empty()) throw std::runtime_error("empty journal manifest");
    const auto manifest = decodeJournalManifest(std::string_view(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (manifestName != manifest.transaction + ".manifest") {
        throw std::runtime_error("journal manifest name does not match its transaction");
    }
    const auto paths = resolveJournalTargets(manifest, roots);
    validateSourceSets(manifest, roots, paths);
    validateArchiveInventories(manifest, roots);
    validateReadStringSources(manifest, roots, paths);
    std::vector<std::unique_ptr<JournalDestination>> targets;
    std::vector<bool> needsWrite;
    targets.reserve(manifest.files.size());
    needsWrite.reserve(manifest.files.size());
    for (std::size_t index = 0; index < manifest.files.size(); ++index) {
        const auto& entry = manifest.files[index];
        static_cast<void>(payload(root_, entry));
#if defined(_WIN32)
        auto target = destination(entry, roots, paths[index]);
#else
        std::unique_ptr<JournalDestination> target;
        try { target = destination(entry, roots, paths[index], !entry.verifyOnly && !entry.originalExists); }
        catch (const JournalDirectoryAbsent&) {
            if (!entry.verifyOnly || entry.originalExists) throw;
            needsWrite.push_back(false);
            targets.push_back(nullptr);
            continue;
        }
#endif
#if !defined(_WIN32)
        const auto current = target->read(static_cast<std::size_t>(std::max(entry.originalSize, entry.finalSize)));
        if (entry.verifyOnly) {
            if (!matches(current, entry.originalExists, entry.originalSize, entry.originalHash))
                throw std::runtime_error("journal precondition changed");
            needsWrite.push_back(false);
        } else if (matches(current, true, entry.finalSize, entry.finalHash)) {
            needsWrite.push_back(false);
        } else if (matches(current, entry.originalExists, entry.originalSize, entry.originalHash)) {
            needsWrite.push_back(true);
        } else {
            throw std::runtime_error("journal destination does not match its original or final state");
        }
#endif
        targets.push_back(std::move(target));
    }
    for (const auto& peer : peers) verifyJournalPeer(peer);
#if defined(_WIN32)
    WindowsJournalDirectory windowsJournal(root_.nativeHandle());
    const std::string_view document(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto revalidate = [&] {
        validateSourceSets(manifest, roots, paths);
        validateArchiveInventories(manifest, roots);
        validateReadStringSources(manifest, roots, paths);
        for (const auto& entry : manifest.files) static_cast<void>(payload(root_, entry));
        for (const auto& peer : peers) verifyJournalPeer(peer);
        WindowsJournalSessionState::inspect(windowsJournal, document, manifest, paths, targets);
    };
    revalidate();
    if (!WindowsJournalSessionState::cleanup(windowsJournal, document, manifest, paths, targets, nullptr, {}, true)) {
        WindowsJournalSessionState::prepare(windowsJournal, document, manifest, paths, targets, windowsHook, revalidate);
        WindowsJournalSessionState::apply(windowsJournal, manifest, paths, targets, revalidate,
#ifdef FO4IDE_TEST_HOOKS
            [](std::size_t count) { if (commitHook) commitHook(count); });
#else
            {});
#endif
    }
    WindowsJournalSessionState::cleanup(windowsJournal, document, manifest, paths, targets, windowsHook, revalidate);
#else
#ifdef FO4IDE_TEST_HOOKS
    std::size_t committed = 0;
#endif
    for (const bool plugin : {false, true}) {
        for (std::size_t index = 0; index < manifest.files.size(); ++index) {
            const auto& entry = manifest.files[index];
            if ((entry.kind == "plugin") != plugin || !needsWrite[index]) continue;
            const auto bytesToWrite = payload(root_, entry);
            const auto stage = manifest.transaction + "_" + std::to_string(index) + ".stage";
            targets[index]->replace(entry.originalExists, entry.originalHash, entry.originalSize, bytesToWrite, stage);
#ifdef FO4IDE_TEST_HOOKS
            ++committed;
            if (commitHook) commitHook(committed);
#endif
        }
    }
    root_.removeFile(manifestName);
    for (const auto& entry : manifest.files) root_.removeFile(entry.payload);
#endif
}

}
