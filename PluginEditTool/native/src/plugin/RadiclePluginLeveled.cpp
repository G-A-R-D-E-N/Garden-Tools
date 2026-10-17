#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/forms/leveled_list.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {

using support::editorId;

namespace {

std::string leveledStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

std::size_t leveledLimitArgument(const nlohmann::json& args,
                                 std::size_t index,
                                 std::size_t fallback) {
    if (!args.is_array() || index >= args.size() || !args[index].is_number_integer()) return fallback;
    const auto raw = args[index].get<std::int64_t>();
    if (raw <= 0) return fallback;
    return std::min<std::size_t>(static_cast<std::size_t>(raw), 1000);
}

std::string leveledLower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

struct LeveledOwner {
    std::string plugin;
    std::string editorId;
    std::string type;
};

std::string cycleIdentity(const std::vector<fo4::FormKey>& cycle) {
    std::string out;
    for (const auto& key : cycle) {
        if (!out.empty()) out += "->";
        out += key.to_string();
    }
    return out;
}

constexpr const char* k_invalid_entry =
    "Leveled list entry needs level, item, count and chanceNone within range; extra needs owner, globalOrRank and itemCondition";

nlohmann::json leveledEntryJson(const fo4::forms::LeveledListEntry& entry) {
    nlohmann::json result{
        {"level", entry.level},
        {"item", support::formKeyJson(entry.item)},
        {"count", entry.count},
        {"chanceNone", entry.chance_none},
        {"extra", nullptr}
    };
    if (entry.extra.has_value()) {
        result["extra"] = {
            {"owner", support::formKeyJson(entry.extra->owner)},
            {"globalOrRank", entry.extra->global_or_rank},
            {"itemCondition", entry.extra->item_condition}
        };
    }
    return result;
}

nlohmann::json leveledListJson(const fo4::forms::LeveledList& list) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : list.entries) entries.push_back(leveledEntryJson(entry));
    return {
        {"formKey", list.key.to_string()},
        {"chanceNone", list.chance_none},
        {"maxCount", list.max_count},
        {"flags", list.flags},
        {"useGlobal", list.use_global ? support::formKeyJson(*list.use_global) : nlohmann::json(nullptr)},
        {"epicLootChance", list.epic_loot_chance ? support::formKeyJson(*list.epic_loot_chance) : nlohmann::json(nullptr)},
        {"entries", std::move(entries)}
    };
}

Result<fo4::forms::LeveledListEntry> leveledEntryFromJson(const nlohmann::json& value) {
    const auto level = support::integerField<std::uint16_t>(value, "level");
    const auto count = support::integerField<std::uint16_t>(value, "count");
    const auto chanceNone = support::integerField<std::uint8_t>(value, "chanceNone");
    const auto item = support::formKeyArgument(support::stringField(value, "item"));
    if (!level || !count || !chanceNone || !item) {
        return std::unexpected(Error{"InvalidLeveledListEntry", k_invalid_entry});
    }

    fo4::forms::LeveledListEntry entry;
    entry.level = *level;
    entry.item = *item;
    entry.count = *count;
    entry.chance_none = *chanceNone;
    if (value.contains("extra") && !value.at("extra").is_null()) {
        const auto& extra = value.at("extra");
        fo4::FormKey owner{};
        if (extra.contains("owner") && !extra.at("owner").is_null()) {
            const auto parsed = support::formKeyArgument(support::stringField(extra, "owner"));
            if (!parsed) return std::unexpected(parsed.error());
            owner = *parsed;
        }
        const auto globalOrRank = support::integerField<std::uint32_t>(extra, "globalOrRank");
        if (!globalOrRank || !extra.contains("itemCondition") ||
            !extra.at("itemCondition").is_number()) {
            return std::unexpected(Error{"InvalidLeveledListEntry", k_invalid_entry});
        }
        entry.extra = fo4::forms::LeveledListExtraData{
            owner, *globalOrRank, extra.at("itemCondition").get<float>()};
    }
    return entry;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::checkCircularLeveledLists(
    const nlohmann::json& arguments) const {
    const auto filterName = leveledStringArgument(arguments, 0);
    const auto limit = leveledLimitArgument(arguments, 1, 200);
    const auto filterLower = leveledLower(filterName);
    const auto sigLvli = fo4::Signature::from_string("LVLI");
    const auto sigLvln = fo4::Signature::from_string("LVLN");

    OpLock lock(*this, OpLock::Mode::Read);
    std::map<fo4::FormKey, std::vector<fo4::FormKey>> edges;
    std::map<fo4::FormKey, LeveledOwner> owners;

    try {
        // Walk in load order so a later version replaces the previous version's
        // edges, matching the game's winning-record semantics and the C# oracle.
        for (const auto& loaded : plugins_) {
            const auto links = fo4::collect_form_links(loaded.plugin);
            std::map<fo4::FormKey, std::vector<fo4::FormKey>> byRecord;
            for (const auto& link : links) {
                byRecord[link.record].push_back(link.key);
            }

            for (const auto* record : fo4::collect_records(loaded.plugin)) {
                if (record->header.signature != sigLvli && record->header.signature != sigLvln) continue;
                edges[record->form_key] = byRecord[record->form_key];
                owners[record->form_key] = {
                    .plugin = loaded.name,
                    .editorId = editorId(*record),
                    .type = record->header.signature.to_string(),
                };
            }
        }
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CircularLeveledListScanFailed", error.what()});
    }

    if (edges.empty()) {
        return nlohmann::json("No leveled lists found in the load order.");
    }

    std::map<fo4::FormKey, int> state;
    std::vector<std::vector<fo4::FormKey>> cycles;
    std::set<std::string> reported;

    for (const auto& [root, ignored] : edges) {
        static_cast<void>(ignored);
        if (state[root] != 0) continue;

        std::vector<fo4::FormKey> path{root};
        std::vector<std::pair<fo4::FormKey, std::size_t>> stack{{root, 0}};
        state[root] = 1;

        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto node = frame.first;
            const auto edgeIt = edges.find(node);
            if (edgeIt == edges.end() || frame.second >= edgeIt->second.size()) {
                state[node] = 2;
                stack.pop_back();
                if (!path.empty() && path.back() == node) path.pop_back();
                continue;
            }

            const auto child = edgeIt->second[frame.second++];
            if (!edges.contains(child)) continue;
            const auto childState = state[child];
            if (childState == 1) {
                const auto start = std::find(path.begin(), path.end(), child);
                std::vector<fo4::FormKey> cycle;
                if (start != path.end()) cycle.assign(start, path.end());
                else cycle.push_back(node);
                cycle.push_back(child);

                bool matchesFilter = filterLower.empty();
                if (!matchesFilter) {
                    for (const auto& key : cycle) {
                        const auto owner = owners.find(key);
                        if (owner != owners.end() && leveledLower(owner->second.plugin) == filterLower) {
                            matchesFilter = true;
                            break;
                        }
                    }
                }
                if (!matchesFilter) continue;

                const auto identity = cycleIdentity(cycle);
                if (reported.insert(identity).second) {
                    cycles.push_back(std::move(cycle));
                    if (cycles.size() >= limit) break;
                }
            } else if (childState == 0) {
                state[child] = 1;
                path.push_back(child);
                stack.emplace_back(child, 0);
            }
        }
        if (cycles.size() >= limit) break;
    }

    if (cycles.empty()) {
        return nlohmann::json(
            "No circular leveled lists across " + std::to_string(edges.size()) +
            " leveled list(s)" +
            (filterName.empty() ? std::string{} : " (filtered to " + filterName + ")") + ".");
    }

    std::string report =
        std::to_string(cycles.size()) + " circular leveled list(s) found across " +
        std::to_string(edges.size()) +
        " checked. Each of these can hang or crash the engine when it resolves:";
    for (const auto& cycle : cycles) {
        report += "\n  ";
        for (std::size_t index = 0; index < cycle.size(); ++index) {
            if (index != 0) report += " -> ";
            const auto owner = owners.find(cycle[index]);
            if (owner != owners.end() && !owner->second.editorId.empty()) {
                report += owner->second.editorId + " [" + cycle[index].to_string() + "]";
            } else {
                report += cycle[index].to_string();
            }
        }
    }
    if (cycles.size() >= limit) {
        report += "\nReport capped at " + std::to_string(limit) + " cycle(s).";
    }
    return nlohmann::json(std::move(report));
}

Result<nlohmann::json> RadiclePluginBackend::getLeveledList(const nlohmann::json& arguments) const {
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 0));
    if (!key) return std::unexpected(key.error());

    OpLock lock(*this, OpLock::Mode::Read);
    const auto version = versionPlugin(support::stringArgument(arguments, 1), *key);
    if (!version) return std::unexpected(version.error());
    const auto* source = *version;
    try {
        return leveledListJson(fo4::forms::decode_leveled_list(*source, *key));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"LeveledListReadFailed", error.what()});
    }
}

Result<nlohmann::json> RadiclePluginBackend::setLeveledListSettings(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto chanceNone = support::integerArgument<std::uint8_t>(arguments, 2);
    const auto maxCount = support::integerArgument<std::uint8_t>(arguments, 3);
    const auto flags = support::integerArgument<std::uint8_t>(arguments, 4);
    if (!key) return std::unexpected(key.error());
    if (!chanceNone || !maxCount || !flags) {
        return std::unexpected(Error{
            "InvalidLeveledListSettings",
            "chanceNone, maxCount and flags must be integers from 0 to 255"
        });
    }

    const auto staged = stageTypedRecordEdit(
        pluginName, *key, "LeveledListWriteFailed", [&](fo4::Plugin& mutated) {
            fo4::forms::set_leveled_list_settings(mutated, *key, *chanceNone, *maxCount, *flags);
        });
    if (!staged) return std::unexpected(staged.error());
    if (!*staged) return nlohmann::json("No change for leveled list settings of " + key->to_string() + " in " + pluginName + ".");
    return nlohmann::json("Updated leveled list settings for " + key->to_string() + " in " + pluginName + ".");
}

Result<nlohmann::json> RadiclePluginBackend::editLeveledListEntry(const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto key = support::formKeyArgument(support::stringArgument(arguments, 1));
    const auto op = support::stringArgument(arguments, 2);
    const auto index = support::integerArgument<std::uint32_t>(arguments, 3);
    if (!key) return std::unexpected(key.error());
    if (op != "add" && op != "set" && op != "remove") {
        return std::unexpected(Error{
            "InvalidLeveledListOperation",
            "Leveled list entry operation must be add, set or remove"
        });
    }
    if (!index) {
        return std::unexpected(Error{"InvalidLeveledListEntry", "Leveled list entry index must be a non-negative integer"});
    }

    std::optional<fo4::forms::LeveledListEntry> entry;
    if (op != "remove") {
        auto parsed = leveledEntryFromJson(support::argumentAt(arguments, 4));
        if (!parsed) return std::unexpected(parsed.error());
        entry = std::move(*parsed);
    }

    const auto staged = stageTypedRecordEdit(
        pluginName, *key, "LeveledListWriteFailed", [&](fo4::Plugin& mutated) {
            if (op != "remove" && entry->extra.has_value() &&
                fo4::find_record(mutated, *key)->header.signature == fo4::Signature::from_string("LVSP")) {
                throw fo4::forms::LeveledListError(
                    "LVSP entries take only LVLO; extra (COED) is not allowed on LVSP");
            }
            const auto position = static_cast<std::size_t>(*index);
            if (op == "add") {
                fo4::forms::insert_leveled_list_entry(mutated, *key, position, *entry);
            } else if (op == "set") {
                auto replacement = *entry;
                const auto existing = fo4::forms::decode_leveled_list(mutated, *key).entries;
                if (position < existing.size()) {
                    replacement.reserved_before_form_id = existing[position].reserved_before_form_id;
                    replacement.reserved_tail = existing[position].reserved_tail;
                }
                fo4::forms::set_leveled_list_entry(mutated, *key, position, replacement);
            } else {
                fo4::forms::remove_leveled_list_entry(mutated, *key, position);
            }
        });
    if (!staged) return std::unexpected(staged.error());
    if (!*staged) {
        return nlohmann::json(
            "No change for leveled list entry " + std::to_string(*index) + " in " + key->to_string() +
            " in " + pluginName + ".");
    }
    return nlohmann::json(
        "Updated leveled list entry " + std::to_string(*index) + " in " + key->to_string() +
        " in " + pluginName + ".");
}

}  // namespace fo4ide::plugin
