#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include <fo4/plugin/maintenance.hpp>
#include <fo4/plugin/mutation.hpp>

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

    OpLock lock(*this, OpLock::Mode::Read);
    const auto* loaded = findPlugin(name);
    if (loaded == nullptr) {
        return std::unexpected(Error{"PluginNotLoaded", "Plugin is not loaded: " + name});
    }

    const auto esl = fo4::esl_eligibility(loaded->plugin);
    fo4::Plugin targetPolicy;
    targetPolicy.header = loaded->plugin.header;
    targetPolicy.header.tes4_flags = fo4::RecordFlags{
        targetPolicy.header.tes4_flags.value() | fo4::RecordFlags::small_file};
    const auto [first, last] = fo4::local_form_id_range(targetPolicy);
    const std::size_t capacity = last - first + 1u;
    const std::string range = first == 1u ? "0x001-0xFFF" : "0x800-0xFFF";
    const auto nativeCount = esl.native_records;
    const auto records = fo4::collect_records(loaded->plugin);
    const auto outOfRange = esl.out_of_range + static_cast<std::size_t>(std::count_if(
        records.begin(), records.end(), [&](const auto* record) {
            return record->header.signature != fo4::sig_tes4 &&
                   record->form_key.owner() == loaded->plugin.key &&
                   record->form_key.local().value() < first;
        }));

    if (outOfRange == 0) {
        return nlohmann::json(
            "'" + name + "' is already ESL-compatible: all " +
            std::to_string(nativeCount) +
            " native record(s) use object IDs within " + range + ". "
            "Native compact_to_esl would be a no-op.");
    }

    if (nativeCount > capacity) {
        return nlohmann::json(
            "'" + name + "' CANNOT fit the ESL compacted range: " +
            std::to_string(nativeCount) + " native record(s) exceed the " +
            std::to_string(capacity) +
            "-slot " + range + " capacity. Native compact_to_esl must refuse this plugin.");
    }

    return nlohmann::json(
        "'" + name + "' can fit the ESL compacted range by remapping " +
        std::to_string(outOfRange) + " of " + std::to_string(nativeCount) +
        " native record(s) into " + range + ". This check is read-only; external "
        "references must be updated separately after compact_to_esl.");
}

}
