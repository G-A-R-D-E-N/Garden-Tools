#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4ide::plugin::assets {

struct AssetRow {
    std::string formKey;
    std::string editorId;
    std::string name;
    std::string signature;
    std::string modelPath;
    std::string plugin;
    bool deleted{false};
};

struct AssetGroup {
    std::string name;
    std::vector<std::string> signatures;
};

const std::vector<AssetGroup>& assetGroups();
std::string groupOfSignature(std::string_view signature);
std::optional<std::string> canonicalGroupName(std::string_view name);

std::vector<std::string> splitModelPath(std::string_view raw);
std::vector<std::string> modelFolderSegments(std::string_view modelPath);
std::string normalizeFolder(std::string_view path);

struct AssetFilter {
    std::string group;
    std::string folder;
    std::string plugin;
    bool withModelOnly{false};
    bool hideDeleted{true};
};

struct NamedCount {
    std::string name;
    std::size_t count{0};
};

struct FolderEntry {
    std::string name;
    std::string path;
    std::size_t count{0};
    std::vector<std::size_t> samples;
};

struct FolderBrowse {
    std::size_t total{0};
    std::size_t direct{0};
    std::vector<FolderEntry> folders;
};

enum class TextField { any, editorId, name, formKey, modelPath };
enum class TextMatch { contains, starts_with, exact };
enum class AssetSort { editorId, name, type, modelPath, plugin, formKey };

struct AssetSearch {
    AssetFilter filter;
    std::string text;
    TextField field{TextField::any};
    TextMatch match{TextMatch::contains};
    std::optional<bool> hasModel;
    AssetSort sort{AssetSort::editorId};
    bool descending{false};
    std::size_t offset{0};
    std::size_t limit{50};
};

struct AssetPage {
    std::size_t total{0};
    std::vector<std::size_t> indices;
    std::optional<std::size_t> nextOffset;
};

class AssetCatalog {
public:
    explicit AssetCatalog(std::vector<AssetRow> rows);

    const std::vector<AssetRow>& rows() const noexcept { return rows_; }

    std::vector<NamedCount> groupCounts(const AssetFilter& filter) const;
    std::vector<NamedCount> pluginCounts(const AssetFilter& filter) const;
    FolderBrowse browseFolder(const AssetFilter& filter, std::string_view path, std::size_t depth,
                              std::size_t sampleLimit) const;
    AssetPage search(const AssetSearch& query) const;
    std::vector<std::size_t> editorIdMatches(std::string_view editorId) const;

private:
    struct Derived {
        std::size_t groupIndex{0};
        std::string pluginKey;
        std::string typeKey;
        std::string editorIdKey;
        std::string nameKey;
        std::string formKeyKey;
        std::string modelKey;
        std::string folderKey;
        std::vector<std::string> segmentKeys;
        std::vector<std::string> segmentNames;
    };

    bool passes(std::size_t index, const AssetFilter& filter, const std::string& folderKey, bool skipGroup,
                bool skipPlugin) const;
    bool textHit(const Derived& derived, const AssetSearch& query, const std::string& needle) const;

    std::vector<AssetRow> rows_;
    std::vector<Derived> derived_;
    std::vector<std::size_t> order_;
};

}
