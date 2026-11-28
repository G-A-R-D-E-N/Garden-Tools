#include "RadiclePluginSupport.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include <fo4/formid.hpp>
#include <fo4/flags.hpp>
#include <fo4/plugin/maintenance.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/visit.hpp>

namespace fo4ide::plugin {

Result<nlohmann::json> RadiclePluginBackend::checkEslEligibility(
    const nlohmann::json& arguments) const {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }

    const auto esl = fo4::esl_eligibility(loaded->plugin);
    const auto slots = support::eslSlots(loaded->plugin);
    const auto nativeCount = esl.native_records;
    const auto outOfRange = esl.out_of_range;

    if (outOfRange == 0) {
        const auto lowered = fo4::lowered_light_next_form_id(loaded->plugin);
        const auto effect = lowered
            ? "would only lower HEDR nextFormID to " + std::format("0x{:03X}", *lowered) + "."
            : std::string("would be a no-op.");
        return nlohmann::json(
            "'" + name + "' is already ESL-compatible: all " +
            std::to_string(nativeCount) +
            " native record(s) use object IDs within " + slots.range + ". "
            "Native compact_to_esl " + effect + support::lightFlagAdvice(loaded->plugin));
    }

    fo4::Plugin dryRun = loaded->plugin;
    fo4::EslCompactionResult result;
    try {
        result = fo4::compact_to_esl(dryRun);
    } catch (const std::exception& error) {
        return std::unexpected(Error{"EslEligibilityFailed", error.what()});
    }
    if (result.status == fo4::EslCompactionStatus::capacity_exceeded) {
        return nlohmann::json(
            "'" + name + "' CANNOT fit the ESL compacted range: " +
            std::to_string(nativeCount) + " native record(s) exceed the " +
            std::to_string(slots.capacity) +
            "-slot " + slots.range + " capacity. Native compact_to_esl must refuse this plugin.");
    }
    if (result.status == fo4::EslCompactionStatus::blocked_nested_records) {
        std::map<std::string, std::size_t> groups;
        for (const auto& blocker : result.blockers) {
            ++groups[blocker.signature.to_string() + " nested"];
        }
        std::string summary;
        for (const auto& [label, count] : groups) {
            if (!summary.empty()) summary += ", ";
            summary += std::to_string(count) + " " + label;
        }
        return nlohmann::json(
            "'" + name + "' would be BLOCKED: " + std::to_string(result.blockers.size()) +
            " record(s) cannot be remapped here (" + summary + "). This check is read-only; "
            "Native compact_to_esl will refuse this plugin.");
    }
    if (result.status == fo4::EslCompactionStatus::rekey_failed) {
        return nlohmann::json(
            "'" + name + "' would be BLOCKED: " + result.blockers.front().key.to_string() +
            " cannot be remapped here. This check is read-only; "
            "Native compact_to_esl will refuse this plugin.");
    }

    return nlohmann::json(
        "'" + name + "' can fit the ESL compacted range by remapping " +
        std::to_string(outOfRange) + " of " + std::to_string(nativeCount) +
        " native record(s) into " + slots.range + ". This check is read-only; external "
        "references must be updated separately after compact_to_esl.");
}

Result<nlohmann::json> RadiclePluginBackend::setNextObjectId(const nlohmann::json& arguments) {
    const auto name = support::stringArgument(arguments, 0);
    if (name.empty()) return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    const auto requested = support::integerArgument<std::uint32_t>(arguments, 1);
    if (!requested) {
        return std::unexpected(Error{"InvalidNextObjectId", "Next object ID must be an unsigned integer"});
    }

    OpLock lock(*this, OpLock::Mode::Write, {name});
    auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }
    if (const auto guard = writeGuard(*loaded)) return std::unexpected(*guard);

    try {
        std::uint32_t highest = 0;
        fo4::visit_records(loaded->plugin, [&](const fo4::Record& record) {
            if (record.header.signature == fo4::sig_tes4 || record.form_key.owner() != loaded->plugin.key) {
                return;
            }
            highest = std::max(highest, static_cast<std::uint32_t>(record.form_key.local().value()));
        });
        if (*requested > 0x00FFFFFFu) {
            return std::unexpected(Error{"NextObjectIdOutOfRange", "Next object ID must not exceed 0xFFFFFF"});
        }
        if (fo4::classify_plugin_kind(loaded->plugin.tes4.header.flags, loaded->plugin.key) == fo4::PluginKind::light &&
            *requested > 0x1000u) {
            return std::unexpected(Error{
                "NextObjectIdOutOfRange",
                "A light plugin's next object ID must not exceed 0x1000: " + name
            });
        }
        if (*requested <= highest) {
            return std::unexpected(Error{
                "NextObjectIdTooLow",
                "Next object ID 0x" + support::localHex(*requested) +
                    " must be above the highest owned object ID 0x" + support::localHex(highest) +
                    " in " + name
            });
        }

        auto mutated = loaded->plugin;
        fo4::set_hedr_next_form_id(mutated, *requested);
        mutated.original_range.clear();
        mutated.tes4.original_range.clear();
        stagePluginMutationLocked(*loaded, std::move(mutated));
        return nlohmann::json(
            "Set the HEDR next object ID of " + name + " to 0x" + support::localHex(*requested) +
            ". SavePlugin to persist it.");
    } catch (const std::exception& error) {
        return std::unexpected(Error{"SetNextObjectIdFailed", error.what()});
    }
}

}
