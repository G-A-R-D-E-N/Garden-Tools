#include "RadiclePluginSupport.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <utility>

#include <fo4/plugin/maintenance.hpp>

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

    const auto slots = support::eslSlots(loaded->plugin);
    try {
        auto mutated = loaded->plugin;
        const auto result = fo4::compact_to_esl(mutated);
        if (result.status == fo4::EslCompactionStatus::already_compatible) {
            if (result.next_form_id == loaded->plugin.header.next_form_id) {
                return nlohmann::json(
                    "'" + pluginName +
                    "' is already ESL-compatible (all native FormIDs within " + slots.range + ")." +
                    support::lightFlagAdvice(loaded->plugin));
            }
            stagePluginMutationLocked(*loaded, std::move(mutated));
            return nlohmann::json(
                "'" + pluginName + "' already has all native FormIDs within " + slots.range +
                "; lowered HEDR nextFormID to " + std::format("0x{:03X}", result.next_form_id) + "." +
                support::lightFlagAdvice(loaded->plugin));
        }
        if (result.status == fo4::EslCompactionStatus::capacity_exceeded) {
            return std::unexpected(Error{
                "EslCapacityExceeded",
                "Cannot compact '" + pluginName + "': it has " +
                    std::to_string(fo4::esl_eligibility(loaded->plugin).native_records) +
                    " native records, exceeding the " + std::to_string(slots.capacity) +
                    "-record ESL range"
            });
        }
        if (result.status == fo4::EslCompactionStatus::blocked_nested_records) {
            return std::unexpected(Error{
                "NestedRecordCompactionUnsupported",
                "Cannot compact '" + pluginName + "': out-of-range nested record " +
                    result.blockers.front().key.to_string() +
                    " requires parent GRUP label rekeying. Compact this plugin in xEdit instead."
            });
        }
        if (result.status == fo4::EslCompactionStatus::rekey_failed) {
            return std::unexpected(Error{
                "CompactRekeyFailed",
                "Could not remap " + result.blockers.front().key.to_string() + " into the ESL range"
            });
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Compacted " + pluginName + ": remapped " + std::to_string(result.remapped) +
            " record(s) into the ESL range (" + slots.range +
            ") and fixed all supported in-plugin references." +
            support::lightFlagAdvice(loaded->plugin));
    } catch (const std::exception& error) {
        return std::unexpected(Error{"CompactToEslFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
