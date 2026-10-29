#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>

namespace fo4ide::plugin {
Result<nlohmann::json> RadiclePluginBackend::injectFormsIntoMaster(
    const nlohmann::json& arguments) {
    const auto pluginName = support::stringArgument(arguments, 0);
    const auto masterName = support::stringArgument(arguments, 1);
    auto startText = support::stringArgument(arguments, 2);
    const bool apply = support::boolArgument(arguments, 3);

    if (pluginName.empty() || masterName.empty() || startText.empty()) {
        return std::unexpected(Error{
            "InjectArgumentsMissing",
            "InjectFormsIntoMaster requires plugin, master and starting hexadecimal id"
        });
    }
    if (startText.size() >= 2 && startText[0] == '0' &&
        (startText[1] == 'x' || startText[1] == 'X')) {
        startText.erase(0, 2);
    }

    std::uint32_t start = 0;
    const auto [ptr, parseError] = std::from_chars(
        startText.data(), startText.data() + startText.size(), start, 16);
    if (parseError != std::errc{} || ptr != startText.data() + startText.size() ||
        start == 0 || start > 0xFFFFFFu) {
        return std::unexpected(Error{
            "InvalidFormId",
            "Starting id must be a non-zero 24-bit hexadecimal object id"
        });
    }

    OpLock lock(*this, OpLock::Mode::Write, {pluginName, masterName});
    auto* loaded = findPlugin(pluginName);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + pluginName});
    }
    const auto masterKey = fo4::PluginKey::from_filename(masterName);
    const auto& masters = loaded->plugin.header.masters;
    if (std::none_of(masters.begin(), masters.end(),
                     [&masterKey](const auto& master) { return master.key == masterKey; })) {
        return std::unexpected(Error{"InjectMasterNotListed", "Master is not in the plugin master list: " + masterName});
    }
    const auto* master = findPlugin(masterName);
    if (master == nullptr) {
        return std::unexpected(Error{"InjectMasterNotLoaded", "Master is not loaded: " + masterName});
    }

    std::vector<fo4::FormKey> nativeKeys;
    for (const auto* record : fo4::collect_records(loaded->plugin)) {
        if (record->header.signature == fo4::sig_tes4 ||
            record->form_key.owner() != loaded->plugin.key) {
            continue;
        }
        nativeKeys.push_back(record->form_key);
    }
    std::sort(nativeKeys.begin(), nativeKeys.end(), [](const auto& left, const auto& right) {
        return left.local().value() < right.local().value();
    });

    if (nativeKeys.empty()) {
        return nlohmann::json("'" + pluginName + "' has no records of its own to inject.");
    }
    const auto [lowest, highest] = fo4::local_form_id_range(master->plugin);
    const auto end64 = static_cast<std::uint64_t>(start) + nativeKeys.size() - 1u;
    if (start < lowest || end64 > highest) {
        return std::unexpected(Error{
            "InjectRangeInvalid",
            "Requested range " + support::localHex(start) + "-" +
                support::localHex(static_cast<std::uint32_t>(std::min<std::uint64_t>(end64, 0xFFFFFFu))) +
                " is outside the range of " + masterName
        });
    }
    struct Injection {
        fo4::FormKey oldKey;
        fo4::FormKey newKey;
    };
    std::vector<Injection> mappings;
    mappings.reserve(nativeKeys.size());
    for (std::size_t index = 0; index < nativeKeys.size(); ++index) {
        const auto local = start + static_cast<std::uint32_t>(index);
        mappings.push_back({nativeKeys[index], fo4::FormKey{master->plugin.key, fo4::LocalFormId{local}}});
    }
    for (const auto& mapping : mappings) {
        if (fo4::find_record(loaded->plugin, mapping.newKey) != nullptr) return std::unexpected(Error{"InjectTargetOccupied", "Target already has a record: " + mapping.newKey.to_string()});
    }
    for (const auto& mapping : mappings) {
        if (fo4::find_record(master->plugin, mapping.newKey) != nullptr) return std::unexpected(Error{"InjectTargetOccupied", "Target already has a record in the master: " + mapping.newKey.to_string()});
    }
    for (const auto& mapping : mappings) {
        if (support::topLevelRecord(loaded->plugin, mapping.oldKey) == nullptr) {
            return nlohmann::json("Inject is blocked by nested record " + mapping.oldKey.to_string() +
                                  ". Inject this plugin in xEdit instead.");
        }
    }
    for (const auto& mapping : mappings) {
        if (const auto refusal = support::parentGroupRelabelRefusal(loaded->plugin, mapping.oldKey)) {
            return std::unexpected(*refusal);
        }
    }
    if (!apply) {
        return nlohmann::json(
            "DRY RUN: would inject " + std::to_string(mappings.size()) + " of " +
            std::to_string(nativeKeys.size()) + " record(s) of " + pluginName + " into " +
            masterName + " at " + support::localHex(start) + "-" +
            support::localHex(static_cast<std::uint32_t>(end64)) +
            ", repointing every supported in-plugin reference. Re-run with apply=true.");
    }

    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    try {
        auto mutated = loaded->plugin;
        std::size_t changed = 0;
        for (const auto& mapping : mappings) {
            const auto status = fo4::inject_local_record(mutated, mapping.oldKey, mapping.newKey);
            if (status != fo4::RecordRenumberStatus::renumbered) {
                return std::unexpected(Error{
                    "InjectPluginFailed",
                    "Could not inject " + mapping.oldKey.to_string() + " as " + mapping.newKey.to_string()
                });
            }
            ++changed;
        }

        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Injected " + std::to_string(changed) + " record(s) from " + pluginName + " into " +
            masterName + " at " + support::localHex(start) + "-" +
            support::localHex(static_cast<std::uint32_t>(end64)) +
            ". References from other plugins to the old ids now dangle until patched. SavePlugin to persist.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"InjectPluginFailed", error.what()});
    }
}

}  // namespace fo4ide::plugin
