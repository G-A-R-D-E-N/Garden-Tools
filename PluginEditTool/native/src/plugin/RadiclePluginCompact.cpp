#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
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

}  // namespace

Result<nlohmann::json> RadiclePluginBackend::compactToEsl(
    const nlohmann::json& arguments) {
    const auto pluginName = compactStringArgument(arguments, 0);
    if (pluginName.empty()) {
        return std::unexpected(Error{"PluginMissing", "CompactToEsl requires a plugin name"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    const auto [first, last, capacity, range] = support::eslSlots(loaded->plugin);

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
        if (local >= first && local <= last) {
            inRange.insert(local);
        } else {
            outOfRange.push_back(record->form_key);
        }
    }

    if (outOfRange.empty()) {
        const auto lowered = fo4::lowered_light_next_form_id(loaded->plugin);
        if (!lowered) {
            return nlohmann::json(
                "'" + pluginName +
                "' is already ESL-compatible (all native FormIDs within " + range + ")." +
                support::lightFlagAdvice(loaded->plugin));
        }
        try {
            auto mutated = loaded->plugin;
            fo4::set_hedr_next_form_id(mutated, *lowered);
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json(
                "'" + pluginName + "' already has all native FormIDs within " + range +
                "; lowered HEDR nextFormID to " + std::format("0x{:03X}", *lowered) + "." +
                support::lightFlagAdvice(loaded->plugin));
        } catch (const std::exception& error) {
            return std::unexpected(Error{"CompactToEslFailed", error.what()});
        }
    }
    if (nativeCount > capacity) {
        return std::unexpected(Error{
            "EslCapacityExceeded",
            "Cannot compact '" + pluginName + "': it has " +
                std::to_string(nativeCount) + " native records, exceeding the " +
                std::to_string(capacity) + "-record ESL range"
        });
    }

    const auto blockers = support::eslCompactionBlockers(loaded->plugin, first, last);
    if (!blockers.empty()) {
        const auto& blocker = blockers.front();
        if (blocker.nested) {
            return std::unexpected(Error{
                "NestedRecordCompactionUnsupported",
                "Cannot compact '" + pluginName + "': out-of-range nested record " +
                    blocker.key.to_string() +
                    " requires parent GRUP label rekeying. Compact this plugin in xEdit instead."
            });
        }
        return std::unexpected(support::parentGroupRelabelError(blocker.key));
    }

    std::sort(outOfRange.begin(), outOfRange.end(), [](const auto& left, const auto& right) {
        return left.local().value() < right.local().value();
    });

    try {
        auto mutated = loaded->plugin;
        const auto originalFlags = mutated.header.tes4_flags;
        mutated.header.tes4_flags = fo4::RecordFlags{
            originalFlags.value() | fo4::RecordFlags::small_file};
        std::uint32_t next = first;
        std::size_t remapped = 0;
        for (const auto& oldKey : outOfRange) {
            while (next <= last && inRange.contains(next)) ++next;
            if (next > last) {
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

        fo4::set_hedr_next_form_id(mutated, next);
        mutated.header.tes4_flags = originalFlags;
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Compacted " + pluginName + ": remapped " +
            std::to_string(remapped) +
            " record(s) into the ESL range (" + range +
            ") and fixed all supported in-plugin references." + support::lightFlagAdvice(loaded->plugin));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CompactToEslFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
