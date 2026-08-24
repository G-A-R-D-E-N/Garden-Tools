#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/form_links.hpp>
#include <fo4/signature.hpp>

namespace fo4ide::plugin {
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

std::string leveledEditorId(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != fo4::sig_edid || subrecord.data.empty()) continue;
        const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
        const auto* end = begin + subrecord.data.size();
        const auto* nul = std::find(begin, end, '\0');
        return std::string(begin, nul);
    }
    return {};
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

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::checkCircularLeveledLists(
    const nlohmann::json& arguments) const {
    const auto filterName = leveledStringArgument(arguments, 0);
    const auto limit = leveledLimitArgument(arguments, 1, 200);
    const auto filterLower = leveledLower(filterName);
    const auto sigLvli = fo4::Signature::from_string("LVLI");
    const auto sigLvln = fo4::Signature::from_string("LVLN");

    std::scoped_lock lock(mutex_);
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
                    .editorId = leveledEditorId(*record),
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

}  // namespace fo4ide::plugin
