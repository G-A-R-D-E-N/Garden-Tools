#include "RadiclePluginHeldLocalization.hpp"
#include "RadiclePluginBa2Source.hpp"

#include <fo4/io/path_text.hpp>
#include <fo4/text/encoding.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace fo4ide::plugin::detail {
namespace {
using Source = fo4::env::StringSource;
fo4::localization::StringTableKind kind(Source source) {
    if (source == Source::dl) return fo4::localization::StringTableKind::dlstrings;
    if (source == Source::il) return fo4::localization::StringTableKind::ilstrings;
    return fo4::localization::StringTableKind::strings;
}
}

HeldLocalizationGeneration HeldLocalizationGeneration::capture(const JournalReadConfiguration& config,
    const std::vector<HeldStringDirectoryInput>& directories, std::string pluginFilename,
    const std::vector<std::string>& requestedLanguages, std::size_t maxWorkingBytes) {
    constexpr std::size_t archiveOverhead = 32 * 1024 * 1024;
    auto remaining = std::min<std::size_t>(maxWorkingBytes, kJournalMaxFileBytes);
    if (pluginFilename.empty() || pluginFilename.size() > 255 || pluginFilename.find_first_of("/\\:") != std::string::npos || pluginFilename.find('\0') != std::string::npos ||
        !fo4::text::is_valid_utf8(pluginFilename) || directories.size() > 16 ||
        config.approvedReadRoots.size() > 16 || config.archiveDirectories.size() > 16 ||
        config.archiveProviderOrder.size() > kJournalMaxArchiveProviders || requestedLanguages.size() > 1365)
        throw std::runtime_error("invalid localization generation input");
    auto stem = pluginFilename;
    if (const auto dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
    if (stem.empty()) throw std::runtime_error("plugin filename has no stem");
    HeldLocalizationGeneration result;
    result.pluginFilename_ = pluginFilename;
    for (const auto& requested : requestedLanguages) {
        auto language = fo4::text::fold(requested);
        if (language.empty() || language.size() > 255 || !std::all_of(language.begin(), language.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; }))
            throw std::runtime_error("invalid localization language");
        result.languages_.insert(std::move(language));
        if (result.languages_.size() > 1365) throw std::runtime_error("too many localization languages");
    }
    std::size_t maxProviderMetadata = 0;
    for (const auto& provider : config.archiveProviderOrder) {
        const auto rootLength = fo4::io::utf8(provider.rootSelector).size();
        if (rootLength > 32768 || provider.directories.size() > 16 || provider.filename.size() > 255)
            throw std::runtime_error("archive provider metadata is too large");
        std::size_t cost = rootLength + provider.filename.size() + 512;
        for (const auto& part : provider.directories) {
            if (part.size() > 255) throw std::runtime_error("archive directory component is too large");
            cost += part.size() + 64;
        }
        maxProviderMetadata = std::max(maxProviderMetadata, cost);
    }
    std::size_t remainingRows = 1000000;
    for (std::size_t i = 0; i < directories.size(); ++i) {
        const auto& input = directories[i];
        if (!input.root) throw std::runtime_error("string read root unavailable");
        std::size_t approved = 0;
        for (const auto& root : config.approvedReadRoots) {
            if (!sameJournalIdentity(root.path, input.root->path())) continue;
            if (!root.lease || !sameJournalIdentity(root.path, root.lease->path()) || !root.lease->sameRoot(*input.root))
                throw std::runtime_error("string read root mismatch");
            ++approved;
        }
        if (approved != 1) throw std::runtime_error("string read root is not authorized");
        for (std::size_t j = 0; j < i; ++j)
            if (input.relative == directories[j].relative && input.root->sameRoot(*directories[j].root))
                throw std::runtime_error("duplicate string read directory");
        auto directory = HeldStringDirectory::capture(input.root, input.relative, pluginFilename, remaining);
        if (directory.accountedRetainedBytes() > remaining) throw std::runtime_error("localization working budget exceeded");
        remaining -= directory.accountedRetainedBytes();
        for (const auto& table : directory.tables()) {
            if (table.table.entries.size() > remainingRows) throw std::runtime_error("localization row budget exceeded");
            remainingRows -= table.table.entries.size(); result.languages_.insert(table.language);
        }
        result.directories_.push_back(std::move(directory));
    }
    if (remaining < archiveOverhead) throw std::runtime_error("localization working budget exceeded");
    remaining -= archiveOverhead;
    result.archives_ = HeldArchiveSet::capture(config, {1000000, remaining});
    if (result.archives_->accountedIndexBytes() > remaining) throw std::runtime_error("localization working budget exceeded");
    remaining -= static_cast<std::size_t>(result.archives_->accountedIndexBytes());
    for (const auto& language : result.archives_->capturedLanguages(pluginFilename)) result.languages_.insert(language);
    struct Request { std::string language{}; Source source{}; std::string path{}; };
    std::vector<Request> requests;
    for (const auto& language : result.languages_) for (const auto source : {Source::normal, Source::dl, Source::il}) {
        const auto path = std::string("Strings/") + stem + '_' + language + '.' + std::string(fo4::env::string_source_extension(source));
        const auto perProvider = maxProviderMetadata + 2 * (path.size() + language.size()) + 512;
        const auto metadata = perProvider * config.archiveProviderOrder.size() + 4 * (path.size() + language.size()) + 512;
        if (metadata > remaining) throw std::runtime_error("localization working budget exceeded");
        remaining -= metadata;
        requests.push_back({language, source, path});
        if (requests.size() > 4096) throw std::runtime_error("too many localization tables");
    }
    std::vector<std::string> paths; paths.reserve(requests.size());
    for (const auto& request : requests) paths.push_back(request.path);
    auto payloads = result.archives_->extractAll(paths, remaining);
    for (const auto& match : payloads) {
        if (match.payload.bytes.size() > remaining) throw std::runtime_error("localization working budget exceeded");
        remaining -= match.payload.bytes.size();
    }
    for (auto& match : payloads) {
        const auto& request = requests[match.requestIndex];
        auto table = parseHeldStringTable(match.payload.bytes, kind(request.source), remaining, remainingRows);
        result.archiveTables_.push_back({request.language, request.source, std::move(match.payload), std::move(table), request.path});
    }
    result.verify();
    result.remainingBytes_ = remaining;
    return result;
}

std::optional<HeldTableView> HeldLocalizationGeneration::find(std::string_view language, Source source) const {
    const auto folded = fo4::text::fold(language);
    for (const auto& directory : directories_) for (const auto& table : directory.tables())
        if (table.language == folded && table.source == source) return HeldTableView{&table.table, &table.bytes, directory.directory() / fo4::io::from_utf8(table.filename), {}};
    for (auto table = archiveTables_.rbegin(); table != archiveTables_.rend(); ++table) if (table->language == folded && table->source == source) {
        auto path = table->payload.provider.rootSelector;
        for (const auto& part : table->payload.provider.directories) path /= fo4::io::from_utf8(part);
        path /= fo4::io::from_utf8(table->payload.provider.filename); path /= fo4::io::from_utf8(table->entryPath);
        return HeldTableView{&table->table, &table->payload.bytes, path, table->payload.provider.filename};
    }
    return std::nullopt;
}

void HeldLocalizationGeneration::verify() const {
    for (const auto& directory : directories_) directory.verify();
    archives_->verify();
}

std::vector<fo4::env::PluginStringTableSnapshot> HeldLocalizationGeneration::copySelectedSnapshots(
    std::string_view language, std::size_t maxBytes) {
    auto remaining = std::min(remainingBytes_, maxBytes);
    std::vector<std::pair<Source, HeldTableView>> selected;
    for (const auto source : {Source::normal, Source::dl, Source::il}) {
        auto view = find(language, source);
        if (!view) continue;
        const auto charge = [&](std::size_t amount) {
            if (amount > remaining) throw std::runtime_error("localization copy budget exceeded");
            remaining -= amount;
        };
        charge(view->bytes->size());
        charge(fo4::io::utf8(view->path).size()); charge(view->archive.size()); charge(512);
        for (const auto& entry : view->table->entries) {
            charge(sizeof(fo4::localization::LocalizedStringEntry) + 64);
            charge(entry.text.size());
        }
        selected.emplace_back(source, std::move(*view));
    }
    std::vector<fo4::env::PluginStringTableSnapshot> result;
    result.reserve(selected.size());
    for (const auto& [source, view] : selected)
        result.push_back({{source, view.path, view.archive, *view.table}, *view.bytes});
    remainingBytes_ -= std::min(remainingBytes_, maxBytes) - remaining;
    return result;
}

void HeldLocalizationGeneration::reserveWorkingBytes(std::size_t bytes) {
    if (bytes > remainingBytes_) throw std::runtime_error("localization working budget exceeded");
    remainingBytes_ -= bytes;
}

std::uint32_t HeldLocalizationGeneration::maximumStringId() const {
    std::uint32_t maximum = 0;
    for (const auto& directory : directories_) for (const auto& table : directory.tables())
        for (const auto& entry : table.table.entries) maximum = std::max(maximum, entry.id);
    for (const auto& table : archiveTables_) for (const auto& entry : table.table.entries)
        maximum = std::max(maximum, entry.id);
    return maximum;
}

JournalReadStringInventory HeldLocalizationGeneration::readOnlyInventory(const std::filesystem::path& root,
                                                                           const std::filesystem::path& relative) const {
    verify();
    const HeldStringDirectory* found = nullptr;
    for (const auto& directory : directories_) {
        if (!sameJournalIdentity(directory.rootPath(), root) || directory.relativeDirectory() != relative) continue;
        if (found) throw std::runtime_error("ambiguous read-only string directory");
        found = &directory;
    }
    if (!found) throw std::runtime_error("read-only string directory is not in the generation");
    std::vector<std::string> components;
    for (const auto& part : relative) components.push_back(fo4::io::utf8(part));
    auto inventory = journalReadStringInventoryFromHeld(found->rootPath(), std::move(components), found->exists(),
                                                         found->tables(), fo4::io::from_utf8(pluginFilename_));
    verify();
    return inventory;
}
}
