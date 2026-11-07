#include "RadiclePluginSupport.hpp"

#include <fo4ide/core/JsonInt.hpp>
#include <fo4ide/plugin/BaseObjectTypes.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/environment/environment.hpp>
#include <fo4/plugin/load_order.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

namespace {

constexpr std::size_t k_default_list_limit = 100;
constexpr std::size_t k_max_list_limit = 200;
constexpr std::size_t k_max_regex_length = 256;

struct BaseObjectRow {
    fo4::FormKey key;
    std::string signatureText;
    std::uint32_t hexId{0};
    std::uint32_t plugin{0};
    bool deleted{false};
    std::string editorId;
    std::string name;
    std::string modelPath;
};

struct CachedListing {
    std::vector<std::uint32_t> order;
    std::map<std::string, std::size_t> typeCounts;
};

enum class MatchMode { contains, starts_with, exact, regex };

struct BaseObjectQuery {
    std::string type;
    std::string text;
    std::set<std::string> scopes;
    MatchMode match{MatchMode::contains};
    std::string activePlugin;
    std::string changedBy;
    bool hideDeleted{true};
    bool placeableWithModel{false};
    std::string sortBy{"editorId"};
    bool descending{false};
    std::size_t offset{0};
    std::size_t limit{k_default_list_limit};
};

char lowerAscii(char ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

std::string lowerCopy(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lowerAscii);
    return out;
}

int compareIgnoreCase(std::string_view a, std::string_view b) {
    const auto shared = std::min(a.size(), b.size());
    for (std::size_t index = 0; index < shared; ++index) {
        const auto left = lowerAscii(a[index]);
        const auto right = lowerAscii(b[index]);
        if (left != right) return left < right ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

std::string hexIdText(std::uint32_t hexId) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", hexId);
    return buffer;
}

// Runtime prefix per plugin; must match the slot table Radicle uses to resolve runtime IDs.
std::vector<std::uint32_t> runtimePrefixes(const fo4::env::Environment& env) {
    std::vector<const fo4::Plugin*> order;
    order.reserve(env.plugins().size());
    for (std::size_t index = 0; index < env.plugins().size(); ++index) order.push_back(&env.plugin(index));
    const auto runtime = fo4::build_runtime_form_id_context(order);

    std::vector<std::uint32_t> prefixes(env.plugins().size(), 0u);
    for (std::size_t slot = 0; slot < runtime.full_plugins.size(); ++slot) {
        if (const auto index = env.find_plugin(runtime.full_plugins[slot].key.filename())) {
            prefixes[*index] = static_cast<std::uint32_t>(slot) << 24;
        }
    }
    for (std::size_t slot = 0; slot < runtime.light_plugins.size(); ++slot) {
        if (const auto index = env.find_plugin(runtime.light_plugins[slot].key.filename())) {
            prefixes[*index] = (static_cast<std::uint32_t>(fo4::k_light_runtime_index) << 24) |
                               (static_cast<std::uint32_t>(slot) << 12);
        }
    }
    return prefixes;
}

std::uint32_t hexIdFor(const fo4::env::Environment& env,
                       const std::vector<std::uint32_t>& prefixes,
                       const fo4::FormKey& key) {
    const auto owner = env.find_plugin(key.owner().filename());
    if (!owner) return key.local().value() & fo4::k_full_local_mask;
    const bool light = env.plugins()[*owner].kind == fo4::PluginKind::light;
    const auto local = key.local().value() & (light ? fo4::k_light_local_mask : fo4::k_full_local_mask);
    return prefixes[*owner] | local;
}

std::string stringField(const nlohmann::json& object, const char* name) {
    if (!object.contains(name) || !object.at(name).is_string()) return {};
    return object.at(name).get<std::string>();
}

std::string filterKey(const BaseObjectQuery& query) {
    std::string key = query.type + '\x1f' + query.text + '\x1f' + query.activePlugin + '\x1f' +
                      query.changedBy + '\x1f';
    key += query.hideDeleted ? '1' : '0';
    key += query.placeableWithModel ? '1' : '0';
    key += std::to_string(static_cast<int>(query.match)) + '\x1f' + query.sortBy + '\x1f';
    key += query.descending ? "desc" : "asc";
    key += '\x1f';
    for (const auto& scope : query.scopes) key += scope + ',';
    return key;
}

Result<BaseObjectQuery> parseQuery(const nlohmann::json& arguments) {
    nlohmann::json object = arguments.is_array() ? (arguments.empty() ? nlohmann::json::object() : arguments[0])
                                                 : arguments;
    if (object.is_null()) object = nlohmann::json::object();
    if (!object.is_object()) {
        return std::unexpected(Error{"BaseObjectQueryInvalid", "ListBaseObjects query must be an object"});
    }
    const auto invalid = [](std::string message) {
        return std::unexpected(Error{"BaseObjectQueryInvalid", std::move(message)});
    };

    BaseObjectQuery query;
    query.type = stringField(object, "type");
    std::ranges::transform(query.type, query.type.begin(), [](char ch) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    });
    if (!query.type.empty() && !placeableBaseTypeName(query.type)) {
        return invalid("Not a placeable base type: " + query.type);
    }
    query.text = stringField(object, "query");

    if (object.contains("scopes") && object.at("scopes").is_array()) {
        for (const auto& scope : object.at("scopes")) {
            if (!scope.is_string()) return invalid("ListBaseObjects scopes must be strings");
            const auto name = scope.get<std::string>();
            if (name != "editorId" && name != "name" && name != "hexId" && name != "modelPath" &&
                name != "plugin") {
                return invalid("Unknown ListBaseObjects scope: " + name);
            }
            query.scopes.insert(name);
        }
    }
    if (query.scopes.empty()) query.scopes = {"editorId", "name", "hexId"};

    const auto match = stringField(object, "match");
    if (match.empty() || match == "contains") query.match = MatchMode::contains;
    else if (match == "startsWith") query.match = MatchMode::starts_with;
    else if (match == "exact") query.match = MatchMode::exact;
    else if (match == "regex") query.match = MatchMode::regex;
    else return invalid("Unknown ListBaseObjects match mode: " + match);
    if (query.match == MatchMode::regex && query.text.size() > k_max_regex_length) {
        return invalid("ListBaseObjects regex pattern is longer than 256 characters");
    }

    query.activePlugin = stringField(object, "activePlugin");
    query.changedBy = stringField(object, "changedBy");
    if (object.contains("hideDeleted") && object.at("hideDeleted").is_boolean()) {
        query.hideDeleted = object.at("hideDeleted").get<bool>();
    }
    if (object.contains("placeableWithModel") && object.at("placeableWithModel").is_boolean()) {
        query.placeableWithModel = object.at("placeableWithModel").get<bool>();
    }

    const auto sortBy = stringField(object, "sortBy");
    if (!sortBy.empty()) {
        static const std::set<std::string> sorts{"editorId", "hexId", "name", "type", "modelPath", "plugin"};
        if (!sorts.contains(sortBy)) return invalid("Unknown ListBaseObjects sortBy: " + sortBy);
        query.sortBy = sortBy;
    }
    const auto sortDir = stringField(object, "sortDir");
    if (sortDir == "desc") query.descending = true;
    else if (!sortDir.empty() && sortDir != "asc") return invalid("Unknown ListBaseObjects sortDir: " + sortDir);

    if (object.contains("offset") && !object.at("offset").is_null()) {
        const auto offset = core::jsonInt(object.at("offset"));
        if (!offset || *offset < 0) return invalid("ListBaseObjects offset must be a non-negative integer");
        query.offset = static_cast<std::size_t>(*offset);
    }
    if (object.contains("limit") && !object.at("limit").is_null()) {
        const auto limit = core::jsonInt(object.at("limit"));
        if (!limit) return invalid("ListBaseObjects limit must be an integer");
        query.limit = static_cast<std::size_t>(std::clamp(*limit, 1, static_cast<int>(k_max_list_limit)));
    }
    return query;
}

}  // namespace

struct BaseObjectIndex {
    std::shared_ptr<const fo4::env::Environment> env;
    std::vector<BaseObjectRow> rows;
    mutable std::mutex cacheMutex;
    mutable std::string cacheKey;
    mutable std::shared_ptr<const CachedListing> cache;
};

std::shared_ptr<const BaseObjectIndex> RadiclePluginBackend::baseObjectIndex() const {
    std::scoped_lock lock(baseIndexMutex_);
    if (baseIndex_ && baseIndexEpoch_ == mutationEpoch_) return baseIndex_;

    auto index = std::make_shared<BaseObjectIndex>();
    index->env = borrowedEnvironment();
    const auto& env = *index->env;
    const auto prefixes = runtimePrefixes(env);
    for (const auto& type : kPlaceableBaseTypes) {
        for (const auto* ref : env.winners_of(fo4::Signature::from_string(type.signature))) {
            const auto meta = env.metadata(*ref);
            BaseObjectRow row;
            row.key = meta.form_key;
            row.signatureText = std::string(type.signature);
            row.plugin = static_cast<std::uint32_t>(meta.plugin);
            row.deleted = meta.deleted;
            row.editorId = std::string(meta.editor_id.value_or(std::string_view{}));
            if (meta.name.kind == fo4::env::NameKind::inline_text) row.name = std::string(meta.name.text);
            if (meta.model) row.modelPath = std::string(*meta.model);
            row.hexId = hexIdFor(env, prefixes, row.key);
            index->rows.push_back(std::move(row));
        }
    }
    baseIndex_ = index;
    baseIndexEpoch_ = mutationEpoch_;
    return index;
}

Result<nlohmann::json> RadiclePluginBackend::listBaseObjects(const nlohmann::json& arguments) const {
    const auto parsed = parseQuery(arguments);
    if (!parsed) return std::unexpected(parsed.error());
    const auto& query = *parsed;

    OpLock lock(*this, OpLock::Mode::Read);
    try {
        const auto index = baseObjectIndex();
        const auto& env = *index->env;
        const auto& rows = index->rows;

        std::optional<std::size_t> activeIndex;
        if (!query.activePlugin.empty()) {
            activeIndex = env.find_plugin(query.activePlugin);
            if (!activeIndex) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + query.activePlugin});
        }
        std::optional<std::size_t> changedIndex;
        if (!query.changedBy.empty()) {
            changedIndex = env.find_plugin(query.changedBy);
            if (!changedIndex) return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + query.changedBy});
        }

        std::unique_ptr<std::regex> pattern;
        const auto needle = lowerCopy(query.text);
        if (query.match == MatchMode::regex && !query.text.empty()) {
            try {
                pattern = std::make_unique<std::regex>(query.text, std::regex::ECMAScript | std::regex::icase);
            } catch (const std::regex_error& error) {
                return std::unexpected(Error{"BaseObjectQueryInvalid", std::string("Invalid regex: ") + error.what()});
            }
        }

        const auto fieldMatches = [&](std::string_view field) {
            if (pattern) return std::regex_search(field.data(), field.data() + field.size(), *pattern);
            const auto lower = lowerCopy(field);
            switch (query.match) {
                case MatchMode::starts_with: return lower.starts_with(needle);
                case MatchMode::exact: return lower == needle;
                default: return lower.find(needle) != std::string::npos;
            }
        };
        const auto textMatches = [&](const BaseObjectRow& row) {
            if (query.text.empty()) return true;
            for (const auto& scope : query.scopes) {
                std::string hexText;
                std::string_view field;
                if (scope == "editorId") {
                    field = row.editorId;
                } else if (scope == "name") {
                    field = row.name;
                } else if (scope == "modelPath") {
                    field = row.modelPath;
                } else if (scope == "plugin") {
                    field = env.plugins()[row.plugin].key.filename();
                } else {
                    hexText = hexIdText(row.hexId);
                    field = hexText;
                }
                if (fieldMatches(field)) return true;
            }
            return false;
        };
        const auto passesCommon = [&](const BaseObjectRow& row) {
            if (query.hideDeleted && row.deleted) return false;
            if (query.placeableWithModel && row.modelPath.empty()) return false;
            if (activeIndex && row.plugin != *activeIndex) return false;
            if (changedIndex) {
                const auto versions = env.versions(row.key);
                const bool changed = std::ranges::any_of(versions, [&](const fo4::env::RecordRef& ref) {
                    return ref.plugin == *changedIndex;
                });
                if (!changed) return false;
            }
            return textMatches(row);
        };

        const auto key = filterKey(query);
        std::shared_ptr<const CachedListing> listing;
        {
            std::scoped_lock cacheLock(index->cacheMutex);
            if (index->cache && index->cacheKey == key) listing = index->cache;
        }
        if (!listing) {
            auto built = std::make_shared<CachedListing>();
            for (std::uint32_t position = 0; position < rows.size(); ++position) {
                const auto& row = rows[position];
                if (!passesCommon(row)) continue;
                ++built->typeCounts[row.signatureText];
                if (!query.type.empty() && row.signatureText != query.type) continue;
                built->order.push_back(position);
            }
            const auto sortKey = [&](std::uint32_t a, std::uint32_t b) {
                const auto& left = rows[a];
                const auto& right = rows[b];
                int order = 0;
                if (query.sortBy == "hexId") {
                    order = left.hexId < right.hexId ? -1 : (left.hexId > right.hexId ? 1 : 0);
                } else if (query.sortBy == "name") {
                    order = compareIgnoreCase(left.name, right.name);
                } else if (query.sortBy == "type") {
                    order = compareIgnoreCase(left.signatureText, right.signatureText);
                } else if (query.sortBy == "modelPath") {
                    order = compareIgnoreCase(left.modelPath, right.modelPath);
                } else if (query.sortBy == "plugin") {
                    order = compareIgnoreCase(env.plugins()[left.plugin].key.filename(),
                                              env.plugins()[right.plugin].key.filename());
                } else {
                    order = compareIgnoreCase(left.editorId, right.editorId);
                }
                if (order == 0) order = compareIgnoreCase(left.editorId, right.editorId);
                if (order == 0) order = left.hexId < right.hexId ? -1 : (left.hexId > right.hexId ? 1 : 0);
                return query.descending ? order > 0 : order < 0;
            };
            std::sort(built->order.begin(), built->order.end(), sortKey);

            std::scoped_lock cacheLock(index->cacheMutex);
            index->cacheKey = key;
            index->cache = built;
            listing = built;
        }

        const auto total = listing->order.size();
        nlohmann::json items = nlohmann::json::array();
        const auto end = std::min(total, query.offset + query.limit);
        for (auto position = std::min(query.offset, total); position < end; ++position) {
            const auto& row = rows[listing->order[position]];
            const auto typeName = placeableBaseTypeName(row.signatureText);
            items.push_back({
                {"formKey", row.key.to_string()},
                {"hexId", hexIdText(row.hexId)},
                {"editorId", row.editorId},
                {"name", row.name},
                {"signature", row.signatureText},
                {"typeName", typeName ? std::string(*typeName) : row.signatureText},
                {"modelPath", row.modelPath},
                {"winningPlugin", std::string(env.plugins()[row.plugin].key.filename())},
                {"deleted", row.deleted},
                {"hasModel", !row.modelPath.empty()},
            });
        }
        nlohmann::json counts = nlohmann::json::object();
        for (const auto& [signature, count] : listing->typeCounts) counts[signature] = count;
        return nlohmann::json(nlohmann::json{
            {"total", total},
            {"offset", query.offset},
            {"limit", query.limit},
            {"sortBy", query.sortBy},
            {"sortDir", query.descending ? "desc" : "asc"},
            {"typeCounts", std::move(counts)},
            {"items", std::move(items)},
        }.dump());
    } catch (const std::exception& error) {
        return std::unexpected(Error{"BaseObjectListFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
