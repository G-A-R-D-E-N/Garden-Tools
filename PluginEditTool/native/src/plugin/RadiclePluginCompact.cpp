#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>

namespace fo4ide::plugin {
namespace {

std::string compactStringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

bool compactTopLevelContains(const fo4::Plugin& plugin, const fo4::FormKey& key) {
    for (const auto& group : plugin.groups) {
        if (group.header.group_type != static_cast<std::int32_t>(fo4::GroupType::top)) continue;
        for (const auto& child : group.children) {
            const auto* record = std::get_if<fo4::Record>(&child);
            if (record != nullptr && record->form_key == key) return true;
        }
    }
    return false;
}

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::compactToEsl(
    const nlohmann::json& arguments) {
    const auto pluginName = compactStringArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "CompactToEsl requires a plugin name"});
    }

    std::scoped_lock lock(mutex_);
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    constexpr std::uint32_t kFirst = 0x800u;
    constexpr std::uint32_t kLast = 0xFFFu;
    constexpr std::size_t kCapacity = kLast - kFirst + 1u;

    std::set<std::uint32_t> inRange;
    std::vector<fo4::FormKey> outOfRange;
    std::size_t nativeCount = 0;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == fo4::sig_tes4 ||
            record->form_key.owner() != loaded->plugin.key) {
            continue;
        }
        ++nativeCount;
        const auto local = record->form_key.local().value();
        if (local >= kFirst && local <= kLast) {
            inRange.insert(local);
        } else if (local > kLast) {
            outOfRange.push_back(record->form_key);
        }
    }

    if (outOfRange.empty()) {
        return nlohmann::json(
            "'" + pluginName +
            "' is already ESL-compatible (all native FormIDs within 0x800-0xFFF).");
    }
    if (nativeCount > kCapacity) {
        return std::unexpected(Error{
            "EslCapacityExceeded",
            "Cannot compact '" + pluginName + "': it has " +
                std::to_string(nativeCount) + " native records, exceeding the " +
                std::to_string(kCapacity) + "-record ESL range"
        });
    }

    for (const auto& key : outOfRange) {
        if (compactTopLevelContains(loaded->plugin, key)) continue;
        return std::unexpected(Error{
            "NestedRecordCompactionUnsupported",
            "Cannot compact '" + pluginName + "': out-of-range nested record " +
                key.to_string() +
                " requires parent GRUP label rekeying. Compact this plugin in xEdit instead."
        });
    }

    std::sort(outOfRange.begin(), outOfRange.end(), [](const auto& left, const auto& right) {
        return left.local().value() < right.local().value();
    });

    try {
        auto mutated = loaded->plugin;
        std::uint32_t next = kFirst;
        std::size_t remapped = 0;
        for (const auto& oldKey : outOfRange) {
            while (next <= kLast && inRange.contains(next)) ++next;
            if (next > kLast) {
                return std::unexpected(Error{
                    "EslCapacityExceeded",
                    "No free ESL FormID remains while compacting " + pluginName
                });
            }

            const auto status = fo4::renumber_local_record(
                mutated, oldKey, fo4::LocalFormId{next});
            if (status != fo4::RecordRenumberStatus::renumbered) {
                return std::unexpected(Error{
                    "CompactRekeyFailed",
                    "Could not remap " + oldKey.to_string() + " to local FormID " +
                        std::to_string(next)
                });
            }
            inRange.insert(next);
            ++next;
            ++remapped;
        }

        fo4::write_plugin_file(mutated, loaded->path);
        loaded->plugin = std::move(mutated);
        std::error_code ec;
        loaded->loadedTime = std::filesystem::last_write_time(loaded->path, ec);
        rebuildGraphLocked();
        return nlohmann::json(
            "Compacted " + pluginName + ": remapped " +
            std::to_string(remapped) +
            " record(s) into the ESL range (0x800-0xFFF) and fixed all supported in-plugin references.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CompactToEslFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
