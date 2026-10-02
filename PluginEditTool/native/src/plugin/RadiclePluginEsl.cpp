#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <cstddef>
#include <string>
#include <string_view>

#include <fo4/plugin/maintenance.hpp>

namespace fo4ide::plugin {
namespace {

std::string stringArgument(const nlohmann::json& args, std::size_t index) {
    if (!args.is_array() || index >= args.size() || !args[index].is_string()) return {};
    return args[index].get<std::string>();
}

}

Result<nlohmann::json> RadiclePluginBackend::checkEslEligibility(
    const nlohmann::json& arguments) const {
    const auto name = stringArgument(arguments, 0);
    if (name.empty()) {
        return std::unexpected(Error{"PluginMissing", "Plugin name is empty"});
    }

    std::scoped_lock lock(mutex_);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }

    const auto esl = fo4::esl_eligibility(loaded->plugin);
    constexpr std::size_t capacity = 0x1000u - 0x800u;
    const auto nativeCount = esl.native_records;
    const auto outOfRange = esl.out_of_range;

    if (esl.ids_fit_light_range()) {
        return nlohmann::json(
            "'" + name + "' is already ESL-compatible: all " +
            std::to_string(nativeCount) +
            " native record(s) use 12-bit object IDs (<= 0xFFF). "
            "Native compact_to_esl would be a no-op.");
    }

    if (nativeCount > capacity) {
        return nlohmann::json(
            "'" + name + "' CANNOT fit the ESL compacted range: " +
            std::to_string(nativeCount) + " native record(s) exceed the " +
            std::to_string(capacity) +
            "-slot 0x800-0xFFF capacity. Native compact_to_esl must refuse this plugin.");
    }

    return nlohmann::json(
        "'" + name + "' can fit the ESL compacted range by remapping " +
        std::to_string(outOfRange) + " of " + std::to_string(nativeCount) +
        " native record(s) into 0x800-0xFFF. This check is read-only; native "
        "compact_to_esl remains disabled until record re-keying and external-reference safeguards are complete.");
}

}
