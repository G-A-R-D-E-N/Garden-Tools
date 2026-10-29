#include "RadiclePluginSupport.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include <fo4/plugin/maintenance.hpp>
#include <fo4/plugin/mutation.hpp>

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

    if (nativeCount > slots.capacity) {
        return nlohmann::json(
            "'" + name + "' CANNOT fit the ESL compacted range: " +
            std::to_string(nativeCount) + " native record(s) exceed the " +
            std::to_string(slots.capacity) +
            "-slot " + slots.range + " capacity. Native compact_to_esl must refuse this plugin.");
    }

    const auto blockers = support::eslCompactionBlockers(loaded->plugin, slots.first, slots.last);
    if (!blockers.empty()) {
        std::map<std::string, std::size_t> groups;
        for (const auto& blocker : blockers) {
            ++groups[blocker.signature + (blocker.nested ? " nested" : " with child groups")];
        }
        std::string summary;
        for (const auto& [label, count] : groups) {
            if (!summary.empty()) summary += ", ";
            summary += std::to_string(count) + " " + label;
        }
        return nlohmann::json(
            "'" + name + "' would be BLOCKED: " + std::to_string(blockers.size()) +
            " record(s) cannot be remapped here (" + summary + "). This check is read-only; "
            "Native compact_to_esl will refuse this plugin.");
    }

    return nlohmann::json(
        "'" + name + "' can fit the ESL compacted range by remapping " +
        std::to_string(outOfRange) + " of " + std::to_string(nativeCount) +
        " native record(s) into " + slots.range + ". This check is read-only; external "
        "references must be updated separately after compact_to_esl.");
}

}
