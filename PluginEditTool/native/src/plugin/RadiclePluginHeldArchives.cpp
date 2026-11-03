#include "RadiclePluginHeldArchives.hpp"
#include "RadiclePluginBa2Source.hpp"

#include <fo4/io/path_text.hpp>
#include <fo4/archive/assets.hpp>
#include <fo4/archive/ba2_hash.hpp>
#include <fo4/text/encoding.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace fo4ide::plugin::detail {
namespace {
bool sameReference(const JournalArchiveReference& a, const JournalArchiveReference& b) {
    return sameJournalIdentity(a.rootSelector, b.rootSelector) && a.directories == b.directories && a.filename == b.filename;
}
}

HeldArchiveSet HeldArchiveSet::capture(const JournalReadConfiguration& config, fo4::archive::Ba2IndexLimits limits) {
    const fo4::archive::Ba2IndexLimits ceiling;
    limits.max_entries = std::min(limits.max_entries, ceiling.max_entries);
    limits.max_bytes = std::min(limits.max_bytes, ceiling.max_bytes);
    const auto indexBudget = limits.max_bytes;
    if (config.archiveDirectories.size() > 16 || config.archiveProviderOrder.size() > kJournalMaxArchiveProviders)
        throw std::runtime_error("archive source limit exceeded");
    for (std::size_t i = 0; i < config.archiveProviderOrder.size(); ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (sameReference(config.archiveProviderOrder[i], config.archiveProviderOrder[j]))
                throw std::runtime_error("duplicate archive provider");
    HeldArchiveSet result;
    result.roots_ = config.approvedReadRoots;
    std::uint64_t total = 0;
    for (const auto& directory : config.archiveDirectories) {
        if (!directory.filename.empty() || directory.directories.size() > 16) throw std::runtime_error("invalid archive directory");
        if (std::any_of(result.inventories_.begin(), result.inventories_.end(), [&](const auto& item) {
            return sameReference(directory, {item.rootSelector, item.directories, {}});
        })) throw std::runtime_error("duplicate archive directory");
        const JournalApprovedReadRoot* approved = nullptr;
        for (const auto& root : config.approvedReadRoots) {
            if (!sameJournalIdentity(root.path, directory.rootSelector)) continue;
            if (approved || !root.lease || !sameJournalIdentity(root.path, root.lease->path()))
                throw std::runtime_error("invalid archive read root");
            approved = &root;
        }
        if (!approved) throw std::runtime_error("archive read root unavailable");
        std::filesystem::path relative;
        for (const auto& part : directory.directories) relative /= fo4::io::from_utf8(part);
        auto names = approved->lease->list(relative, 4096);
        std::erase_if(names, [](const auto& name) { return !fo4::text::fold(name).ends_with(".ba2"); });
        if (names.size() > kJournalMaxArchiveProviders - result.sources_.size()) throw std::runtime_error("too many archives");
        JournalArchiveInventory inventory{directory.rootSelector, directory.directories, {}};
        std::set<std::string> folded;
        for (const auto& name : names) {
            if (!folded.insert(fo4::text::fold(name)).second) throw std::runtime_error("ambiguous archive names");
            auto source = approved->lease->open(relative / fo4::io::from_utf8(name), kJournalMaxArchiveBytes);
            if (total > 4 * kJournalMaxArchiveBytes - source->size()) throw std::runtime_error("archive source size limit exceeded");
            total += source->size();
            const JournalArchiveReference reference{directory.rootSelector, directory.directories, name};
            const bool selected = std::any_of(config.archiveProviderOrder.begin(), config.archiveProviderOrder.end(),
                                               [&](const auto& item) { return sameReference(item, reference); });
            std::optional<fo4::archive::Ba2Snapshot> snapshot;
            std::string digest;
            if (selected) {
                snapshot.emplace(fo4::archive::Ba2Snapshot::open(source, limits));
                if (!snapshot->has_name_table()) throw std::runtime_error("archive provider has no names");
                limits.max_entries -= snapshot->entries().size(); limits.max_bytes -= snapshot->index_bytes();
                digest = snapshot->archive_sha256();
            } else digest = fo4::archive::hash_byte_source(source);
            inventory.providers.push_back({name, source->size(), digest});
            result.sources_.push_back({reference, std::move(source), inventory.providers.back().size, std::move(digest), std::move(snapshot)});
        }
        result.inventories_.push_back(std::move(inventory));
    }
    for (const auto& reference : config.archiveProviderOrder) {
        const auto found = std::find_if(result.sources_.begin(), result.sources_.end(), [&](const auto& source) { return sameReference(source.reference, reference); });
        if (found == result.sources_.end() || std::any_of(result.providers_.begin(), result.providers_.end(), [&](const auto& item) { return sameReference(item.reference, reference); }))
            throw std::runtime_error("invalid archive provider order");
        if (!found->snapshot) throw std::runtime_error("archive provider snapshot is unavailable");
        result.providers_.push_back({reference, *found->snapshot});
    }
    result.indexBytes_ = indexBudget - limits.max_bytes;
    result.verify();
    return result;
}

std::vector<JournalArchiveReference> HeldArchiveSet::providerOrder() const {
    std::vector<JournalArchiveReference> result;
    result.reserve(providers_.size());
    for (const auto& provider : providers_) result.push_back(provider.reference);
    return result;
}

std::optional<HeldArchivePayload> HeldArchiveSet::extract(std::string_view path, std::uint64_t maxBytes) const {
    return extractMany({std::string(path)}, maxBytes).front();
}

std::vector<std::optional<HeldArchivePayload>> HeldArchiveSet::extractMany(
    const std::vector<std::string>& paths, std::uint64_t maxBytes) const {
    if (paths.size() > 4096 || maxBytes > kJournalMaxFileBytes) throw std::runtime_error("archive extraction limit exceeded");
    verify();
    std::vector<std::optional<HeldArchivePayload>> result(paths.size());
    std::vector<bool> selected(paths.size(), false);
    for (auto provider = providers_.rbegin(); provider != providers_.rend(); ++provider) {
        std::vector<std::size_t> entries, positions;
        for (std::size_t i = 0; i < paths.size(); ++i) {
            if (selected[i]) continue;
            const auto index = provider->archive.find(paths[i]);
            if (index) { selected[i] = true; entries.push_back(*index); positions.push_back(i); }
        }
        if (entries.empty()) continue;
        auto payloads = provider->archive.extract_many(entries, maxBytes);
        for (std::size_t i = 0; i < payloads.size(); ++i) {
            if (payloads[i].size > maxBytes) throw std::runtime_error("archive extraction limit exceeded");
            maxBytes -= payloads[i].size;
            result[positions[i]] = HeldArchivePayload{provider->reference, std::move(payloads[i].bytes)};
        }
    }
    verify();
    return result;
}

std::vector<HeldArchiveMatch> HeldArchiveSet::extractAll(const std::vector<std::string>& paths, std::uint64_t maxBytes) const {
    if (paths.size() > 4096 || maxBytes > kJournalMaxFileBytes) throw std::runtime_error("archive extraction limit exceeded");
    std::map<std::string, std::size_t> requested;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto encoded = fo4::text::utf8_to_windows1252(paths[i]);
        if (!encoded || !requested.emplace(fo4::archive::normalize_ba2_name(*encoded), i).second)
            throw std::runtime_error("ambiguous or unsupported archive table path");
    }
    verify();
    std::vector<HeldArchiveMatch> result;
    for (const auto& provider : providers_) {
        std::set<std::string> seen;
        std::vector<std::size_t> entries, requests;
        const auto& indexed = provider.archive.entries();
        for (std::size_t i = 0; i < indexed.size(); ++i) {
            const auto encoded = fo4::text::utf8_to_windows1252(indexed[i].path);
            if (!encoded) throw std::runtime_error("archive entry name cannot be resolved");
            const auto key = fo4::archive::normalize_ba2_name(*encoded);
            const auto wanted = requested.find(key);
            if (wanted == requested.end()) continue;
            if (!seen.insert(key).second) throw std::runtime_error("ambiguous archive table path");
            if (result.size() + entries.size() == 4096) throw std::runtime_error("too many archive table sources");
            entries.push_back(i); requests.push_back(wanted->second);
        }
        if (entries.empty()) continue;
        auto payloads = provider.archive.extract_many(entries, maxBytes);
        for (std::size_t i = 0; i < payloads.size(); ++i) {
            if (payloads[i].size > maxBytes) throw std::runtime_error("archive extraction limit exceeded");
            maxBytes -= payloads[i].size;
            result.push_back({requests[i], {provider.reference, std::move(payloads[i].bytes)}});
        }
    }
    verify();
    return result;
}

std::set<std::string> HeldArchiveSet::languages(std::string_view pluginFilename) const {
    verify();
    auto result = capturedLanguages(pluginFilename);
    verify();
    return result;
}

std::set<std::string> HeldArchiveSet::capturedLanguages(std::string_view pluginFilename) const {
    auto stem = fo4::text::fold(pluginFilename);
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
    if (stem.empty()) throw std::runtime_error("plugin filename has no stem");
    const auto prefix = std::string("strings/") + stem + '_';
    std::set<std::string> result;
    for (const auto& provider : providers_) {
        for (const auto& entry : provider.archive.entries()) {
            const auto normalized = fo4::archive::normalize_asset_path(entry.path);
            const auto folded = fo4::text::fold(normalized);
            if (!folded.starts_with(prefix)) continue;
            const auto dot = folded.rfind('.');
            if (dot == std::string::npos || dot <= prefix.size()) continue;
            const auto extension = std::string_view(folded).substr(dot);
            if (extension != ".strings" && extension != ".dlstrings" && extension != ".ilstrings") continue;
            const auto language = folded.substr(prefix.size(), dot - prefix.size());
            if (language.size() > 255) throw std::runtime_error("archive language token is too long");
            if (!std::all_of(language.begin(), language.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; })) continue;
            const auto requested = fo4::archive::normalize_asset_path(prefix + language + std::string(extension));
            if (requested != normalized) throw std::runtime_error("archive language path cannot be resolved consistently");
            result.insert(language);
            if (result.size() > 1365) throw std::runtime_error("too many archive languages");
        }
    }
    return result;
}

void HeldArchiveSet::verify() const {
    for (const auto& inventory : inventories_) {
        const auto root = std::find_if(roots_.begin(), roots_.end(), [&](const auto& item) { return sameJournalIdentity(item.path, inventory.rootSelector); });
        if (root == roots_.end() || !root->lease) throw std::runtime_error("archive read root unavailable");
        std::filesystem::path relative;
        for (const auto& part : inventory.directories) relative /= fo4::io::from_utf8(part);
        auto names = root->lease->list(relative, 4096);
        std::erase_if(names, [](const auto& name) { return !fo4::text::fold(name).ends_with(".ba2"); });
        if (names.size() != inventory.providers.size()) throw std::runtime_error("archive inventory changed");
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] != inventory.providers[i].filename) throw std::runtime_error("archive inventory changed");
    }
    for (const auto& source : sources_) {
        if (source.bytes->size() != source.size || fo4::archive::hash_byte_source(source.bytes) != source.digest)
            throw std::runtime_error("archive source changed");
    }
}
}
