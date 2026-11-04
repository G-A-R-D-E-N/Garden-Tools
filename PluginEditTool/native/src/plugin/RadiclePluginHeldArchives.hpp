#pragma once

#include "RadiclePluginJournalSession.hpp"
#include <fo4/archive/ba2_reader.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::detail {
struct HeldArchivePayload {
    JournalArchiveReference provider{};
    std::vector<std::byte> bytes{};
};
struct HeldArchiveMatch {
    std::size_t requestIndex{};
    HeldArchivePayload payload{};
};

class HeldArchiveSet {
public:
    static HeldArchiveSet capture(const JournalReadConfiguration& config, fo4::archive::Ba2IndexLimits limits = {});
    std::optional<HeldArchivePayload> extract(std::string_view path, std::uint64_t maxBytes) const;
    std::vector<std::optional<HeldArchivePayload>> extractMany(const std::vector<std::string>& paths,
                                                                std::uint64_t maxBytes) const;
    std::vector<HeldArchiveMatch> extractAll(const std::vector<std::string>& paths, std::uint64_t maxBytes) const;
    std::set<std::string> languages(std::string_view pluginFilename) const;
    const std::vector<JournalArchiveInventory>& inventories() const noexcept { return inventories_; }
    const std::vector<JournalApprovedReadRoot>& readRoots() const noexcept { return roots_; }
    std::vector<JournalArchiveReference> providerOrder() const;
    std::uint64_t accountedIndexBytes() const noexcept { return indexBytes_; }
    void verify() const;
private:
    friend class HeldLocalizationGeneration;
    std::set<std::string> capturedLanguages(std::string_view pluginFilename) const;
    struct Source {
        JournalArchiveReference reference{};
        std::shared_ptr<const fo4::archive::Ba2ByteSource> bytes{};
        std::uint64_t size{};
        std::string digest{};
        std::optional<fo4::archive::Ba2Snapshot> snapshot{};
    };
    struct Provider {
        JournalArchiveReference reference{};
        fo4::archive::Ba2Snapshot archive;
    };
    std::vector<JournalArchiveInventory> inventories_;
    std::uint64_t indexBytes_{0};
    std::vector<JournalApprovedReadRoot> roots_;
    std::vector<Source> sources_;
    std::vector<Provider> providers_;
};
}
