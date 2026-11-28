#include <fo4ide/plugin/RadiclePluginAssetCatalog.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4ide::plugin::assets {
namespace {

constexpr std::string_view k_meshes_prefix = "meshes\\";

char lowerChar(char ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

std::string lowerCopy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lowerChar);
    return out;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](char a, char b) { return lowerChar(a) == lowerChar(b); });
}

std::string joinSegments(const std::vector<std::string>& parts, std::size_t count) {
    std::string out;
    for (std::size_t index = 0; index < count && index < parts.size(); ++index) {
        if (index > 0) out.push_back('\\');
        out += parts[index];
    }
    return out;
}

bool isUnderFolder(std::string_view key, std::string_view base) {
    if (base.empty() || key == base) return true;
    return key.size() > base.size() && key.starts_with(base) && key[base.size()] == '\\';
}

bool matchesText(std::string_view haystack, std::string_view needle, TextMatch match) {
    switch (match) {
        case TextMatch::starts_with: return haystack.starts_with(needle);
        case TextMatch::exact: return haystack == needle;
        case TextMatch::contains: break;
    }
    return haystack.find(needle) != std::string_view::npos;
}

std::size_t groupIndexOf(std::string_view signature) {
    const auto& groups = assetGroups();
    for (std::size_t index = 0; index + 1 < groups.size(); ++index) {
        for (const auto& candidate : groups[index].signatures) {
            if (candidate == signature) return index;
        }
    }
    return groups.size() - 1;
}

}  // namespace

const std::vector<AssetGroup>& assetGroups() {
    static const std::vector<AssetGroup> groups{
        {"Statics", {"STAT", "SCOL"}},
        {"Movable Statics", {"MSTT"}},
        {"Furniture", {"FURN"}},
        {"Doors", {"DOOR"}},
        {"Activators", {"ACTI", "TACT"}},
        {"Containers", {"CONT"}},
        {"Lights", {"LIGH"}},
        {"Weapons", {"WEAP", "PROJ"}},
        {"Armor", {"ARMO"}},
        {"Ammo", {"AMMO"}},
        {"Misc", {"MISC", "ALCH", "INGR", "KEYM", "BOOK", "NOTE", "SCRL"}},
        {"Terminals", {"TERM"}},
        {"NPCs", {"NPC_"}},
        {"Flora", {"FLOR", "TREE"}},
        {"Hazards", {"HAZD"}},
        {"Other", {"ADDN", "IDLM", "LVLI"}},
    };
    return groups;
}

std::string groupOfSignature(std::string_view signature) {
    return assetGroups()[groupIndexOf(signature)].name;
}

std::optional<std::string> canonicalGroupName(std::string_view name) {
    for (const auto& group : assetGroups()) {
        if (equalsIgnoreCase(group.name, name)) return group.name;
    }
    return std::nullopt;
}

std::vector<std::string> splitModelPath(std::string_view raw) {
    std::string text(raw);
    std::replace(text.begin(), text.end(), '/', '\\');
    const auto start = text.find_first_not_of(" \t\\");
    text = start == std::string::npos ? std::string{} : text.substr(start);
    if (lowerCopy(std::string_view(text).substr(0, k_meshes_prefix.size())) == k_meshes_prefix) {
        text.erase(0, k_meshes_prefix.size());
    }
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : text) {
        if (ch == '\\') {
            if (!current.empty()) parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) parts.push_back(std::move(current));
    return parts;
}

std::vector<std::string> modelFolderSegments(std::string_view modelPath) {
    auto parts = splitModelPath(modelPath);
    if (!parts.empty()) parts.pop_back();
    return parts;
}

std::string normalizeFolder(std::string_view path) {
    auto parts = splitModelPath(path);
    for (auto& part : parts) part = lowerCopy(part);
    return joinSegments(parts, parts.size());
}

AssetCatalog::AssetCatalog(std::vector<AssetRow> rows) : rows_(std::move(rows)) {
    derived_.reserve(rows_.size());
    for (const auto& row : rows_) {
        Derived derived;
        derived.groupIndex = groupIndexOf(row.signature);
        derived.pluginKey = lowerCopy(row.plugin);
        derived.typeKey = lowerCopy(row.signature);
        derived.editorIdKey = lowerCopy(row.editorId);
        derived.nameKey = lowerCopy(row.name);
        derived.formKeyKey = lowerCopy(row.formKey);
        derived.modelKey = lowerCopy(row.modelPath);
        derived.segmentNames = modelFolderSegments(row.modelPath);
        derived.segmentKeys.reserve(derived.segmentNames.size());
        for (const auto& part : derived.segmentNames) derived.segmentKeys.push_back(lowerCopy(part));
        derived.folderKey = joinSegments(derived.segmentKeys, derived.segmentKeys.size());
        derived_.push_back(std::move(derived));
    }
    order_.resize(rows_.size());
    for (std::size_t index = 0; index < order_.size(); ++index) order_[index] = index;
    std::stable_sort(order_.begin(), order_.end(), [this](std::size_t left, std::size_t right) {
        return derived_[left].editorIdKey < derived_[right].editorIdKey;
    });
}

bool AssetCatalog::passes(std::size_t index, const AssetFilter& filter, const std::string& folderKey,
                          bool skipGroup, bool skipPlugin) const {
    const auto& row = rows_[index];
    const auto& derived = derived_[index];
    if (filter.hideDeleted && row.deleted) return false;
    if (filter.withModelOnly && row.modelPath.empty()) return false;
    if (!skipGroup && !filter.group.empty() &&
        !equalsIgnoreCase(assetGroups()[derived.groupIndex].name, filter.group)) {
        return false;
    }
    if (!skipPlugin && !filter.plugin.empty() && !equalsIgnoreCase(row.plugin, filter.plugin)) return false;
    return isUnderFolder(derived.folderKey, folderKey);
}

bool AssetCatalog::textHit(const Derived& derived, const AssetSearch& query, const std::string& needle) const {
    const auto hit = [&](const std::string& haystack) { return matchesText(haystack, needle, query.match); };
    switch (query.field) {
        case TextField::editorId: return hit(derived.editorIdKey);
        case TextField::name: return hit(derived.nameKey);
        case TextField::formKey: return hit(derived.formKeyKey);
        case TextField::modelPath: return hit(derived.modelKey);
        case TextField::any: break;
    }
    return hit(derived.editorIdKey) || hit(derived.nameKey) || hit(derived.formKeyKey);
}

std::vector<NamedCount> AssetCatalog::groupCounts(const AssetFilter& filter) const {
    const auto folderKey = normalizeFolder(filter.folder);
    const auto& groups = assetGroups();
    std::vector<std::size_t> counts(groups.size(), 0);
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        if (passes(index, filter, folderKey, true, false)) ++counts[derived_[index].groupIndex];
    }
    std::vector<NamedCount> out;
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (counts[group] > 0) out.push_back(NamedCount{groups[group].name, counts[group]});
    }
    return out;
}

std::vector<NamedCount> AssetCatalog::pluginCounts(const AssetFilter& filter) const {
    const auto folderKey = normalizeFolder(filter.folder);
    std::map<std::string, NamedCount> byKey;
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        if (!passes(index, filter, folderKey, false, true)) continue;
        auto& entry = byKey[derived_[index].pluginKey];
        if (entry.name.empty()) entry.name = rows_[index].plugin;
        ++entry.count;
    }
    std::vector<NamedCount> out;
    for (const auto& [key, entry] : byKey) out.push_back(entry);
    return out;
}

FolderBrowse AssetCatalog::browseFolder(const AssetFilter& filter, std::string_view path, std::size_t depth,
                                        std::size_t sampleLimit) const {
    const auto baseParts = splitModelPath(path);
    std::vector<std::string> baseKeys;
    baseKeys.reserve(baseParts.size());
    for (const auto& part : baseParts) baseKeys.push_back(lowerCopy(part));

    std::map<std::string, FolderEntry> nodes;
    FolderBrowse browse;
    for (const auto index : order_) {
        if (!passes(index, filter, std::string{}, false, false)) continue;
        const auto& derived = derived_[index];
        const auto& segments = derived.segmentKeys;
        if (segments.size() < baseKeys.size()) continue;
        if (!std::equal(baseKeys.begin(), baseKeys.end(), segments.begin())) continue;
        ++browse.total;
        if (segments.size() == baseKeys.size()) {
            ++browse.direct;
            continue;
        }
        const auto levels = std::min(depth, segments.size() - baseKeys.size());
        for (std::size_t step = 1; step <= levels; ++step) {
            const auto level = baseKeys.size() + step;
            auto& node = nodes[joinSegments(segments, level)];
            if (node.path.empty()) {
                node.name = derived.segmentNames[level - 1];
                node.path = joinSegments(derived.segmentNames, level);
            }
            ++node.count;
            if (node.samples.size() < sampleLimit) node.samples.push_back(index);
        }
    }
    for (auto& [key, node] : nodes) browse.folders.push_back(std::move(node));
    return browse;
}

AssetPage AssetCatalog::search(const AssetSearch& query) const {
    const auto folderKey = normalizeFolder(query.filter.folder);
    const auto needle = lowerCopy(query.text);
    std::vector<std::size_t> hits;
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        if (!passes(index, query.filter, folderKey, false, false)) continue;
        if (query.hasModel && rows_[index].modelPath.empty() == *query.hasModel) continue;
        if (!needle.empty() && !textHit(derived_[index], query, needle)) continue;
        hits.push_back(index);
    }

    const auto primary = [this, &query](std::size_t index) -> const std::string& {
        const auto& derived = derived_[index];
        switch (query.sort) {
            case AssetSort::name: return derived.nameKey;
            case AssetSort::type: return derived.typeKey;
            case AssetSort::modelPath: return derived.modelKey;
            case AssetSort::plugin: return derived.pluginKey;
            case AssetSort::formKey: return derived.formKeyKey;
            case AssetSort::editorId: break;
        }
        return derived.editorIdKey;
    };
    std::sort(hits.begin(), hits.end(), [&](std::size_t left, std::size_t right) {
        const auto& a = primary(left);
        const auto& b = primary(right);
        if (a != b) return query.descending ? a > b : a < b;
        const auto& ea = derived_[left].editorIdKey;
        const auto& eb = derived_[right].editorIdKey;
        if (ea != eb) return ea < eb;
        return left < right;
    });

    AssetPage page;
    page.total = hits.size();
    const auto begin = std::min(query.offset, hits.size());
    const auto end = std::min(hits.size(), query.offset + query.limit);
    for (auto position = begin; position < end; ++position) page.indices.push_back(hits[position]);
    if (end < hits.size()) page.nextOffset = end;
    return page;
}

std::vector<std::size_t> AssetCatalog::editorIdMatches(std::string_view editorId) const {
    std::vector<std::size_t> out;
    const auto key = lowerCopy(editorId);
    if (key.empty()) return out;
    for (std::size_t index = 0; index < derived_.size(); ++index) {
        if (derived_[index].editorIdKey == key) out.push_back(index);
    }
    return out;
}

}  // namespace fo4ide::plugin::assets
