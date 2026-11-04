#pragma once

#include "RadiclePluginHeldArchives.hpp"
#include "RadiclePluginHeldStrings.hpp"
#include <optional>
#include <set>

namespace fo4ide::plugin::detail {
struct HeldStringDirectoryInput {
    std::shared_ptr<const Ba2ReadRoot> root;
    std::filesystem::path relative;
};
struct HeldTableView {
    const fo4::localization::LocalizedStringTable* table{};
    const std::vector<std::byte>* bytes{};
    std::filesystem::path path{};
    std::string archive{};
};

class HeldLocalizationGeneration {
public:
    static HeldLocalizationGeneration capture(const JournalReadConfiguration& config,
        const std::vector<HeldStringDirectoryInput>& directories, std::string pluginFilename,
        const std::vector<std::string>& requestedLanguages, std::size_t maxWorkingBytes);
    std::optional<HeldTableView> find(std::string_view language, fo4::env::StringSource source) const;
    std::uint32_t maximumStringId() const;
    JournalReadStringInventory readOnlyInventory(const std::filesystem::path& root,
                                                  const std::filesystem::path& relative) const;
    std::vector<fo4::env::PluginStringTableSnapshot> copySelectedSnapshots(std::string_view language,
                                                                            std::size_t maxBytes);
    void reserveWorkingBytes(std::size_t bytes);
    const std::set<std::string>& languages() const noexcept { return languages_; }
    const HeldArchiveSet& archives() const { return *archives_; }
    const std::vector<HeldStringDirectory>& directories() const noexcept { return directories_; }
    void verify() const;
private:
    struct ArchiveTable {
        std::string language{};
        fo4::env::StringSource source{};
        HeldArchivePayload payload{};
        fo4::localization::LocalizedStringTable table{};
        std::string entryPath{};
    };
    std::set<std::string> languages_;
    std::string pluginFilename_;
    std::vector<HeldStringDirectory> directories_;
    std::optional<HeldArchiveSet> archives_;
    std::vector<ArchiveTable> archiveTables_;
    std::size_t remainingBytes_{0};
};
}
