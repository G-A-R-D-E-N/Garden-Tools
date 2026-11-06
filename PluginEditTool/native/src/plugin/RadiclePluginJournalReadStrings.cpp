#include "RadiclePluginJournalReadStrings.hpp"

#include "RadiclePluginBa2Source.hpp"
#include "RadiclePluginHeldStrings.hpp"
#include "RadiclePluginJournalLock.hpp"
#include "RadiclePluginJournalManifest.hpp"

#include <fo4/io/path_text.hpp>
#include <fo4/io/sha256.hpp>
#include <fo4/archive/ba2_reader.hpp>
#include <fo4/text/encoding.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace fo4ide::plugin::detail {
namespace {
constexpr std::size_t kMaxInventories = 16;
constexpr std::size_t kMaxFiles = 128;
constexpr std::uint64_t kMaxBytes = 512ull * 1024 * 1024;

bool safePart(std::string_view value) {
    return !value.empty() && value.size() <= 255 && value != "." && value != ".." &&
        value.find_first_of("/\\:\0", 0, 4) == value.npos && fo4::text::is_valid_utf8(value);
}

std::pair<std::string, std::vector<std::string>> key(const std::filesystem::path& root,
                                                     const std::vector<std::string>& directories) {
    auto foldedDirectories = directories;
    for (auto& part : foldedDirectories) part = fo4::text::fold(part);
    return {fo4::text::fold(fo4::io::utf8(root)), std::move(foldedDirectories)};
}

void validateInventory(const JournalReadStringInventory& row, const std::filesystem::path& plugin,
                       std::size_t& files, std::uint64_t& bytes) {
    const auto root = fo4::io::utf8(row.rootSelector);
    if (root.empty() || root.size() > 32768 || root.find('\0') != root.npos || !fo4::text::is_valid_utf8(root) ||
        !row.rootSelector.is_absolute() || row.rootSelector != row.rootSelector.lexically_normal() ||
        row.directories.size() > 128 || (!row.exists && !row.files.empty()))
        throw std::runtime_error("invalid loose string inventory");
    for (const auto& part : row.directories) if (!safePart(part)) throw std::runtime_error("invalid loose string directory");
    files += row.files.size();
    if (files > kMaxFiles) throw std::runtime_error("too many loose string files");
    std::set<std::string> folded;
    std::string previous;
    for (const auto& file : row.files) {
        if (!safePart(file.filename) || !isJournalStringName(file.filename, plugin) || file.sha256.size() != 64 ||
            !std::all_of(file.sha256.begin(), file.sha256.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            (!previous.empty() && file.filename <= previous) || !folded.insert(fo4::text::fold(file.filename)).second ||
            file.size > kMaxBytes - bytes)
            throw std::runtime_error("invalid or ambiguous loose string file");
        previous = file.filename;
        bytes += file.size;
    }
}

const JournalApprovedReadRoot& leaseFor(const std::filesystem::path& selector,
                                        const std::vector<JournalApprovedReadRoot>& approved) {
    const JournalApprovedReadRoot* found = nullptr;
    for (const auto& row : approved) {
        if (!sameJournalIdentity(selector, row.path)) continue;
        if (!row.lease || !sameJournalIdentity(row.path, row.lease->path()) || found)
            throw std::runtime_error("ambiguous or mismatched loose string read root");
        found = &row;
    }
    if (!found) throw std::runtime_error("loose string read root is not authorized");
    return *found;
}

std::string encodeRows(const std::vector<JournalReadStringInventory>& rows) {
    nlohmann::json document = nlohmann::json::array();
    for (const auto& row : rows) {
        nlohmann::json files = nlohmann::json::array();
        for (const auto& file : row.files) files.push_back({{"filename", file.filename}, {"size", file.size}, {"sha256", file.sha256}});
        document.push_back({{"root", fo4::io::utf8(row.rootSelector)}, {"directories", row.directories},
                            {"exists", row.exists}, {"files", std::move(files)}});
    }
    return document.dump();
}
}

std::string encodeJournalReadStringInventories(const std::vector<JournalReadStringInventory>& inventories,
                                              const std::filesystem::path& plugin) {
    if (inventories.size() > kMaxInventories) throw std::runtime_error("too many loose string inventories");
    std::size_t files = 0; std::uint64_t bytes = 0;
    std::set<std::pair<std::string, std::vector<std::string>>> unique;
    for (const auto& row : inventories) {
        validateInventory(row, plugin, files, bytes);
        if (!unique.insert(key(row.rootSelector, row.directories)).second)
            throw std::runtime_error("duplicate loose string inventory");
    }
    return encodeRows(inventories);
}

std::vector<JournalReadStringInventory> decodeJournalReadStringInventories(std::string_view text,
                                                                            const std::filesystem::path& plugin) {
    if (text.size() > 1024 * 1024) throw std::runtime_error("loose string inventory JSON too large");
    const auto depthLimit = [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
        if (depth > 8) throw std::runtime_error("loose string inventory nesting is too deep");
        return true;
    };
    const auto document = nlohmann::json::parse(text, depthLimit);
    if (!document.is_array() || document.size() > kMaxInventories) throw std::runtime_error("invalid loose string inventories");
    std::vector<JournalReadStringInventory> result;
    std::set<std::pair<std::string, std::vector<std::string>>> unique;
    std::size_t files = 0; std::uint64_t bytes = 0;
    for (const auto& row : document) {
        if (!row.is_object() || row.size() != 4 || !row.contains("root") || !row["root"].is_string() ||
            !row.contains("directories") || !row["directories"].is_array() || !row.contains("exists") ||
            !row["exists"].is_boolean() || !row.contains("files") || !row["files"].is_array())
            throw std::runtime_error("invalid loose string inventory row");
        const auto root = row["root"].get<std::string>();
        if (root.find('\0') != root.npos || !fo4::text::is_valid_utf8(root)) throw std::runtime_error("invalid loose string root");
        JournalReadStringInventory inventory; inventory.rootSelector = fo4::io::from_utf8(root); inventory.exists = row["exists"].get<bool>();
        for (const auto& part : row["directories"]) { if (!part.is_string()) throw std::runtime_error("invalid loose string directory"); inventory.directories.push_back(part.get<std::string>()); }
        for (const auto& item : row["files"]) {
            if (!item.is_object() || item.size() != 3 || !item.contains("filename") || !item["filename"].is_string() ||
                !item.contains("size") || !item["size"].is_number_unsigned() || !item.contains("sha256") || !item["sha256"].is_string())
                throw std::runtime_error("invalid loose string file row");
            inventory.files.push_back({item["filename"].get<std::string>(), item["size"].get<std::uint64_t>(), item["sha256"].get<std::string>()});
        }
        validateInventory(inventory, plugin, files, bytes);
        if (!unique.insert(key(inventory.rootSelector, inventory.directories)).second) throw std::runtime_error("duplicate loose string inventory");
        result.push_back(std::move(inventory));
    }
    return result;
}

std::vector<JournalReadStringInventory> captureJournalReadStringInventories(
    const std::vector<JournalArchiveReference>& directories, const std::vector<JournalApprovedReadRoot>& approved,
    const std::filesystem::path& plugin) {
    if (directories.size() > kMaxInventories) throw std::runtime_error("too many loose string inventories");
    std::vector<JournalReadStringInventory> result;
    std::size_t fileCount = 0; std::uint64_t total = 0;
    for (const auto& ref : directories) {
        if (std::any_of(result.begin(), result.end(), [&](const auto& existing) {
            return sameJournalIdentity(existing.rootSelector, ref.rootSelector) && existing.directories == ref.directories;
        })) throw std::runtime_error("duplicate loose string inventory");
        if (!ref.rootSelector.is_absolute() || ref.rootSelector != ref.rootSelector.lexically_normal() || ref.directories.size() > 128)
            throw std::runtime_error("invalid loose string directory reference");
        const auto& approvedRoot = leaseFor(ref.rootSelector, approved);
        JournalReadStringInventory row{ref.rootSelector, ref.directories, false, {}};
        std::filesystem::path directory;
        for (const auto& part : ref.directories) { if (!safePart(part)) throw std::runtime_error("invalid loose string directory"); directory /= fo4::io::from_utf8(part); }
        const auto entries = approvedRoot.lease->listOptional(directory, 4096);
        row.exists = entries.has_value();
        if (entries) for (const auto& name : *entries) {
            if (!fo4::text::is_valid_utf8(name) || !isJournalStringName(name, plugin)) continue;
            if (!safePart(name)) throw std::runtime_error("invalid loose string filename");
            if (++fileCount > kMaxFiles) throw std::runtime_error("too many loose string files");
            auto source = approvedRoot.lease->open(directory / fo4::io::from_utf8(name), kMaxBytes - total);
            const auto size = source->size();
            if (size > kMaxBytes - total) throw std::runtime_error("loose string inventory too large");
            total += size;
            row.files.push_back({name, size, fo4::archive::hash_byte_source(source)});
        }
        std::sort(row.files.begin(), row.files.end(), [](const auto& a, const auto& b) { return a.filename < b.filename; });
        std::set<std::string> folded;
        for (const auto& file : row.files) if (!folded.insert(fo4::text::fold(file.filename)).second)
            throw std::runtime_error("ambiguous loose string filename");
        result.push_back(std::move(row));
    }
    return result;
}

JournalReadStringInventory journalReadStringInventoryFromHeld(
    const std::filesystem::path& rootSelector, std::vector<std::string> directories, bool exists,
    const std::vector<HeldStringTable>& tables, const std::filesystem::path& plugin) {
    JournalReadStringInventory result{rootSelector, std::move(directories), exists, {}};
    if (tables.size() > kMaxFiles) throw std::runtime_error("too many held loose string files");
    std::uint64_t total = 0;
    for (const auto& table : tables) {
        if (table.bytes.size() > kMaxBytes - total) throw std::runtime_error("held loose string inventory too large");
        total += table.bytes.size();
        result.files.push_back({table.filename, table.bytes.size(), fo4::io::sha256_hex(table.bytes)});
    }
    std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.filename < b.filename; });
    std::size_t count = 0; total = 0;
    validateInventory(result, plugin, count, total);
    return result;
}

void verifyJournalReadStringInventories(const std::vector<JournalReadStringInventory>& inventories,
                                       const std::vector<JournalApprovedReadRoot>& approved,
                                       const std::filesystem::path& plugin) {
    for (std::size_t i = 0; i < inventories.size(); ++i) for (std::size_t j = i + 1; j < inventories.size(); ++j)
        if (sameJournalIdentity(inventories[i].rootSelector, inventories[j].rootSelector) &&
            inventories[i].directories == inventories[j].directories) throw std::runtime_error("duplicate loose string inventory");
    const auto current = captureJournalReadStringInventories([&] {
        std::vector<JournalArchiveReference> refs; refs.reserve(inventories.size());
        for (const auto& row : inventories) refs.push_back({row.rootSelector, row.directories, {}});
        return refs;
    }(), approved, plugin);
    if (encodeJournalReadStringInventories(current, plugin) != encodeJournalReadStringInventories(inventories, plugin))
        throw std::runtime_error("loose string inventory changed");
}
}
